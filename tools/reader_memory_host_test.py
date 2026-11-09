#!/usr/bin/env python3
"""中文：运行真实快照函数，拒绝借用指针并核对退出与常驻资源。
English: Exercise actual snapshot functions, rejecting borrowed pointers and checking released versus retained resources.
"""
from pathlib import Path
from shelf_cover_test_support import declarations, implementation
import hashlib
import json
import os
import subprocess
import tempfile
import summarize_reader_memory as summary
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader_memory.h"
#include "html_text.h"
#include "ttf_font_source.h"
#include "ttf_block_cache.h"
#include "ttf_glyph_cache.h"
#define TTF_FONT_SLOTS 3
typedef struct {
 ttf_font_source_t f_source;
 uint8_t *f_file_loca,*f_font_data;
 uint32_t *f_gvar_glyph_off;
 ttf_block_cache_t f_blocks;
 ttf_glyph_cache_t f_glyphs;
 bool f_ready,f_data_borrowed;
} ttf_slot_t;
static ttf_slot_t s_slots[TTF_FONT_SLOTS];
static void *work,*s_catalog,*s_pages,*s_line,*s_images,*s_page_img_start,*s_next_fb,*s_gray;
static void *s_prep_task;
static struct {html_text_t content;} s_reader_session;
static const html_text_t *reader_session_content(const void *session){assert(session==&s_reader_session);return &s_reader_session.content;}
static struct {uint8_t *gray;} *s_page_images;
static int s_page_image_count;
static void *s_shelf,*s_guard_cache,*s_editor_cover;
SHELF_CONTEXT_DECLARATIONS
static struct {void *pointer;size_t bytes;} allocated[64];
static size_t live;
static unsigned queries,logs;
static bool ready=true;
bool reader_perf_memory_ready(void){return ready;}
static reader_memory_stats_t captured;
static unsigned source_snapshots,source_logs;
void book_source_memory_stats(reader_source_memory_stats_t *stats){
 assert(ready);*stats=(reader_source_memory_stats_t){.txt_index=123};++source_snapshots;
}
void reader_perf_source_memory(const char *event,const reader_source_memory_stats_t *stats){
 assert(!strcmp(event,"test")&&stats->txt_index==123);++source_logs;
}
static void *own(size_t bytes){
 void *p=calloc(1,bytes);assert(p);
 for(unsigned i=0;i<64;++i)if(!allocated[i].pointer){allocated[i].pointer=p;allocated[i].bytes=bytes;live+=bytes;return p;}
 abort();
}
static void release(void **p){
 if(!*p)return;
 for(unsigned i=0;i<64;++i)if(allocated[i].pointer==*p){live-=allocated[i].bytes;free(*p);allocated[i].pointer=NULL;*p=NULL;return;}
 abort();
}
size_t heap_caps_get_allocated_size(void *p){
 assert(p);++queries;
 for(unsigned i=0;i<64;++i)if(allocated[i].pointer==p)return allocated[i].bytes;
 // 借用镜像、内部指针、任务句柄与已释放指针都必须被排除。
 // Borrowed images, interior pointers, task handles and freed pointers must all be excluded.
 abort();
}
static unsigned uxTaskGetStackHighWaterMark(void *task){assert(task==s_prep_task&&task);return 1234;}
static void reader_perf_memory_test(const char *event,const reader_memory_stats_t *stats){
 assert(!strcmp(event,"test"));captured=*stats;++logs;
}
#define reader_perf_memory reader_perf_memory_test
'''
UNIT=UNIT.replace('SHELF_CONTEXT_DECLARATIONS',declarations())+implementation()
for path, name in [
    ('main/font/ttf_font.c', 'ttf_font_memory_stats'),
    ('main/book/book_layout.c', 'book_layout_memory_stats'),
    ('main/book/reader_prefetch.c', 'reader_prefetch_memory_stats'),
    ('main/ui/ui_reader_background.c', 'ui_reader_background_memory_stats'),
    ('main/apps/app_book.c', 'reader_memory_snapshot'),
]:
    UNIT += function(name, ROOT / path) + '\n'
UNIT += r'''
static size_t listed(void){
 return captured.font_source+captured.font_tables+captured.font_blocks+captured.font_glyphs+
 captured.font_work+captured.font_catalog+captured.layout+captured.chapter+captured.prefetch+
 captured.background+captured.page_images+captured.shelf_ui;
}
int main(void){
 reader_memory_snapshot("test");assert(!listed()&&!queries&&!captured.prefetch_worker&&!captured.prefetch_stack_free);
 // 两种别名：自有镜像再借给CFF工作字体，静态镜像只借用；均不得重复统计。
 // Two aliases: an owned image borrowed as CFF working data, and a borrowed static image; neither is counted twice.
 static uint8_t borrowed[100];
 s_slots[0].f_source=(ttf_font_source_t){.f_mem=own(101),.f_mem_owned=true};
 s_slots[0].f_font_data=(uint8_t*)s_slots[0].f_source.f_mem;s_slots[0].f_data_borrowed=true;s_slots[0].f_ready=true;
 s_slots[1].f_source=(ttf_font_source_t){.f_mem=borrowed};
 s_slots[1].f_font_data=borrowed;s_slots[1].f_data_borrowed=true;s_slots[1].f_ready=true;
 s_slots[2].f_font_data=own(102);
 s_slots[0].f_source.f_glyf_ram=own(103);s_slots[0].f_source.f_gvar_ram=own(104);
 s_slots[0].f_file_loca=own(105);s_slots[0].f_gvar_glyph_off=own(108);
 s_slots[0].f_blocks.f_io_data=own(109);s_slots[0].f_blocks.f_io_run_buf=own(110);
 glyph_entry_t *a=own(sizeof(*a)),*b=own(sizeof(*b));a->bitmap=own(111);a->lru_next=b;
 s_slots[0].f_glyphs.head=a;
 work=own(112);s_catalog=own(113);
 s_pages=own(114);s_line=own(115);s_images=own(116);s_page_img_start=own(117);
 s_next_fb=own(118);s_gray=own(119);s_prep_task=borrowed;
 html_text_t *ch=&s_reader_session.content;
 ch->utf8=own(120);ch->blocks=own(121);ch->runs=own(122);ch->links=own(123);
 ch->anchors=own(124);ch->strings=own(125);ch->images=own(2*sizeof(char*));ch->image_count=2;
 ch->images[0]=own(126);ch->images[1]=own(127);
 s_page_images=own(2*sizeof(*s_page_images));s_page_image_count=2;s_page_images[0].gray=own(128);
 s_shelf=own(129);s_guard_cache=own(130);s_editor_cover=own(131);s_covers[0].gray=own(132);s_covers[1].fast_bits=own(133);
 ready=false;unsigned before_queries=queries;reader_memory_snapshot("test");assert(queries==before_queries&&logs==1&&source_snapshots==1&&source_logs==1);ready=true;
 reader_memory_snapshot("test");assert(listed()==live&&captured.font_source==101&&captured.font_tables==522);
 assert(captured.font_glyphs==2*sizeof(*a)+111&&captured.font_slots==2);
 assert(captured.layout==462&&captured.prefetch==118&&captured.background==119);
 assert(captured.prefetch_worker==1&&captured.prefetch_stack_free==1234);
 reader_memory_stats_t old=captured;reader_memory_snapshot("test");assert(!memcmp(&old,&captured,sizeof(old)));
 // 部分失败和关闭后的NULL指针不受旧长度影响；保留块工作区单独可见。
 // NULL pointers after partial failure or close ignore stale lengths; retained blocks remain visible separately.
 release((void**)&s_slots[0].f_source.f_mem);s_slots[0].f_font_data=NULL;
 release(&s_next_fb);release(&s_gray);s_prep_task=NULL;
 reader_memory_snapshot("test");assert(listed()==live&&!captured.font_source&&!captured.prefetch&&!captured.background);
 assert(captured.font_blocks==219&&!captured.prefetch_worker&&!captured.prefetch_stack_free);
 // 清空模拟拥有者后重复读零，分配查询不得触碰旧指针。
 // Clearing simulated owners allows repeated zero snapshots without querying stale pointers.
 for(unsigned i=0;i<64;++i)if(allocated[i].pointer){free(allocated[i].pointer);allocated[i].pointer=NULL;}
 memset(s_slots,0,sizeof(s_slots));memset(&s_reader_session,0,sizeof(s_reader_session));memset(s_covers,0,sizeof(s_covers));
 work=s_catalog=s_pages=s_line=s_images=s_page_img_start=s_shelf=s_guard_cache=s_editor_cover=NULL;
 s_page_images=NULL;s_page_image_count=0;
 reader_memory_snapshot("test");reader_memory_snapshot("test");assert(!listed()&&logs==6&&source_snapshots==6&&source_logs==6);
 puts("PASS: actual owner snapshots, borrowed/static exclusion, CFF alias, glyph lists, partial allocation, retained workspaces and repeated exit");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='reader-memory-') as directory:
        folder = Path(directory)
        (folder / 'sdkconfig.h').write_text('#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 1\n')
        (folder / 'esp_heap_caps.h').write_text('#pragma once\n#include <stddef.h>\nsize_t heap_caps_get_allocated_size(void *p);\n')
        (folder / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
        source, binary = folder / 'test.c', folder / 'test'
        source.write_text(UNIT)
        command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined']
        command += ['-I' + str(folder), '-I' + str(ROOT / 'main'), "-I" + str(ROOT / "main/boot"), "-I" + str(ROOT / "main/diagnostics"), "-I" + str(ROOT / "main/display"), "-I" + str(ROOT / "main/settings"), '-I' + str(ROOT / 'main/book'), '-I' + str(ROOT / 'main/font')]
        subprocess.run(command + [str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        # 关闭诊断时不暴露堆接口或资源结构。/ Disabled diagnostics expose neither heap APIs nor resource structures.
        (folder / 'sdkconfig.h').write_text('#define CONFIG_READER_PERF 0\n#define CONFIG_READER_MEMORY 1\n')
        (folder / 'esp_heap_caps.h').write_text('#error disabled diagnostics included heap API\n')
        source.write_text('#include "reader_memory.h"\nint main(void){return 0;}\n')
        subprocess.run(command + [str(source), '-o', str(binary)], check=True)
        (folder / 'sdkconfig.h').write_text('#define CONFIG_READER_PERF 1\n#define CONFIG_READER_MEMORY 0\n')
        subprocess.run(command + [str(source), '-o', str(binary)], check=True)
        print('PASS: resource-disabled and profiler-disabled headers need no heap API')
    row={key:0 for key in summary.FIELDS};row.update(v='1',event='reader_exit',psram_free=1000)
    record='READER_MEMORY '+' '.join(f'{key}={value}' for key,value in row.items())
    samples=summary.parse_samples('boot\nI (42) readerperf: '+record+'\nREADER_PERF v=1')
    row2=dict(samples[0],psram_free=999,font_blocks=128)
    result=summary.summarize(samples+[row2])['reader_exit']
    assert result['samples']==2 and result['fields']['psram_free']=={'first':1000,'last':999,'min':999,'max':1000}
    for broken in ('boot only',record.replace('v=1','v=2'),record.replace('id=0','id=-1'),
                   record+' id=0',record.replace('event=reader_exit','event=unknown'),
                   record.replace('psram_free=1000',''),record.replace('prefetch_worker=0','prefetch_worker=2')):
        try:
            summary.parse_samples(broken)
        except ValueError:
            continue
        raise AssertionError('invalid resource record accepted')
    print('PASS: resource parser schemas, partial records, duplicates and separate exit ranges')
    with tempfile.TemporaryDirectory(prefix='reader-memory-report-') as directory:
        folder=Path(directory);log=folder/'serial.log';report=folder/'report.json'
        log.write_text('boot\nI (42) readerperf: '+record+'\n')
        subprocess.run(['python3',str(ROOT/'tools/summarize_reader_memory.py'),str(log),'--output',str(report)],check=True)
        result=json.loads(report.read_text())
        assert result['log_sha256']==hashlib.sha256(log.read_bytes()).hexdigest()
        assert result['events']['reader_exit']['samples']==1 and result['log']==str(log.resolve())
    print('PASS: resource summary CLI ties output to the exact serial log hash')
    env=os.environ.copy();env['READER_MEMORY_TEST']='1'
    subprocess.run(['python3',str(ROOT / 'tools/title_font_host_test.py')],env=env,check=True)


if __name__ == '__main__':
    main()
