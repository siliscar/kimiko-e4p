#!/usr/bin/env python3
"""中文：在临时目录验证真实 TXT 索引缓存的升级、命中与损坏恢复。
English: Verify actual TXT cache upgrades, hits and corruption recovery in a temporary directory.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include "book_source_internal.h"
#include "book_index_cache.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char cache_path[512];
int cache_test_mkdir(const char *path, mode_t mode) {
    (void)mode;
    assert(!strcmp(path, "/sdcard/.readpico") || !strcmp(path, "/sdcard/.readpico/index"));
    return 0;
}
// 仅将缓存位置改到临时目录；真实缓存头校验、读写与解析器参与测试。
// Relocate caches only; use the actual header checks, file operations and parser.
bool book_index_cache_prepare(const char *source, const char *tag, uint32_t version,
                              char *path, size_t cap, book_index_cache_header_t *header) {
    struct stat st; assert(!stat(source, &st)); assert(!strcmp(tag, "txt"));
    *header = (book_index_cache_header_t){.magic=1234, .version=version,
        .source_size=(uint32_t)st.st_size, .modified=st.st_mtime, .path_hash=42};
    int n = snprintf(path, cap, "%s", cache_path);
    return n > 0 && (size_t)n < cap;
}
static void close_book(book_txt_t *b) {
    fclose(b->file); free(b->entries); *b = (book_txt_t){0};
}
static void open_book(book_txt_t *b, const char *source) {
    assert(book_txt_open(b, source) == ESP_OK && b->count == 2);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char source[512]; snprintf(source, sizeof(source), "%s/source.txt", argv[1]);
    snprintf(cache_path, sizeof(cache_path), "%s/index.bin", argv[1]);
    FILE *f = fopen(source, "wb"); assert(f);
    fprintf(f, "第1章 ");
    for (unsigned i=0; i<60; ++i) fprintf(f, "中");
    fprintf(f, "\n正文\n　　第2章 结束\n正文"); assert(!fclose(f));
    char path[512]; book_index_cache_header_t header;
    assert(book_index_cache_prepare(source, "txt", 1, path, sizeof(path), &header));
    // 构造完整旧版 Part 缓存，升级必须重扫，不能继续返回旧标题。
    // A complete v1 Part cache must be rebuilt rather than returning old titles.
    f = fopen(path, "wb"); assert(f);
    assert(fwrite(&header, sizeof(header), 1, f) == 1);
    uint32_t payload[4] = {header.source_size, 0, 1, 0};
    assert(fwrite(payload, sizeof(payload), 1, f) == 1);
    struct {uint32_t offset; char title[41];} old = {.title="Part 1"};
    assert(fwrite(&old, sizeof(old), 1, f) == 1); assert(!fclose(f));
    book_txt_t book = {0}; open_book(&book, source);
    assert(!strncmp(book.entries[0].title, "第1章 ", strlen("第1章 ")));
    assert(strlen(book.entries[0].title) == strlen("第1章 ") + 39 * 3);
    assert(!strcmp(book.entries[1].title, "第2章 结束")); close_book(&book);
    f = fopen(path, "r+b"); assert(f);
    assert(fread(&header, sizeof(header), 1, f) == 1 && header.version == 2);
    assert(!fseek(f, (long)sizeof(header) + 16, SEEK_SET));
    book_entry_t entry = {.title="cached heading"};
    assert(fwrite(&entry, sizeof(entry), 1, f) == 1); assert(!fclose(f));
    open_book(&book, source);
    assert(!strcmp(book.entries[0].title, "cached heading")); close_book(&book);
    f = fopen(path, "r+b"); assert(f);
    assert(!fseek(f, (long)sizeof(header) + 16 + sizeof(uint32_t), SEEK_SET));
    char invalid[sizeof(entry.title)]; memset(invalid, 'x', sizeof(invalid));
    assert(fwrite(invalid, sizeof(invalid), 1, f) == 1); assert(!fclose(f));
    open_book(&book, source);
    assert(!strncmp(book.entries[0].title, "第1章 ", strlen("第1章 "))); close_book(&book);
    f = fopen(path, "wb"); assert(f); assert(!fclose(f));
    open_book(&book, source); close_book(&book);
    puts("TXT cache: v1 rebuild, v2 hit, UTF-8 truncation and corruption recovery passed");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="txt-cache-") as directory:
        folder = Path(directory)
        source = folder / "test.c"
        source.write_text(HARNESS)
        flags = ["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror",
                 "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 "-Itools/book_epub_stubs", "-Itools/book_layout_stubs", "-Imain/book", "-Imain"]
        obj, binary = folder / "cache.o", folder / "test"
        subprocess.run(flags + ["-Dbook_index_cache_prepare=unused_cache_prepare",
                               "-Dmkdir=cache_test_mkdir", "-c",
                               "main/book/book_index_cache.c", "-o", str(obj)], cwd=ROOT, check=True)
        subprocess.run(flags + [str(source), "main/book/book_txt.c", "main/book/gbk.c",
                               str(obj), "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary), str(folder)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
