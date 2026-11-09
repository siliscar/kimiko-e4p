/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从字体引擎提取字形位图缓存；字体槽持有缓存，所有槽共用原有预算。
 * English: Glyph bitmap caching extracted from the font engine; slots own caches and share the original budget.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTF_CACHE_LIMIT (320 * 1024)
#define TTF_CACHE_BUCKETS 256

typedef struct glyph_entry {
    uint32_t codepoint; ///< 字符码点 / Unicode codepoint
    uint8_t size; ///< 像素字号 / Pixel height
    uint16_t weight; ///< 字重 / Font weight
    int8_t stroke; ///< 笔画微调 / Stroke adjustment
    int16_t width; ///< 位图宽 / Bitmap width
    int16_t height; ///< 位图高 / Bitmap height
    int16_t left; ///< 左侧偏移 / Left bearing
    int16_t top; ///< 顶部偏移 / Top bearing
    int16_t advance_x; ///< 水平步进 / Horizontal advance
    uint8_t *bitmap; ///< 缓存持有的位图，可为空 / Cache-owned bitmap, possibly null
    size_t bitmap_bytes; ///< 位图占用 / Bitmap allocation size
    struct glyph_entry *hash_next; ///< 同桶下一项 / Next hash entry
    struct glyph_entry *lru_prev; ///< 较新条目 / More recent entry
    struct glyph_entry *lru_next; ///< 较旧条目 / Less recent entry
} glyph_entry_t;

typedef struct {
    glyph_entry_t *buckets[TTF_CACHE_BUCKETS]; ///< 哈希桶 / Hash buckets
    glyph_entry_t *head; ///< 最近使用项 / Most recently used entry
    glyph_entry_t *tail; ///< 最久未使用项 / Least recently used entry
    size_t bytes; ///< 位图与条目总占用 / Bitmap and entry bytes
} ttf_glyph_cache_t;

typedef struct {
    size_t bytes; ///< 所有字体槽的总占用 / Total bytes across font slots
} ttf_glyph_budget_t;

/// 缓存和预算均以零初始化；调用方串行化访问，返回的字形只借用到下次淘汰或重置前。
/// Zero-initialize caches and budget; caller serializes access and borrows glyphs only until eviction or reset.
glyph_entry_t *ttf_glyph_cache_lookup(ttf_glyph_cache_t *cache, uint32_t codepoint,
                                    int size, int weight, int stroke);
/// 按原有顺序淘汰当前槽；仍超额时返回 false，由字体层按原规则清其他已就绪槽。
/// Evict this slot in the original order; false asks the font layer to clear other ready slots under its existing policy.
bool ttf_glyph_cache_reserve(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget,
                             size_t extra);
/// 接管已零初始化的条目和位图；分配与光栅化仍由字体层完成，不改变插入顺序。
/// Own a zero-initialized entry and bitmap; allocation and rasterization remain in the font layer with unchanged insertion order.
void ttf_glyph_cache_insert(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget,
                            glyph_entry_t *entry);
/// 归还当前槽全部位图及条目并扣除共享占用，返回条目数供调用方保留原日志。
/// Free all bitmaps and entries in this slot, debit shared usage, and return the count for the caller's existing log.
size_t ttf_glyph_cache_reset(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget);
