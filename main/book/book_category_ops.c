/* 中文：在卡上先同步意图日志，再改路径；NVS 失败后保留完整文件和可重试日志。
 * English: Sync an intent journal on the card before renaming; NVS failures retain intact files and a retryable journal. */
#include "book_category_ops.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define CAP BOOK_STORE_PATH_MAX
#define MAX_NODES 1000
#define MAX_DEPTH 12
#define JOURNAL ".kimiko-category-op"
typedef struct { uint32_t magic, tree, size, checksum; char old[CAP], next[CAP]; } intent_t;
static void *buffer(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}
static bool join(const char *dir, const char *name, char out[CAP]) {
    int n = snprintf(out, CAP, "%s/%s", dir, name);
    return n > 0 && n < CAP;
}
static bool below(const char *root, const char *path) {
    if (!root || !path || root[0] != '/' || strnlen(root, CAP) >= CAP || strnlen(path, CAP) >= CAP) return false;
    size_t n = strlen(root);
    if (!n || root[n - 1] == '/' || strncmp(root, path, n) || path[n] != '/' || !path[n + 1]) return false;
    const char *p = path + n + 1;
    while (*p) {
        const char *end = strchr(p, '/'); size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len || (len == 1 && *p == '.') || (len == 2 && !memcmp(p, "..", 2))) return false;
        p += len; if (*p) ++p;
        if (end && !*p) return false;
    }
    return true;
}
static bool category(const char *root, const char *path) {
    return below(root, path) && !strchr(path + strlen(root) + 1, '/') && path[strlen(root) + 1] != '.';
}
static bool book_file(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".epub") || !strcasecmp(dot, ".mobi"));
}
static int inspect(const char *path, struct stat *st) {
#ifdef ESP_PLATFORM
    return stat(path,st);
#else
    return lstat(path,st);
#endif
}
bool book_category_name_valid(const char *name) {
    if (!name) return false;
    size_t n = strnlen(name, BOOK_CATEGORY_NAME_MAX + 1);
    if (!n || n > BOOK_CATEGORY_NAME_MAX || name[0] == '.' || name[0] == ' ' ||
        name[n - 1] == '.' || name[n - 1] == ' ' || !strcmp(name, "全部书籍") || !strcmp(name, "未分类")) return false;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)name[i++]; unsigned more; uint32_t cp;
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) return false;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; more = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; more = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; more = 3; }
        else return false;
        unsigned width = more;
        while (more--) {
            if (i == n || ((unsigned char)name[i] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | ((unsigned char)name[i++] & 63);
        }
        if ((width == 2 && cp < 0x800) || (width == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    }
    return true;
}
static esp_err_t vacant(const char *path) {
    struct stat st;
    if (!stat(path, &st)) return ESP_ERR_INVALID_STATE;
    return errno == ENOENT ? ESP_OK : ESP_FAIL;
}
bool book_category_pending(const char *root) {
    char path[CAP]; struct stat st;
    if (!root || !join(root, JOURNAL, path)) return true;
    return !stat(path, &st) || errno != ENOENT;
}
static uint32_t checksum(const intent_t *in) {
    intent_t copy = *in; copy.checksum = 0;
    const unsigned char *bytes = (const unsigned char *)&copy; uint32_t h = UINT32_C(2166136261);
    for (size_t i = 0; i < sizeof(copy); ++i) h = (h ^ bytes[i]) * UINT32_C(16777619);
    return h;
}
static esp_err_t save_intent(const char *root, const intent_t *in) {
    char path[CAP]; if (!join(root, JOURNAL, path)) return ESP_ERR_INVALID_SIZE;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return ESP_ERR_INVALID_STATE;
    bool ok = write(fd, in, sizeof(*in)) == sizeof(*in) && fsync(fd) == 0;
    if (close(fd)) ok = false;
    // 不完整日志也不静默删除；尚未重命名的源文件仍然完好。
    // Do not silently discard a torn journal; its not-yet-renamed source is still intact.
    return ok ? ESP_OK : ESP_FAIL;
}
static esp_err_t walk(const char *old, const char *next, unsigned depth, unsigned *nodes,
                       bool apply, book_path_state_extra_fn extra) {
    if (depth > MAX_DEPTH || ++*nodes > MAX_NODES) return ESP_ERR_INVALID_SIZE;
    const char *path = apply ? next : old;
    struct stat st;
    if (inspect(path, &st)) return ESP_FAIL;
    if (S_ISREG(st.st_mode)) {
        if (!book_file(path)) return ESP_OK;
        if (st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) return ESP_ERR_INVALID_SIZE;
        return apply ? book_path_state_relocate(old, next, (uint32_t)st.st_size, extra) : book_path_state_prepare(old, next);
    }
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    DIR *dir = opendir(path); if (!dir) return ESP_FAIL;
    char *paths = buffer(2 * CAP);
    if (!paths) { closedir(dir); return ESP_ERR_NO_MEM; }
    esp_err_t err = ESP_OK; struct dirent *entry;
    for (;;) {
        errno = 0; entry = readdir(dir);
        if (!entry) { if (errno) err = ESP_FAIL; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!join(old, entry->d_name, paths) || !join(next, entry->d_name, paths + CAP)) { err = ESP_ERR_INVALID_SIZE; break; }
        err = walk(paths, paths + CAP, depth + 1, nodes, apply, extra);
        if (err != ESP_OK) break;
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
    free(paths); if (closedir(dir)) err = ESP_FAIL;
    return err;
}
static esp_err_t finish_intent(const char *root, intent_t *in, book_path_state_extra_fn extra) {
    struct stat a, b;
    int from = stat(in->old, &a), from_errno = errno;
    int to = stat(in->next, &b), to_errno = errno;
    if (from == 0 && to < 0 && to_errno == ENOENT) {
        if ((in->tree ? !S_ISDIR(a.st_mode) : (!S_ISREG(a.st_mode) || a.st_size != in->size)) ||
            rename(in->old, in->next)) return ESP_FAIL;
    } else if (!(from < 0 && from_errno == ENOENT && to == 0)) return ESP_ERR_INVALID_STATE;
    if (stat(in->next, &b) || (in->tree ? !S_ISDIR(b.st_mode) : (!S_ISREG(b.st_mode) || b.st_size != in->size))) return ESP_ERR_INVALID_STATE;
    unsigned nodes = 0;
    esp_err_t err = walk(in->old, in->next, 0, &nodes, true, extra);
    if (err != ESP_OK) return err;
    char path[CAP]; if (!join(root, JOURNAL, path)) return ESP_ERR_INVALID_SIZE;
    return unlink(path) ? ESP_FAIL : ESP_OK;
}
esp_err_t book_category_recover(const char *root, book_path_state_extra_fn extra) {
    char path[CAP]; if (!root || !join(root, JOURNAL, path)) return ESP_ERR_INVALID_ARG;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    intent_t *in = buffer(sizeof(*in));
    if (!in) { fclose(file); return ESP_ERR_NO_MEM; }
    bool ok = fread(in, 1, sizeof(*in), file) == sizeof(*in) && fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) ok = false;
    ok = ok && in->magic == UINT32_C(0x4d434f31) && in->tree <= 1 && in->checksum == checksum(in) &&
        memchr(in->old, 0, CAP) && memchr(in->next, 0, CAP) && below(root, in->old) && below(root, in->next) &&
        strcmp(in->old, in->next) && (in->tree ? (category(root, in->old) && category(root, in->next)) : (book_file(in->old) && book_file(in->next)));
    esp_err_t err = ok ? finish_intent(root, in, extra) : ESP_ERR_INVALID_STATE;
    free(in); return err;
}
static esp_err_t transact(const char *root, const char *old, const char *next, bool tree, book_path_state_extra_fn extra) {
    esp_err_t err = book_category_recover(root, extra);
    if (err != ESP_OK) return err;
    err = vacant(next); if (err != ESP_OK) return err;
    unsigned nodes = 0;
    err = walk(old, next, 0, &nodes, false, extra); if (err != ESP_OK) return err;
    struct stat st; if (stat(old, &st)) return ESP_ERR_NOT_FOUND;
    intent_t *in = buffer(sizeof(*in)); if (!in) return ESP_ERR_NO_MEM;
    memset(in, 0, sizeof(*in)); in->magic = UINT32_C(0x4d434f31); in->tree = tree;
    in->size = tree ? 0 : (uint32_t)st.st_size; strcpy(in->old, old); strcpy(in->next, next); in->checksum = checksum(in);
    err = save_intent(root, in);
    if (err == ESP_OK) err = finish_intent(root, in, extra);
    free(in); return err;
}
esp_err_t book_category_create(const char *root, const char *name, char out[CAP]) {
    if (!root || !out || !book_category_name_valid(name) || !join(root, name, out) || !category(root, out)) return ESP_ERR_INVALID_ARG;
    char journal[CAP]; if (!join(root, JOURNAL, journal)) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = vacant(journal); if (err != ESP_OK) return err;
    err = vacant(out); if (err != ESP_OK) return err;
    return mkdir(out, 0775) ? ESP_FAIL : ESP_OK;
}
esp_err_t book_category_rename(const char *root, const char *source, const char *name, book_path_state_extra_fn extra) {
    char next[CAP]; struct stat st;
    if (!category(root, source) || !book_category_name_valid(name) || !join(root, name, next)) return ESP_ERR_INVALID_ARG;
    if (inspect(source, &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_NOT_FOUND;
    if (!strcmp(source, next)) return book_category_pending(root) ? ESP_ERR_INVALID_STATE : ESP_OK;
    return transact(root, source, next, true, extra);
}
static esp_err_t move_target(const char *root, const char *source, const char *destination, char next[CAP]) {
    struct stat st;
    if (!root || !source || !destination) return ESP_ERR_INVALID_ARG;
    if (!below(root, source) || !book_file(source) || (!category(root, destination) && strcmp(root, destination))) return ESP_ERR_INVALID_ARG;
    if (inspect(source, &st) || !S_ISREG(st.st_mode)) return ESP_ERR_NOT_FOUND;
    if (inspect(destination, &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_NOT_FOUND;
    const char *base = strrchr(source, '/') + 1;
    if (!join(destination, base, next)) return ESP_ERR_INVALID_SIZE;
    if (!strcmp(source, next)) return ESP_OK;
    return vacant(next);
}
esp_err_t book_category_move(const char *root, const char *source, const char *destination, book_path_state_extra_fn extra) {
    char next[CAP]; esp_err_t err = move_target(root, source, destination, next);
    if (err != ESP_OK || !strcmp(source, next)) return err;
    return transact(root, source, next, false, extra);
}
esp_err_t book_category_plan_move(const char *root, const char *source, const char *destination, char next[CAP]) {
    if (!root || !source || !destination || !next) return ESP_ERR_INVALID_ARG;
    esp_err_t err = move_target(root, source, destination, next);
    if (err == ESP_OK && strcmp(source, next)) err = book_path_state_prepare(source, next);
    return err;
}
typedef struct move_item { struct move_item *next; char path[CAP]; } move_item_t;
esp_err_t book_category_remove(const char *root, const char *source, const char *destination,
                               book_category_result_t *result, book_path_state_extra_fn extra) {
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (book_category_result_t){0};
    if (!root || !source || !destination || !category(root, source) || !strcmp(source, destination)) return ESP_ERR_INVALID_ARG;
    if (strcmp(root, destination) && !category(root, destination)) return ESP_ERR_INVALID_ARG;
    struct stat target;
    if (inspect(destination, &target) || !S_ISDIR(target.st_mode)) return ESP_ERR_NOT_FOUND;
    esp_err_t err = book_category_recover(root, extra); if (err != ESP_OK) return err;
    DIR *dir = opendir(source); if (!dir) return ESP_ERR_NOT_FOUND;
    move_item_t *head = NULL, **tail = &head; struct dirent *entry;
    for (;;) {
        errno = 0; entry = readdir(dir); if (!entry) { if (errno) err = ESP_FAIL; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (++result->total > MAX_NODES) { err = ESP_ERR_INVALID_SIZE; break; }
        move_item_t *item = buffer(sizeof(*item));
        if (!item) { err = ESP_ERR_NO_MEM; break; }
        item->next = NULL; *tail = item; tail = &item->next;
        if (!join(source, entry->d_name, item->path)) { err = ESP_ERR_INVALID_SIZE; break; }
        struct stat st;
        if (inspect(item->path, &st)) { err = ESP_FAIL; break; }
        if (!S_ISREG(st.st_mode) || !book_file(item->path) || entry->d_name[0] == '.') { err = ESP_ERR_NOT_SUPPORTED; break; }
        char next[CAP]; err = move_target(root, item->path, destination, next);
        if (err == ESP_OK) err = book_path_state_prepare(item->path, next);
        if (err != ESP_OK) break;
    }
    if (closedir(dir)) err = ESP_FAIL;
    if (err == ESP_OK) {
        for (move_item_t *item = head; item; item = item->next) {
            err = book_category_move(root, item->path, destination, extra);
            if (err != ESP_OK) break;
            ++result->moved;
        }
        // rmdir 只接受真正空目录；它不是递归删除或“成功一部分就清空”。
        // rmdir accepts a genuinely empty directory, never recursive or partial-success deletion.
        if (err == ESP_OK) { if (rmdir(source)) err = ESP_FAIL; else result->removed = true; }
    }
    while (head) { move_item_t *next = head->next; free(head); head = next; }
    return err;
}
