/* 中文：保留进度原始序号、书签、收藏、书名和隐藏状态；先复制、后清理。
 * English: Preserve raw progress order, bookmarks, favorites, titles and hidden state; copy before cleanup. */
#include "book_path_state.h"
#include "book_bookmark_format.h"
#include "book_progress.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#define CAP BOOK_STORE_PATH_MAX
#define RECORD_CAP 640
typedef enum { PROGRESS, MARKS, FAVORITE, TITLE, HIDDEN, KINDS } kind_t;
static const char *const namespaces[KINDS] = {"rp_books", "rp_marks", "rp_favs", "rp_titles", "rp_shelf"};
static const char prefixes[KINDS] = {'b', 'm', 'f', 't', 'h'};
static uint32_t hash_path(const char *s) {
    uint32_t h = UINT32_C(2166136261);
    while (*s) h = (h ^ (unsigned char)*s++) * UINT32_C(16777619);
    return h;
}
static void key_for(kind_t kind, const char *path, char key[11]) {
    snprintf(key, 11, "%c_%08lx", prefixes[kind], (unsigned long)hash_path(path));
}
static bool paths_ok(const char *a, const char *b) {
    return a && b && *a && *b && strnlen(a, CAP) < CAP && strnlen(b, CAP) < CAP &&
        strcmp(a, b) && hash_path(a) != hash_path(b);
}
static bool owned(const char *stored, size_t cap, const char *path) {
    return strnlen(stored, cap) < cap && !strcmp(stored, path);
}
static esp_err_t read_record(nvs_handle_t h, kind_t kind, const char *path, uint8_t *data, size_t *len) {
    char key[11]; key_for(kind, path, key); *len = RECORD_CAP;
    esp_err_t err;
    if (kind == FAVORITE) { *len = 1; err = nvs_get_u8(h, key, data); }
    else if (kind == HIDDEN) err = nvs_get_str(h, key, (char *)data, len);
    else err = nvs_get_blob(h, key, data, len);
    if (err != ESP_OK) return err;
    bool valid = false;
    if (kind == FAVORITE) valid = data[0] == 1;
    else if (kind == HIDDEN) valid = *len == strlen(path) + 1 && owned((char *)data, *len, path);
    else if (kind == TITLE) {
        size_t p = strlen(path) + 1;
        valid = *len > p && *len <= p + 121 && owned((char *)data, *len, path) &&
            strnlen((char *)data + p, *len - p) == *len - p - 1;
    } else if (kind == PROGRESS) {
        size_t n = *len >= 24 ? *len - 24 : 0;
        valid = n > 1 && n <= CAP && !memcmp(data, "RPB", 3) && (data[3] == 1 || data[3] == 2) &&
            (data[3] != 1 || (n <= 160 && data[23] == 0)) &&
            ((size_t)data[22] | ((size_t)data[23] << 8)) == n &&
            strnlen((char *)data + 24, n) == n - 1 && owned((char *)data + 24, n, path);
    } else if (kind == MARKS) {
        if (*len == sizeof(reader_bookmarks_t)) {
            reader_bookmarks_t marks; memcpy(&marks, data, sizeof(marks));
            valid = marks.magic == UINT32_C(0x52504d32) && marks.count <= BOOKMARK_MAX && owned(marks.path, CAP, path);
        } else if (*len == sizeof(legacy_reader_bookmark_t)) {
            legacy_reader_bookmark_t marks; memcpy(&marks, data, sizeof(marks));
            valid = marks.magic == UINT32_C(0x52504d31) && owned(marks.path, CAP, path);
        }
    }
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static void rebase(kind_t kind, uint8_t *data, size_t *len, const char *old, const char *next) {
    size_t n = strlen(next) + 1;
    if (kind == PROGRESS) {
        // v1 的短路径限制不适用于新位置；升级外壳但保留零排序。
        // A v1 short-path limit need not constrain relocation; upgrade its envelope with zero order.
        if (data[3] == 1) { data[3] = 2; memset(data + 16, 0, 4); }
        data[22] = (uint8_t)n; data[23] = (uint8_t)(n >> 8);
        memcpy(data + 24, next, n); *len = 24 + n;
    } else if (kind == TITLE) {
        size_t p = strlen(old) + 1, tail = *len - p;
        memmove(data + n, data + p, tail); memcpy(data, next, n); *len = n + tail;
    } else if (kind == HIDDEN) { memcpy(data, next, n); *len = n; }
    else if (kind == MARKS) {
        size_t offset = *len == sizeof(reader_bookmarks_t) ? offsetof(reader_bookmarks_t, path) : offsetof(legacy_reader_bookmark_t, path);
        memset(data + offset, 0, CAP); memcpy(data + offset, next, n);
    }
}
static esp_err_t check_or_copy(kind_t kind, const char *old, const char *next, bool copy) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(namespaces[kind], copy ? NVS_READWRITE : NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND && !copy) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t from[RECORD_CAP], to[RECORD_CAP]; size_t a, b;
    esp_err_t src = read_record(h, kind, old, from, &a);
    esp_err_t dst = read_record(h, kind, next, to, &b);
    err = src == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : src;
    if (err == ESP_OK && !copy) err = dst == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && copy && src == ESP_OK) {
        rebase(kind, from, &a, old, next);
        if (dst == ESP_OK) err = a == b && !memcmp(from, to, a) ? ESP_OK : ESP_ERR_INVALID_STATE;
        else if (dst != ESP_ERR_NVS_NOT_FOUND) err = dst;
        else {
            char key[11]; key_for(kind, next, key);
            if (kind == FAVORITE) err = nvs_set_u8(h, key, from[0]);
            else if (kind == HIDDEN) err = nvs_set_str(h, key, (char *)from);
            else err = nvs_set_blob(h, key, from, a);
            if (err == ESP_OK) err = nvs_commit(h);
        }
    } else if (err == ESP_OK && copy && dst != ESP_OK && dst != ESP_ERR_NVS_NOT_FOUND) err = dst;
    nvs_close(h); return err;
}
esp_err_t book_path_state_prepare(const char *old, const char *next) {
    if (!paths_ok(old, next)) return ESP_ERR_INVALID_ARG;
    for (kind_t k = 0; k < KINDS; ++k) {
        esp_err_t err = check_or_copy(k, old, next, false);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
esp_err_t book_path_state_relocate(const char *old, const char *next, uint32_t size, book_path_state_extra_fn extra) {
    if (!paths_ok(old, next)) return ESP_ERR_INVALID_ARG;
    for (kind_t k = 0; k < KINDS; ++k) {
        esp_err_t err = check_or_copy(k, old, next, true);
        if (err != ESP_OK) return err;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("rp_books", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    char last[CAP]; size_t len = sizeof(last);
    err = nvs_get_str(h, "last", last, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else if (err == ESP_OK && !strcmp(last, old)) {
        err = nvs_set_str(h, "last", next);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK && extra) err = extra(old, next, size);
    if (err != ESP_OK) return err;
    // 全部副本及附属记录可靠后再清旧键；任一步失败都由卡上日志重试。
    // Clear old keys only after every copy and extra record is durable; the card journal retries failures.
    for (kind_t k = MARKS; k < KINDS; ++k) {
        err = nvs_open(namespaces[k], NVS_READWRITE, &h);
        if (err != ESP_OK) return err;
        uint8_t data[RECORD_CAP]; size_t n;
        err = read_record(h, k, old, data, &n);
        if (err == ESP_OK) {
            char key[11]; key_for(k, old, key); err = nvs_erase_key(h, key);
            if (err == ESP_OK) err = nvs_commit(h);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        nvs_close(h);
        if (err != ESP_OK) return err;
    }
    return book_progress_forget(old);
}
