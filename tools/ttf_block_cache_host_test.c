/*
 * 中文：验证真实分块缓存的多槽隔离、容量、原有回退与文件游标，不替代字形像素测试。
 * English: Verify real block-cache slot isolation, bounds, original fallbacks and file cursors; glyph pixel tests remain separate.
 */
#undef read
#undef lseek
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ttf_block_cache.h"

static unsigned reads, seeks, raw_reads, allocations, live;
static int fail_alloc = -1, fail_read, fail_seek;
static size_t read_limit;
static int64_t ticks, measured;
static uint32_t position;
static int fd;
static uint8_t bytes[TTF_IO_BLOCK * 48 + 17];

void *heap_caps_malloc(size_t size, int caps) {
    assert(caps == 3); ++allocations;
    if (fail_alloc == 0) return NULL;
    if (fail_alloc > 0) --fail_alloc;
    void *pointer = malloc(size); assert(pointer); ++live; return pointer;
}
int64_t esp_timer_get_time(void) { return ++ticks; }
ssize_t cache_test_read(int source_fd, void *out, size_t size) {
    ++reads;
    if (fail_read) { --fail_read; return -1; }
    if (read_limit && size > read_limit) size = read_limit;
    return read(source_fd, out, size);
}
off_t cache_test_lseek(int source_fd, off_t offset, int origin) {
    ++seeks;
    if (fail_seek) { --fail_seek; return -1; }
    return lseek(source_fd, offset, origin);
}
static bool raw(uint32_t offset, void *out, size_t size) {
    ++raw_reads;
    int64_t start = esp_timer_get_time();
    bool ok = true;
    if (position != offset) {
        if (cache_test_lseek(fd, offset, SEEK_SET) < 0) { position = UINT32_MAX; ok = false; }
        else position = offset;
    }
    size_t done = 0;
    while (ok && done < size) {
        size_t chunk = size - done;
        if (chunk > 16384) chunk = 16384;
        ssize_t got = cache_test_read(fd, (uint8_t *)out + done, chunk);
        if (got <= 0) { position = UINT32_MAX; ok = false; break; }
        done += (size_t)got; position = offset + (uint32_t)done;
    }
    measured += esp_timer_get_time() - start;
    return ok;
}
static ttf_block_source_t source(void) {
    return (ttf_block_source_t){fd, &position, &measured, raw};
}
static void counters(void) {
    reads = seeks = raw_reads = allocations = 0;
    fail_alloc = -1; fail_read = fail_seek = 0; read_limit = 0; measured = ticks = 0;
    position = UINT32_MAX;
}
static void drop(ttf_block_cache_t *cache) {
    if (cache->f_io_data) { free(cache->f_io_data); --live; }
    if (cache->f_io_run_buf) { free(cache->f_io_run_buf); --live; }
    *cache = (ttf_block_cache_t){0};
}
static void check_read(ttf_block_cache_t *cache, uint32_t off, size_t size) {
    uint8_t out[TTF_IO_BLOCK * 2 + 128]; assert(size <= sizeof(out) && off + size <= sizeof(bytes));
    ttf_block_source_t input = source(); memset(out, 0, sizeof(out));
    assert(ttf_block_cache_read(cache, &input, off, out, size));
    assert(!memcmp(out, bytes + off, size));
}
int main(void) {
    FILE *file = tmpfile(); assert(file); fd = fileno(file);
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i * 37 + i / TTF_IO_BLOCK);
    assert(fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes)); assert(!fflush(file));
    ttf_block_cache_t a = {0}, b = {0}; uint8_t out[100];
    counters(); ttf_block_source_t input = source();
    assert(ttf_block_cache_read(&a, &input, 0, NULL, 0));
    assert(!allocations && !reads);
    check_read(&a, TTF_IO_BLOCK - 29, 100);
    assert(allocations == 2 && reads == 2 && seeks == 2 && measured == 2 && live == 2);
    unsigned prior = reads; check_read(&a, TTF_IO_BLOCK - 29, 100); assert(reads == prior);
    check_read(&b, 10, 32); assert(b.f_io_data != a.f_io_data && live == 4);
    memset(a.f_io_data, 0, TTF_IO_BLOCK); check_read(&b, 10, 32);
    uint8_t *data = a.f_io_data, *run = a.f_io_run_buf;
    ttf_block_cache_touch(&a, 8 * TTF_IO_BLOCK, 1); assert(a.f_touch_n == 1);
    ttf_block_cache_reset(&a); assert(a.f_io_data == data && a.f_io_run_buf == run && !a.f_touch_n);
    for (unsigned i = 0; i < TTF_IO_SLOTS; ++i) assert(a.f_io_base[i] == UINT32_MAX && !a.f_io_fill[i] && !a.f_io_age[i]);
    check_read(&a, 10, 32);
    // 超过32块依然只保留固定容量；重置不分配新缓冲。
    // More than 32 blocks still use fixed storage; reset allocates no new buffers.
    unsigned before_allocations = allocations;
    for (unsigned i = 0; i < 48; ++i) check_read(&a, i * TTF_IO_BLOCK, 10);
    assert(allocations == before_allocations && live == 4);
    a.f_io_clock = UINT16_MAX; check_read(&a, 47 * TTF_IO_BLOCK, 10);
    drop(&a); drop(&b); assert(!live);

    // 主缓冲不足回退原始读取；连续块暂存不足仍可逐块读。
    // Main-buffer OOM falls back to raw reads; run-scratch OOM still allows block reads.
    counters(); fail_alloc = 0; check_read(&a, TTF_IO_BLOCK - 3, 20);
    assert(raw_reads == 1 && !a.f_io_data && !a.f_io_run_buf && !live);
    counters(); fail_alloc = 1; check_read(&a, 3, 20);
    assert(a.f_io_data && !a.f_io_run_buf && !raw_reads && live == 1);
    ttf_block_cache_touch(&a, 3 * TTF_IO_BLOCK, 2 * TTF_IO_BLOCK);
    input = source(); prior = reads; ttf_block_cache_flush(&a, &input);
    assert(!a.f_touch_n && reads == prior + 2 && !raw_reads);
    drop(&a); assert(!live);

    // 预取排序去重，连续块按8块上限合并；首次分配清空预取队列的旧行为保留。
    // Sort/deduplicate prefetch and coalesce up to eight blocks; retain first-allocation queue clearing.
    counters(); ttf_block_cache_touch(&a, 2 * TTF_IO_BLOCK, 1);
    input = source(); ttf_block_cache_flush(&a, &input);
    assert(reads == 1 && !a.f_touch_n && allocations == 2);
    ttf_block_cache_touch(&a, 3 * TTF_IO_BLOCK + 100, 10);
    ttf_block_cache_touch(&a, 2 * TTF_IO_BLOCK + 20, 100);
    ttf_block_cache_touch(&a, 3 * TTF_IO_BLOCK + 5, 100);
    prior = reads; ttf_block_cache_flush(&a, &input);
    assert(raw_reads == 1 && reads == prior + 1 && !a.f_touch_n);
    prior = reads; check_read(&a, 2 * TTF_IO_BLOCK + 80, 100); assert(reads == prior);
    ttf_block_cache_reset(&a); counters();
    ttf_block_cache_touch(&a, 0, 10 * TTF_IO_BLOCK);
    input = source(); ttf_block_cache_flush(&a, &input);
    assert(raw_reads == 2 && reads == 3 && seeks == 1 && measured == 2);
    ttf_block_cache_touch(&a, 0, UINT32_MAX);
    assert(a.f_touch_n == TTF_PREFETCH_MAX); ttf_block_cache_reset(&a);

    // 合并读失败后逐块回退；尾块短读保持有效长度，EOF与seek/read错误保留无效游标。
    // A failed coalesced read falls back by block; short final blocks retain fill length, and EOF/seek/read errors invalidate the cursor.
    counters(); fail_read = 1; ttf_block_cache_touch(&a, 0, 2 * TTF_IO_BLOCK);
    input = source(); ttf_block_cache_flush(&a, &input);
    assert(raw_reads == 1 && reads == 3 && seeks == 3 && !a.f_touch_n);
    check_read(&a, 48 * TTF_IO_BLOCK, 17);
    assert(!ttf_block_cache_read(&a, &input, 48 * TTF_IO_BLOCK, out, 18));
    assert(!ttf_block_cache_read(&a, &input, sizeof(bytes), out, 1));
    fail_seek = 1;
    assert(!ttf_block_cache_read(&a, &input, 20 * TTF_IO_BLOCK, out, 1) && position == UINT32_MAX);
    fail_read = 1;
    assert(!ttf_block_cache_read(&a, &input, 21 * TTF_IO_BLOCK, out, 1) && position == UINT32_MAX);
    ttf_block_cache_reset(&a); counters(); read_limit = 17;
    assert(!ttf_block_cache_read(&a, &input, 0, out, 18));
    assert(a.f_io_fill[0] == 17 && reads == 1);
    drop(&a); assert(!live); fclose(file);
    puts("PASS: real font block caching, slot isolation, bounded storage, coalescing, OOM/raw fallback, short reads, seek/read errors and retained-buffer resets");
}
