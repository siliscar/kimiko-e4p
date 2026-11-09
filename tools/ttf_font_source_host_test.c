/*
 * 中文：独立验证字体字节源的游标、短读、映射预算和资源交还，不运行字体解析或显示。
 * English: Independently verify font-source cursors, short reads, map budgets and resource release without parsing or display.
 */
#include "ttf_font_source.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static uint32_t positions[2];
static size_t eof = 6u * 1024u * 1024u, read_limit = 16384;
static unsigned reads, seeks, closes, fail_read_at;
static bool seek_error, alloc_error;
static size_t largest = 8u * 1024u * 1024u, allocations, frees, live;
static int64_t now;
static uint8_t byte_at(int fd, size_t offset) { return (uint8_t)(fd * 17 + offset * 13); }
static void expected(const uint8_t *data, size_t n, int fd, size_t offset) {
    for (size_t i = 0; i < n; ++i) assert(data[i] == byte_at(fd, offset + i));
}
ssize_t source_test_read(int fd, void *dst, size_t n) {
    assert(fd >= 7 && fd <= 8 && n <= 16384); ++reads;
    if (reads == fail_read_at) return -1;
    size_t pos = positions[fd - 7];
    if (pos >= eof) return 0;
    if (n > read_limit) n = read_limit;
    if (n > eof - pos) n = eof - pos;
    for (size_t i = 0; i < n; ++i) ((uint8_t *)dst)[i] = byte_at(fd, pos + i);
    positions[fd - 7] += (uint32_t)n;
    return (ssize_t)n;
}
off_t source_test_lseek(int fd, off_t offset, int whence) {
    assert(fd >= 7 && fd <= 8 && offset >= 0 && whence == SEEK_SET); ++seeks;
    if (seek_error) return -1;
    positions[fd - 7] = (uint32_t)offset; return offset;
}
int source_test_close(int fd) { assert(fd >= 7 && fd <= 8); ++closes; return 0; }
int64_t esp_timer_get_time(void) { now += 11; return now; }
void source_test_log(const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "ttf_font")); (void)format;
}
void *heap_caps_malloc(size_t n, int caps) {
    assert(caps == 3);
    if (alloc_error) return NULL;
    void *p = malloc(n); assert(p); ++allocations; ++live; return p;
}
void heap_caps_free(void *p) { if (!p) return; assert(live); --live; ++frees; free(p); }
size_t heap_caps_get_largest_free_block(int caps) { assert(caps == 3); return largest; }

static void stream_reads(void) {
    ttf_font_source_t a = {.f_fd=7, .f_file_pos=UINT32_MAX};
    ttf_font_source_t b = {.f_fd=8, .f_file_pos=UINT32_MAX};
    uint8_t buffer[40000]; int64_t measured = 37;
    assert(ttf_font_source_read(&a, 100, buffer, sizeof(buffer), &measured));
    expected(buffer, sizeof(buffer), 7, 100);
    assert(reads == 3 && seeks == 1 && a.f_file_pos == 40100 && measured == 48);
    unsigned before = seeks; int64_t time_before = now;
    assert(ttf_font_source_read(&a, 40100, buffer, 23, NULL));
    expected(buffer, 23, 7, 40100); assert(seeks == before && now == time_before);
    assert(ttf_font_source_read(&b, 17, buffer, 31, NULL));
    expected(buffer, 31, 8, 17); assert(a.f_file_pos == 40123 && b.f_file_pos == 48);
    read_limit = 257;
    assert(ttf_font_source_read(&a, 50, buffer, 1000, NULL));
    expected(buffer, 1000, 7, 50); assert(a.f_file_pos == 1050);
    fail_read_at = reads + 2;
    assert(!ttf_font_source_read(&a, 50, buffer, 1000, &measured));
    assert(a.f_file_pos == UINT32_MAX); expected(buffer, 257, 7, 50);
    fail_read_at = 0; before = seeks;
    assert(ttf_font_source_read(&a, 50, buffer, 1000, NULL) && seeks == before + 1);
    eof = 55;
    assert(!ttf_font_source_read(&a, 50, buffer, 10, NULL));
    assert(a.f_file_pos == UINT32_MAX); expected(buffer, 5, 7, 50);
    seek_error = true; before = reads;
    assert(!ttf_font_source_read(&a, 5, buffer, 1, &measured));
    assert(a.f_file_pos == UINT32_MAX && reads == before);
    seek_error = false; eof = 6u*1024u*1024u; read_limit = 16384;
    ttf_font_source_abandon(&a); ttf_font_source_abandon(&b);
    assert(a.f_fd == -1 && b.f_fd == -1 && closes == 2);
    ttf_font_source_abandon(&a); assert(closes == 2);
    assert(!ttf_font_source_read(&a, 0, buffer, 0, NULL));
}
static void memory_reads(void) {
    uint8_t memory[32], buffer[32];
    for (unsigned i = 0; i < sizeof(memory); ++i) memory[i] = (uint8_t)(i*3);
    ttf_font_source_t source = {.f_fd=-1, .f_mem=memory, .f_mem_len=sizeof(memory), .f_file_pos=71};
    int64_t measured = 17, before_time = now; unsigned before_reads = reads;
    assert(ttf_font_source_read(&source, 3, buffer, 29, &measured));
    assert(!memcmp(buffer, memory+3, 29) && measured == 17 && now == before_time);
    assert(!ttf_font_source_read(&source, 4, buffer, 29, NULL));
    assert(!ttf_font_source_read(&source, UINT32_MAX, buffer, 1, NULL));
    assert(ttf_font_source_read(&source, 32, buffer, 0, NULL));
    assert(reads == before_reads && source.f_file_pos == 71);
    size_t before_frees = frees;
    ttf_font_source_abandon(&source);
    assert(!source.f_mem && !source.f_mem_len && !source.f_mem_owned && frees == before_frees);
    assert(source.f_file_pos == 71);
    source.f_mem = heap_caps_malloc(32, 3); source.f_mem_len = 32; source.f_mem_owned = true;
    ttf_font_source_abandon(&source); ttf_font_source_abandon(&source);
    assert(frees == before_frees+1 && !live);
}
static void table_maps(void) {
    ttf_font_source_t a = {.f_fd=7, .f_file_pos=UINT32_MAX};
    ttf_font_source_t b = {.f_fd=8, .f_file_pos=UINT32_MAX};
    uint8_t output[8192]; const size_t reserve = 2560u * 1024u;
    assert(ttf_font_source_covers(&a, UINT32_MAX, 0));
    assert(!ttf_font_source_covers(&a, 0, 1));
    assert(!ttf_font_source_read_map(&a, 0, 1, output));
    assert(!ttf_font_source_map_table(&a, 512, 4095, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    assert(!ttf_font_source_map_table(&a, 512, 5u*1024u*1024u+1, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    largest = reserve+4095;
    assert(!ttf_font_source_map_table(&a, 512, 4096, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    assert(!allocations || allocations == frees);
    largest = reserve+4096; alloc_error = true;
    assert(!ttf_font_source_map_table(&a, 512, 4096, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    alloc_error = false; fail_read_at = reads+1;
    assert(!ttf_font_source_map_table(&a, 512, 4096, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    assert(!a.f_glyf_ram && !a.f_glyf_ram_off && !a.f_glyf_ram_len && !live);
    fail_read_at = 0;
    assert(ttf_font_source_map_table(&a, 512, 4096, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    expected(a.f_glyf_ram, 4096, 7, 512);
    unsigned before = reads;
    assert(!ttf_font_source_map_table(&a, 512, 4096, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    assert(reads == before && a.f_glyf_ram_off == 512 && a.f_glyf_ram_len == 4096);
    assert(ttf_font_source_covers(&a, 512, 4096) && ttf_font_source_covers(&a, 4608, 0));
    assert(!ttf_font_source_covers(&a, 511, 1) && !ttf_font_source_covers(&a, 4607, 2));
    assert(ttf_font_source_read_map(&a, 600, 4000, output)); expected(output, 4000, 7, 600);
    assert(!ttf_font_source_read_map(&a, 600, 4009, output));
    assert(ttf_font_source_map_table(&b, 512, 4096, &b.f_glyf_ram, &b.f_glyf_ram_off, &b.f_glyf_ram_len, "glyf", NULL));
    assert(ttf_font_source_read_map(&b, 512, 4096, output)); expected(output, 4096, 8, 512);
    assert(ttf_font_source_map_table(&a, 2048, 4096, &a.f_gvar_ram, &a.f_gvar_ram_off, &a.f_gvar_ram_len, "gvar", NULL));
    memset(a.f_gvar_ram, 0x6d, 4096);
    assert(ttf_font_source_read_map(&a, 3000, 10, output)); expected(output, 10, 7, 3000);
    assert(ttf_font_source_read_map(&a, 5000, 10, output));
    for (unsigned i=0;i<10;++i) assert(output[i]==0x6d);
    assert(ttf_font_source_covers(&a, 5000, 10));
    // 字节源关闭不越过原有映射回收边界，槽的关闭顺序由上层保持。
    // Closing bytes does not cross the existing map-release boundary; the caller retains slot teardown ordering.
    ttf_font_source_abandon(&a); assert(a.f_glyf_ram && a.f_gvar_ram && live==3);
    ttf_font_source_unmap(&a); ttf_font_source_unmap(&a);
    assert(!a.f_glyf_ram && !a.f_gvar_ram && !a.f_glyf_ram_off && !a.f_glyf_ram_len);
    assert(!a.f_gvar_ram_off && !a.f_gvar_ram_len && live==1);
    assert(ttf_font_source_covers(&b,512,4096));
    ttf_font_source_unmap(&b); ttf_font_source_abandon(&b); assert(!live);
    largest = reserve + 5u*1024u*1024u;
    a = (ttf_font_source_t){.f_fd=7, .f_file_pos=UINT32_MAX};
    assert(ttf_font_source_map_table(&a, 0, 5u*1024u*1024u, &a.f_glyf_ram, &a.f_glyf_ram_off, &a.f_glyf_ram_len, "glyf", NULL));
    expected(a.f_glyf_ram,5u*1024u*1024u,7,0);
    ttf_font_source_unmap(&a);ttf_font_source_abandon(&a);assert(!live);
}
static void repeated_release(void) {
    for(unsigned i=0;i<300;++i) {
        uint8_t *memory=heap_caps_malloc(8192,3);memset(memory,(int)(i&255),8192);
        ttf_font_source_t source={.f_fd=-1,.f_mem=memory,.f_mem_len=8192,.f_mem_owned=true,.f_file_pos=UINT32_MAX};
        alloc_error=i%3==0;
        bool mapped=ttf_font_source_map_table(&source,0,4096,&source.f_glyf_ram,&source.f_glyf_ram_off,&source.f_glyf_ram_len,"glyf",NULL);
        assert(mapped!=alloc_error);alloc_error=false;
        if(mapped)assert(!memcmp(source.f_glyf_ram,memory,4096));
        ttf_font_source_unmap(&source);ttf_font_source_abandon(&source);
        ttf_font_source_unmap(&source);ttf_font_source_abandon(&source);
        assert(!live&&allocations==frees);
    }
}
int main(void) {
    stream_reads(); memory_reads(); table_maps(); repeated_release();
    assert(allocations==frees && !live);
    printf("PASS: font sources retain byte output, 16KiB chunks/cursors, short/error/EOF reads, memory bounds, map reserve/cap/priority, slot isolation and exactly-once release (%zu allocations)\n",allocations);
}
