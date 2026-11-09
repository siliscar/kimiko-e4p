/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：字形位图的哈希与 LRU，保留原有键、预算、淘汰和释放顺序。
 * English: Glyph bitmap hashing and LRU, preserving the original keys, budget, eviction and free order.
 */
#include "ttf_glyph_cache.h"
#include "esp_heap_caps.h"

static unsigned cache_bucket(uint32_t codepoint, int size) {
    return (unsigned)((codepoint * 33u + (uint32_t)size) % TTF_CACHE_BUCKETS);
}

// 新条目尚未在链表中，不能把空 prev/next 当作头尾身份。
// A new entry is not linked yet; null prev/next do not establish head/tail membership.
static void lru_detach(ttf_glyph_cache_t *cache, glyph_entry_t *entry) {
    if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else if (cache->head == entry) {
        cache->head = entry->lru_next;
    }
    if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else if (cache->tail == entry) {
        cache->tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void lru_touch(ttf_glyph_cache_t *cache, glyph_entry_t *entry) {
    if (cache->head == entry) return;
    lru_detach(cache, entry);
    entry->lru_next = cache->head;
    if (cache->head != NULL) cache->head->lru_prev = entry;
    cache->head = entry;
    if (cache->tail == NULL) cache->tail = entry;
}

size_t ttf_glyph_cache_reset(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget) {
    size_t n = 0;
    for (unsigned i = 0; i < TTF_CACHE_BUCKETS; i++) {
        glyph_entry_t *entry = cache->buckets[i];
        cache->buckets[i] = NULL;
        while (entry != NULL) {
            glyph_entry_t *next = entry->hash_next;
            heap_caps_free(entry->bitmap);
            heap_caps_free(entry);
            entry = next;
            n++;
        }
    }
    cache->head = NULL;
    cache->tail = NULL;
    budget->bytes -= cache->bytes;
    cache->bytes = 0;
    return n;
}

static void cache_evict_one(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget) {
    glyph_entry_t *victim = cache->tail;
    if (victim == NULL) return;
    lru_detach(cache, victim);
    unsigned bucket = cache_bucket(victim->codepoint, victim->size);
    glyph_entry_t **slot = &cache->buckets[bucket];
    while (*slot != NULL) {
        if (*slot == victim) {
            *slot = victim->hash_next;
            break;
        }
        slot = &(*slot)->hash_next;
    }
    cache->bytes -= victim->bitmap_bytes + sizeof(*victim);
    budget->bytes -= victim->bitmap_bytes + sizeof(*victim);
    heap_caps_free(victim->bitmap);
    heap_caps_free(victim);
}

bool ttf_glyph_cache_reserve(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget,
                             size_t extra) {
    while (cache->tail != NULL && budget->bytes + extra > TTF_CACHE_LIMIT) {
        cache_evict_one(cache, budget);
    }
    return budget->bytes + extra <= TTF_CACHE_LIMIT;
}

glyph_entry_t *ttf_glyph_cache_lookup(ttf_glyph_cache_t *cache, uint32_t codepoint,
                                    int size, int weight, int stroke) {
    unsigned bucket = cache_bucket(codepoint, size);
    for (glyph_entry_t *entry = cache->buckets[bucket]; entry != NULL; entry = entry->hash_next) {
        if (entry->codepoint == codepoint && entry->size == (uint8_t)size
            && entry->weight == (uint16_t)weight && entry->stroke == stroke) {
            lru_touch(cache, entry);
            return entry;
        }
    }
    return NULL;
}

void ttf_glyph_cache_insert(ttf_glyph_cache_t *cache, ttf_glyph_budget_t *budget,
                            glyph_entry_t *entry) {
    unsigned bucket = cache_bucket(entry->codepoint, entry->size);
    entry->hash_next = cache->buckets[bucket];
    cache->buckets[bucket] = entry;
    cache->bytes += entry->bitmap_bytes + sizeof(*entry);
    budget->bytes += entry->bitmap_bytes + sizeof(*entry);
    lru_touch(cache, entry);
}
