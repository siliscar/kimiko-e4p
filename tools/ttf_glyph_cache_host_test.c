/*
 * 中文：独立验证位图缓存链表、键与共享预算，不使用字体解析或显示替身。
 * English: Independently check bitmap-cache links, keys and shared budget without font parsing or display stubs.
 */
#include "ttf_glyph_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t live, released;
void heap_caps_free(void *allocation) {
    if (allocation) { assert(live); --live; ++released; }
    free(allocation);
}

static glyph_entry_t *entry(uint32_t cp, int size, int weight, int stroke, size_t bytes) {
    glyph_entry_t *e = calloc(1, sizeof(*e)); assert(e); ++live;
    if (bytes) { e->bitmap = malloc(bytes); assert(e->bitmap); ++live; memset(e->bitmap, 0x6b, bytes); }
    e->bitmap_bytes = bytes;
    e->codepoint = cp; e->size = size; e->weight = weight; e->stroke = stroke;
    return e;
}

// 从哈希独立计算占用和链表成员，避免只比较缓存维护的计数。
// Independently count bytes and membership from the hash rather than trusting maintained counters.
static size_t check(const ttf_glyph_cache_t *cache) {
    size_t n = 0, bytes = 0, links = 0;
    for (unsigned i = 0; i < TTF_CACHE_BUCKETS; ++i) {
        for (glyph_entry_t *e = cache->buckets[i]; e; e = e->hash_next) {
            assert((e->codepoint * 33u + e->size) % TTF_CACHE_BUCKETS == i);
            bytes += sizeof(*e) + e->bitmap_bytes; ++n; assert(n < 10000);
        }
    }
    const glyph_entry_t *prev = NULL;
    for (glyph_entry_t *e = cache->head; e; e = e->lru_next) {
        assert(e->lru_prev == prev); prev = e; ++links; assert(links <= n);
        size_t matches = 0;
        for (unsigned i = 0; i < TTF_CACHE_BUCKETS; ++i)
            for (glyph_entry_t *h = cache->buckets[i]; h; h = h->hash_next) matches += h == e;
        assert(matches == 1);
    }
    assert(links == n && prev == cache->tail && bytes == cache->bytes);
    return n;
}

int main(void) {
    ttf_glyph_cache_t a = {0}, b = {0}; ttf_glyph_budget_t budget = {0};
    assert(!ttf_glyph_cache_lookup(&a, 0x4e00, 59, 300, 0));
    glyph_entry_t *first = entry(0x4e00, 59, 300, 0, 65536);
    glyph_entry_t *collision = entry(0x4f00, 59, 300, 0, 65536);
    glyph_entry_t *weight = entry(0x4e00, 59, 500, 0, 65536);
    glyph_entry_t *stroke = entry(0x4e00, 59, 300, -16, 0);
    ttf_glyph_cache_insert(&a, &budget, first);
    ttf_glyph_cache_insert(&a, &budget, collision);
    ttf_glyph_cache_insert(&a, &budget, weight);
    ttf_glyph_cache_insert(&a, &budget, stroke);
    assert(check(&a) == 4 && a.head == stroke && a.tail == first);
    assert(ttf_glyph_cache_lookup(&a, 0x4e00, 59, 300, 0) == first);
    assert(a.head == first && a.tail == collision);
    assert(ttf_glyph_cache_lookup(&a, 0x4e00, 59, 500, 0) == weight);
    assert(ttf_glyph_cache_lookup(&a, 0x4e00, 59, 300, -16) == stroke);
    assert(!ttf_glyph_cache_lookup(&a, 0x4e00, 60, 300, 0));
    assert(!ttf_glyph_cache_lookup(&a, 0x4e00, 59, 301, 0));
    assert(!ttf_glyph_cache_lookup(&a, 0x4e00, 59, 300, 1));
    assert(check(&a) == 4);
    glyph_entry_t *other = entry(0x4e00, 59, 300, 0, 65536);
    ttf_glyph_cache_insert(&b, &budget, other);
    assert(budget.bytes == a.bytes + b.bytes);
    // 恰好达到预算不淘汰；超出一字节只淘汰最旧碰撞项，保留其他槽。
    // Exactly meeting the budget does not evict; exceeding it by one byte drops only the oldest collision entry, retaining other slots.
    size_t exact = TTF_CACHE_LIMIT - budget.bytes;
    assert(ttf_glyph_cache_reserve(&a, &budget, exact)); assert(check(&a) == 4);
    assert(ttf_glyph_cache_reserve(&a, &budget, exact + 1)); assert(check(&a) == 3);
    assert(!ttf_glyph_cache_lookup(&a, 0x4f00, 59, 300, 0));
    assert(check(&b) == 1 && b.head == other && released == 2);
    // 当前槽清空仍超预算时只报告，由调用方依原策略清其他就绪槽。
    // Report a remaining over-budget condition after draining this slot; the caller retains its policy for clearing other ready slots.
    assert(!ttf_glyph_cache_reserve(&a, &budget, TTF_CACHE_LIMIT));
    assert(check(&a) == 0 && check(&b) == 1 && budget.bytes == b.bytes);
    assert(ttf_glyph_cache_reset(&b, &budget) == 1);
    assert(!budget.bytes && !live && !check(&b));
    assert(ttf_glyph_cache_reset(&a, &budget) == 0);
    assert(ttf_glyph_cache_reserve(&a, &budget, TTF_CACHE_LIMIT));
    assert(!ttf_glyph_cache_reserve(&a, &budget, TTF_CACHE_LIMIT + 1));
    // 单项、头尾命中与重复重置都不得遗失条目或重复归还位图。
    // Single-entry, head/tail hits and repeated resets must not lose entries or double-free bitmaps.
    for (unsigned i = 0; i < 300; ++i) {
        glyph_entry_t *e = entry(i, 160, 800, 16, i % 13);
        ttf_glyph_cache_insert(&a, &budget, e);
        assert(ttf_glyph_cache_lookup(&a, i, 160, 800, 16) == e);
    }
    assert(check(&a) == 300 && budget.bytes == a.bytes);
    assert(ttf_glyph_cache_reset(&a, &budget) == 300);
    assert(!check(&a) && !budget.bytes && !live);
    puts("glyph cache: collisions, size/weight/stroke keys, blank glyphs, LRU order, exact shared budget, cross-slot release and repeated reset passed");
}
