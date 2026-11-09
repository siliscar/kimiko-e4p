/*
 * 中文：跟踪真实书源自有分配，只接受活跃基址；不把文件库内部或调用方章节计入书源。
 * English: Track real source-owned allocations, accepting live bases only and excluding libc file internals and caller chapters.
 */
#include "reader_memory.h"
#include "book_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

reader_perf_scope_t reader_perf_scope_begin(reader_perf_stage_t stage) {
    return (reader_perf_scope_t){.stage=stage};
}
void reader_perf_scope_end(reader_perf_scope_t *scope) { (void)scope; }

static struct { void *pointer; size_t bytes; } allocations[8192];
static size_t live, queries, calls;
static long fail_at = -1;
static bool should_fail(void) { return (long)calls++ == fail_at; }
static unsigned slot_of(void *pointer) {
    for (unsigned i=0;i<8192;++i) if (allocations[i].pointer==pointer) return i;
    abort();
}
void *tracked_malloc(size_t bytes) {
    if (should_fail()) return NULL;
    void *p=malloc(bytes); if (!p) return NULL;
    unsigned i=slot_of(NULL); allocations[i].pointer=p;
    allocations[i].bytes=bytes+31; live+=bytes+31; return p;
}
void *tracked_calloc(size_t count,size_t bytes) {
    assert(!bytes || count<=SIZE_MAX/bytes);
    void *p=tracked_malloc(count*bytes); if(p) memset(p,0,count*bytes); return p;
}
void tracked_free(void *pointer) {
    if (!pointer) return;
    unsigned i=slot_of(pointer); live-=allocations[i].bytes;
    allocations[i].pointer=NULL; free(pointer);
}
void *tracked_realloc(void *pointer,size_t bytes) {
    if (!pointer) return tracked_malloc(bytes);
    if (!bytes) { tracked_free(pointer); return NULL; }
    if (should_fail()) return NULL;
    unsigned i=slot_of(pointer); void *p=realloc(pointer,bytes); if(!p) return NULL;
    live-=allocations[i].bytes; allocations[i].pointer=p; allocations[i].bytes=bytes+31;
    live+=bytes+31; return p;
}
size_t heap_caps_get_allocated_size(void *pointer) {
    assert(pointer); ++queries; return allocations[slot_of(pointer)].bytes;
}
int ttf_font_open_mem(uint8_t *data,size_t size,const char *label) {
    (void)size;(void)label;tracked_free(data);return -1;
}
void ttf_font_close_embedded(void) {}
static size_t capacity(reader_source_memory_stats_t s) {
    return s.txt_index+s.mobi_state+s.mobi_index+s.epub_state+s.epub_index+s.epub_text+s.epub_faces+
        s.zip_state+s.zip_index+s.zip_names+s.zip_directory;
}
static reader_source_memory_stats_t snapshot(void) {
    reader_source_memory_stats_t a,b; memset(&a,0xa5,sizeof(a));memset(&b,0x5a,sizeof(b));
    book_source_memory_stats(&a);book_source_memory_stats(&b);assert(!memcmp(&a,&b,sizeof(a)));
    return a;
}
static void closed(void) {
    book_close();book_close();reader_source_memory_stats_t s=snapshot();
    assert(!capacity(s)&&!s.open_files&&!live);
}
int main(int argc,char **argv) {
    assert(argc==5);closed();assert(!queries);
    for(unsigned round=0;round<20;++round)for(int book=1;book<=4;++book) {
        assert(book_open(argv[book])==ESP_OK);
        reader_source_memory_stats_t before=snapshot();
        assert(capacity(before)==live&&before.open_files==1);
        if(book==1) assert(before.txt_index&&!before.epub_state&&!before.zip_state);
        else if(book==4) assert(book_kind()==BOOK_KIND_MOBI&&before.mobi_state&&before.mobi_index&&
                               !before.txt_index&&!before.epub_state&&!before.zip_state);
        else assert(!before.txt_index&&before.epub_state&&before.epub_index&&before.epub_text&&
                    before.zip_state&&before.zip_index&&before.zip_names&&!before.zip_directory);
        size_t last=book_chapter_count()-1;
        for(size_t chapter=0;chapter<=last;chapter+=last?last:1) {
            html_text_t content={0};assert(book_chapter_load_blocks(chapter,&content)==ESP_OK);
            reader_source_memory_stats_t during=snapshot();assert(capacity(during)<live);
            if(book==2)assert(during.epub_faces);
            html_text_free(&content);assert(capacity(snapshot())==live);
            if(chapter==last)break;
        }
        (void)book_navigation_count();assert(capacity(snapshot())==live);
        closed();
    }
    size_t failures=0;
    for(int book=2;book<=4;book+=2) {
    calls=0;assert(book_open(argv[book])==ESP_OK);size_t open_calls=calls;closed();
    failures+=open_calls;
    for(size_t i=0;i<open_calls;++i) {
        calls=0;fail_at=(long)i;esp_err_t error=book_open(argv[book]);fail_at=-1;
        reader_source_memory_stats_t s=snapshot();
        if(error==ESP_OK)assert(capacity(s)==live&&s.open_files==1);
        else assert(!capacity(s)&&!s.open_files&&!live);
        closed();
    }
    }
    assert(book_open(argv[4])==ESP_OK);
    calls=0;html_text_t text={0};assert(book_chapter_load_blocks(1,&text)==ESP_OK);
    size_t load_calls=calls;html_text_free(&text);
    failures+=load_calls;
    for(size_t i=0;i<load_calls;++i) {
        calls=0;fail_at=(long)i;esp_err_t error=book_chapter_load_blocks(1,&text);fail_at=-1;
        if(error!=ESP_OK)assert(!text.utf8&&!text.blocks&&!text.links);
        html_text_free(&text);assert(capacity(snapshot())==live);
    }
    closed();
    assert(book_open("/tmp/source-memory-missing.unknown")==ESP_ERR_NOT_SUPPORTED);closed();
    printf("PASS: 80 real TXT/EPUB/MOBI open/parse/navigation/close cycles, %zu allocation-failure points, allocation capacities, live-base-only queries and repeated zero exit\n",failures);
}
