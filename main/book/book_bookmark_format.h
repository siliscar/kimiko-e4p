/* 中文：阅读书签的持久化布局，供阅读页和路径迁移共用。
 * English: Persisted bookmark layouts shared by the reader and path relocation. */
#pragma once
#include <stdint.h>
#include "book_store.h"

#define BOOKMARK_MAX 24
typedef struct {
    uint32_t magic, file_size, byte_off;
    uint16_t chapter;
    char path[BOOK_STORE_PATH_MAX];
} legacy_reader_bookmark_t;
typedef struct {
    uint16_t chapter, reserved;
    uint32_t byte_off, saved_s;
} reader_bookmark_entry_t;
typedef struct {
    uint32_t magic, file_size;
    uint16_t count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[BOOKMARK_MAX];
} reader_bookmarks_t;
