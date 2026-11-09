/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从字体引擎提取文件块缓存，保留容量、淘汰、合并读取和计时规则。
 * English: File block caching extracted from the font engine, preserving capacity, eviction, read coalescing and timing rules.
 */
#include "ttf_block_cache.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define io_data (cache->f_io_data)
#define io_run_buf (cache->f_io_run_buf)
#define io_base (cache->f_io_base)
#define io_fill (cache->f_io_fill)
#define io_age (cache->f_io_age)
#define io_clock (cache->f_io_clock)
#define touch_blocks (cache->f_touch_blocks)
#define touch_n (cache->f_touch_n)

void ttf_block_cache_reset(ttf_block_cache_t *cache) {
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        io_base[i] = UINT32_MAX;
        io_fill[i] = 0;
        io_age[i] = 0;
    }
    io_clock = 0;
    touch_n = 0;
}

bool ttf_block_cache_ensure(ttf_block_cache_t *cache) {
    if (io_data == NULL) {
        io_data = heap_caps_malloc(
            (size_t)TTF_IO_SLOTS * TTF_IO_BLOCK,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (io_data == NULL) return false;
        ttf_block_cache_reset(cache);
    }
    if (io_run_buf == NULL) {
        io_run_buf = heap_caps_malloc(
            (size_t)TTF_IO_RUN_MAX * TTF_IO_BLOCK,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }
    return true;
}

static int io_find(ttf_block_cache_t *cache, uint32_t base) {
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        if (io_base[i] == base) {
            io_age[i] = ++io_clock;
            return i;
        }
    }
    return -1;
}

static int io_victim(ttf_block_cache_t *cache) {
    int empty = -1;
    int best = 0;
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        if (io_base[i] == UINT32_MAX) {
            empty = i;
            break;
        }
        if (io_age[i] < io_age[best]) best = i;
    }
    return empty >= 0 ? empty : best;
}

static bool io_load_block(ttf_block_cache_t *cache, const ttf_block_source_t *source, uint32_t base) {
    if (io_find(cache, base) >= 0) return true;
    if (io_data == NULL && !ttf_block_cache_ensure(cache)) return false;
    int slot = io_victim(cache);
    uint8_t* dst = io_data + (size_t)slot * TTF_IO_BLOCK;
    int64_t t0 = source->read_us != NULL ? esp_timer_get_time() : 0;
    if (lseek(source->fd, (off_t)base, SEEK_SET) < 0) {
        *source->file_pos = UINT32_MAX;
        if (source->read_us != NULL) *source->read_us += esp_timer_get_time() - t0;
        return false;
    }
    ssize_t got = read(source->fd, dst, TTF_IO_BLOCK);
    if (source->read_us != NULL) *source->read_us += esp_timer_get_time() - t0;
    if (got <= 0) {
        *source->file_pos = UINT32_MAX;
        return false;
    }
    *source->file_pos = base + (uint32_t)got;
    io_base[slot] = base;
    io_fill[slot] = (uint16_t)got;
    io_age[slot] = ++io_clock;
    return true;
}

bool ttf_block_cache_read(ttf_block_cache_t *cache, const ttf_block_source_t *source, uint32_t offset, void* dst, size_t n) {
    if (n == 0) return true;
    if (io_data == NULL && !ttf_block_cache_ensure(cache)) return source->read_raw(offset, dst, n);

    uint8_t* out = dst;
    uint32_t pos = offset;
    size_t left = n;
    while (left > 0) {
        uint32_t base = pos & ~(uint32_t)(TTF_IO_BLOCK - 1);
        uint32_t skip = pos - base;
        if (!io_load_block(cache, source, base)) return false;
        int slot = io_find(cache, base);
        if (slot < 0 || skip >= io_fill[slot]) return false;
        size_t take = (size_t)io_fill[slot] - skip;
        if (take > left) take = left;
        memcpy(out, io_data + (size_t)slot * TTF_IO_BLOCK + skip, take);
        out += take;
        pos += (uint32_t)take;
        left -= take;
    }
    return true;
}

void ttf_block_cache_touch(ttf_block_cache_t *cache, uint32_t off, uint32_t len) {
    if (len == 0) return;
    uint32_t a = off & ~(uint32_t)(TTF_IO_BLOCK - 1);
    uint32_t end = off + len;
    while (a < end && touch_n < TTF_PREFETCH_MAX) {
        touch_blocks[touch_n++] = a;
        a += TTF_IO_BLOCK;
    }
}

static int u32_cmp(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a;
    uint32_t y = *(const uint32_t*)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static bool io_load_run(ttf_block_cache_t *cache, const ttf_block_source_t *source, uint32_t base, int blocks) {
    if (blocks <= 1) return io_load_block(cache, source, base);
    if (blocks > TTF_IO_RUN_MAX) blocks = TTF_IO_RUN_MAX;
    if (io_run_buf == NULL
        || !source->read_raw(base, io_run_buf, (size_t)blocks * TTF_IO_BLOCK)) {
        bool ok = true;
        for (int i = 0; i < blocks; i++) {
            if (!io_load_block(cache, source, base + (uint32_t)i * TTF_IO_BLOCK)) ok = false;
        }
        return ok;
    }
    for (int i = 0; i < blocks; i++) {
        uint32_t b = base + (uint32_t)i * TTF_IO_BLOCK;
        if (io_find(cache, b) >= 0) continue;
        int slot = io_victim(cache);
        memcpy(
            io_data + (size_t)slot * TTF_IO_BLOCK,
            io_run_buf + (size_t)i * TTF_IO_BLOCK,
            TTF_IO_BLOCK
        );
        io_base[slot] = b;
        io_fill[slot] = TTF_IO_BLOCK;
        io_age[slot] = ++io_clock;
    }
    return true;
}

void ttf_block_cache_flush(ttf_block_cache_t *cache, const ttf_block_source_t *source) {
    if (touch_n <= 0) return;
    if (io_data == NULL && !ttf_block_cache_ensure(cache)) {
        touch_n = 0;
        return;
    }
    qsort(touch_blocks, (size_t)touch_n, sizeof(touch_blocks[0]), u32_cmp);
    int w = 1;
    for (int i = 1; i < touch_n; i++) {
        if (touch_blocks[i] != touch_blocks[w - 1]) {
            touch_blocks[w++] = touch_blocks[i];
        }
    }
    int i = 0;
    while (i < w) {
        int j = i + 1;
        while (j < w && j - i < TTF_IO_RUN_MAX
               && touch_blocks[j] == touch_blocks[j - 1] + TTF_IO_BLOCK) {
            j++;
        }
        io_load_run(cache, source, touch_blocks[i], j - i);
        i = j;
    }
    touch_n = 0;
}
