/* 中文：逻辑分类真实存储与故障注入回归，不连接设备。/ English: Real logical-category storage and fault-injection regressions, without hardware. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_collection.h"

static int allocation_fail = -1, write_fail, sync_fail, unlink_fail;
static int read_fail = -1;
static FILE *read_error_stream;
void *heap_caps_calloc(size_t n, size_t bytes, int caps) {
    assert(caps == 3);
    if (allocation_fail == 0) return NULL;
    if (allocation_fail > 0) --allocation_fail;
    return calloc(n, bytes);
}
void *heap_caps_realloc(void *p, size_t bytes, int caps) {
    assert(caps == 3);
    if (allocation_fail == 0) return NULL;
    if (allocation_fail > 0) --allocation_fail;
    return realloc(p, bytes);
}
static size_t test_write(const void *p, size_t n, size_t count, FILE *f) {
    if (write_fail) { --write_fail; return fwrite(p, n, count / 2, f); }
    return fwrite(p, n, count, f);
}
static int test_sync(int fd) { if (sync_fail) { --sync_fail; errno = EIO; return -1; } return fsync(fd); }
static int test_unlink(const char *p) { if (unlink_fail) { --unlink_fail; errno = EIO; return -1; } return unlink(p); }
static bool fail_read(FILE *f) {
    if (read_fail < 0) return false;
    if (read_fail-- > 0) return false;
    read_error_stream = f; errno = EIO; return true;
}
static size_t test_read(void *p, size_t n, size_t count, FILE *f) {
    return fail_read(f) ? 0 : fread(p, n, count, f);
}
static int test_getc(FILE *f) { return fail_read(f) ? EOF : fgetc(f); }
static int test_error(FILE *f) { return f == read_error_stream || ferror(f); }
#define fwrite test_write
#define fsync test_sync
#define unlink test_unlink
#define fread test_read
#define fgetc test_getc
#define ferror test_error
#include "../main/book/book_collection.c"
#undef fwrite
#undef fsync
#undef unlink
#undef fread
#undef fgetc
#undef ferror

static void corrupt(const char *prefix, int slot) {
    char path[320]; snprintf(path, sizeof(path), "%s.%d", prefix, slot);
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite("broken", 1, 6, f) == 6); assert(!fclose(f));
}
static void book(book_shelf_entry_t *b, const char *root, const char *leaf) {
    memset(b, 0, sizeof(*b)); snprintf(b->path, sizeof(b->path), "%s/%s", root, leaf); b->search_match = true;
}
int main(void) {
    char directory[] = "/tmp/kimiko-collections-XXXXXX"; assert(mkdtemp(directory));
    char root[288], prefix[288], second[288], folder[288];
    snprintf(root, sizeof(root), "%s/books", directory); assert(!mkdir(root, 0700));
    snprintf(prefix, sizeof(prefix), "%s/library-card-a", directory);
    snprintf(second, sizeof(second), "%s/library-card-b", directory);
    snprintf(folder, sizeof(folder), "%s/文学", root); assert(!mkdir(folder, 0700));
    const char *folders[] = {folder};
    book_shelf_entry_t books[4]; book(&books[0], root, "文学/same.txt"); book(&books[1], root, "same.txt");
    book(&books[2], "/flash", "builtin.epub"); books[2].is_flash = true;
    FILE *original = fopen(books[0].path, "wb"); assert(original); fputs("original book\n", original); assert(!fclose(original));
    book_collection_t s = {0}; assert(book_collection_open(&s, prefix) == ESP_OK);
    assert(!book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, folders, 1, books, 3, false) == ESP_OK && !book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK);
    assert(book_collection_count(&s) == 1 && books[0].category_id && !books[1].category_id && !books[2].category_id);
    uint32_t literature = books[0].category_id, id = 0; unsigned changed = 99;
    uint32_t generation = s.generation;
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && s.generation == generation);
    books[0].selected = books[1].selected = books[2].selected = true;
    assert(book_collection_create(&s, " 科幻 ", books, 3, true, &id, &changed) == ESP_OK && changed == 3);
    assert(strcmp(book_collection_find(&s, id)->name, "科幻") == 0);
    assert(book_collection_assignment(&s, books[0].path) == id && book_collection_assignment(&s, books[1].path) == id);
    assert(book_collection_assignment(&s, books[2].path) == id);
    assert(book_collection_can_undo(&s));
    generation = s.generation;
    assert(book_collection_assign(&s, books, 3, true, id, &changed) == ESP_OK && !changed && s.generation == generation);
    assert(book_collection_rename(&s, id, "科幻小说") == ESP_OK);
    assert(book_collection_order(&s, id, -1) == ESP_OK && book_collection_at(&s, 0)->id == id);
    assert(book_collection_remove(&s, id) == ESP_OK && !book_collection_assignment(&s, books[0].path));
    assert(book_collection_undo(&s) == ESP_OK && book_collection_assignment(&s, books[0].path) == id);
    assert(book_collection_at(&s, 0)->id == id && !book_collection_can_undo(&s));
    book_collection_close(&s); assert(book_collection_open(&s, prefix) == ESP_OK);
    assert(book_collection_assignment(&s, books[0].path) == id);
    // 任一副本短暂读错都阻断写入，不回退旧数据再覆盖新副本。/ Transient errors on either copy block writes instead of rolling back and overwriting newer data.
    generation = s.generation;
    for (int fail = 0; fail < 6; ++fail) {
        read_fail = fail;
        assert(book_collection_open(&s, prefix) == ESP_FAIL && s.blocked && !s.ready);
        read_fail = -1; read_error_stream = NULL;
        assert(book_collection_open(&s, prefix) == ESP_OK && s.generation == generation);
        assert(book_collection_assignment(&s, books[0].path) == id);
    }
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && books[0].category_id == id);
    assert(book_collection_remove(&s, literature) == ESP_OK);
    assert(book_collection_sync(&s, root, folders, 1, books, 3, true) == ESP_OK && book_collection_count(&s) == 1);
    book(&books[3], root, "文学/new.epub");
    assert(book_collection_sync(&s, root, folders, 1, books, 4, true) == ESP_OK && books[3].new_book && !books[3].category_id);
    books[3].selected = true;
    generation = s.generation; write_fail = 1;
    assert(book_collection_assign(&s, books, 4, true, 0, &changed) != ESP_OK && !changed && s.generation == generation);
    assert(books[0].selected && book_collection_assignment(&s, books[0].path) == id);
    book_collection_close(&s); assert(book_collection_open(&s, prefix) == ESP_OK && book_collection_assignment(&s, books[0].path) == id);
    sync_fail = 1;
    assert(book_collection_assign(&s, books, 4, true, 0, &changed) != ESP_OK && book_collection_assignment(&s, books[0].path) == id);
    allocation_fail = 0;
    assert(book_collection_rename(&s, id, "另一名称") == ESP_ERR_NO_MEM);
    allocation_fail = -1;
    assert(!strcmp(book_collection_find(&s, id)->name, "科幻小说"));
    for (int fail = 0; fail < 7; ++fail) {
        allocation_fail = fail;
        esp_err_t result = book_collection_assign(&s, books, 4, true, 0, &changed);
        allocation_fail = -1;
        if (result == ESP_OK) assert(book_collection_undo(&s) == ESP_OK);
        else assert(!changed);
        assert(book_collection_assignment(&s, books[0].path) == id && books[0].selected);
    }
    FILE *backup = tmpfile(); assert(backup); assert(book_collection_export(&s, backup) == ESP_OK); rewind(backup);
    book_collection_t other = {0}; assert(book_collection_open(&other, second) == ESP_OK && !book_collection_count(&other));
    assert(book_collection_import(&other, backup) == ESP_OK && book_collection_assignment(&other, books[1].path) == id);
    assert(!fclose(backup)); book_collection_close(&other);
    assert(book_collection_open(&other, second) == ESP_OK && book_collection_assignment(&other, books[1].path) == id);
    assert(book_collection_assign(&other, books, 4, true, 0, &changed) == ESP_OK);
    assert(book_collection_assignment(&s, books[1].path) == id); book_collection_close(&other);
    assert(book_collection_rename(&s, id, "新名称") == ESP_OK);
    int latest = s.slot; book_collection_close(&s); corrupt(prefix, latest);
    assert(book_collection_open(&s, prefix) == ESP_OK && s.recovered && !strcmp(book_collection_find(&s, id)->name, "科幻小说"));
    book_collection_close(&s); corrupt(prefix, 1-latest);
    assert(book_collection_open(&s, prefix) != ESP_OK && s.blocked && !s.ready);
    assert(book_collection_create(&s, "不覆盖坏索引", NULL, 0, false, &id, &changed) != ESP_OK);
    book_collection_close(&s);
    char clean[97];
    assert(book_collection_name("  中文 Name  ", clean) && !strcmp(clean, "中文 Name"));
    assert(!book_collection_name("全部书籍", clean) && !book_collection_name("未分类", clean));
    assert(!book_collection_name("a/b", clean) && !book_collection_name("a\n", clean));
    assert(!book_collection_name("\xc0\x80", clean) && !book_collection_name("\xed\xa0\x80", clean));
    assert(book_collection_name("一二三四五六七八九十一二三四五六七八九十一二三四", clean));
    assert(!book_collection_name("一二三四五六七八九十一二三四五六七八九十一二三四五", clean));
    // 旧目录超长、保留名或修剪后重名仍独立迁移，不因截断合并。/ Long/reserved/trim-colliding legacy folders migrate independently without truncation merges.
    char migration_prefix[288], legacy_paths[6][288];
    snprintf(migration_prefix, sizeof(migration_prefix), "%s/legacy-alias", directory);
    const char *names[] = {"一二三四五六七八九十一二三四五六七八九十一二三四五", "一二三四五六七八九十一二三四五六七八九十一二三四六",
                           "未分类", "一二三四五六七八九十一二三四五六七八 1", "文学", " 文学 "};
    const char *legacy_folders[6]; book_shelf_entry_t legacy_books[6];
    for (int i = 0; i < 6; ++i) {
        snprintf(legacy_paths[i], sizeof(legacy_paths[i]), "%s/%s", root, names[i]);
        legacy_folders[i] = legacy_paths[i]; char leaf[288];
        snprintf(leaf, sizeof(leaf), "%s/book.txt", names[i]); book(&legacy_books[i], root, leaf);
    }
    assert(book_collection_open(&s, migration_prefix) == ESP_OK);
    write_fail = 1;
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) != ESP_OK && !book_collection_migrated(&s));
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) == ESP_OK && book_collection_count(&s) == 6);
    for (int i = 0; i < 6; ++i) {
        assert(legacy_books[i].category_id && book_collection_name(book_collection_find(&s, legacy_books[i].category_id)->name, clean));
        for (int j = 0; j < i; ++j) assert(legacy_books[i].category_id != legacy_books[j].category_id);
    }
    assert(!strcmp(book_collection_find(&s, legacy_books[3].category_id)->name, names[3]));
    assert(!strcmp(book_collection_find(&s, legacy_books[4].category_id)->name, names[4]));
    book_collection_close(&s); assert(book_collection_open(&s, migration_prefix) == ESP_OK);
    generation = s.generation;
    assert(book_collection_sync(&s, root, legacy_folders, 6, legacy_books, 6, true) == ESP_OK && s.generation == generation);
    book_collection_close(&s);
    original = fopen(books[0].path, "rb"); assert(original); char content[32]; assert(fgets(content, sizeof(content), original));
    assert(!strcmp(content, "original book\n") && !fclose(original));
    assert(!unlink(books[0].path) && !rmdir(folder) && !rmdir(root));
    for (int slot = 0; slot < 2; ++slot) { char path[320]; snprintf(path, sizeof(path), "%s.%d", prefix, slot); unlink(path); snprintf(path, sizeof(path), "%s.%d", second, slot); unlink(path); snprintf(path, sizeof(path), "%s.%d", migration_prefix, slot); unlink(path); }
    assert(!rmdir(directory));
    puts("logical collections: PASS (migration, stable IDs, same filenames, internal books, persistence, no file mutations, batch rollback, undo, card isolation, corrupt copies, UTF-8 and allocation/write failures)");
}
