#!/usr/bin/env python3
"""中文：按 PR9 原始实现，缺失插图记录不阻断后续正文。
English: Missing image records must not abort remaining prose, matching PR9.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/book/book_layout.c').read_text()
start = source.index('void book_layout_draw_page(')
end = len(source)
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "book_layout.h"
static const char *s_text="IMG\ntail";
static size_t s_count=1,s_len=8,s_pages[]={0};
static int s_px=10,s_lead_height,s_reading_line,s_reading_line_offset,s_tracking_px;
static EpdRect s_rect={0,0,100,100};
static int right_slack(void){return s_rect.x;}
static char s_line[16];
static uint32_t image_starts[]={0,1};
static uint32_t *s_page_img_start=image_starts;
typedef struct {int image,y,width,height;} layout_image_t;
static layout_image_t s_images[]={{0,10,80,20}};
static const blk_t block={.offset=0,.len=3,.image=0};
static int painted,baseline;
static size_t skip_image_spacing(size_t off){return off;}
static const blk_t *block_at(size_t off){return off==0?&block:NULL;}
int book_layout_page_image(size_t page){(void)page;return -1;}
int book_layout_page_image_count(size_t page){(void)page;return s_page_img_start[1]-s_page_img_start[0];}
static bool take_line(size_t off,int advance_limit,int hang_margin,size_t *next,bool *end,int *px,bool *heading,int *width,int *indent,uint8_t *align,int *before,int *after,size_t *line_start,size_t *line_end){
 (void)advance_limit;(void)hang_margin;
 assert(off==4);strcpy(s_line,"tail");*next=8;*end=true;*px=10;*heading=false;*width=40;*indent=0;*align=0;*before=*after=0;
 if(line_start)*line_start=4;if(line_end)*line_end=8;return true;
}
static int line_height_for(int px){return px;}
static int gap_for(int height,bool heading){(void)height;(void)heading;return 0;}
static int ttf_ascender_px(int px){return px;}
// rc93 竖排与按段换字体的状态；横排回归里竖排恒关、run 表恒空。
// rc93 vertical and per-run state; this horizontal regression keeps vertical off and runs empty.
static bool s_vertical;
static uint32_t s_generation;
static book_layout_draw_cursor_t *s_idle_draw;
#define LINE_RUN_MAX 64u
typedef struct {uint32_t offset;uint8_t slot;} ttf_run_t;
static ttf_run_t s_line_runs[LINE_RUN_MAX];
static size_t s_line_run_count;
#define TTF_FONT_SLOT_SYSTEM 0
static void slot_select_for(size_t off){(void)off;}
static void ttf_draw_set_runs(const ttf_run_t *runs,size_t count){(void)runs;(void)count;}
static int ttf_font_select(int slot){(void)slot;return 0;}
static void draw_vertical_page(uint8_t *fb,size_t page,EpdRect rect){(void)fb;(void)page;(void)rect;}
int test_guide_segments,test_guide_first_x,test_guide_first_y,test_guide_second_y,test_guide_last_y,test_guide_height;
uint8_t test_guide_gray;
static void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *text,int align,int fg,int bg){
 (void)fb;(void)x;(void)px;(void)align;(void)fg;(void)bg;assert(!strcmp(text,"tail"));++painted;baseline=y;
}
static void ttf_draw_text_px_spaced(uint8_t *fb,int x,int y,int px,const char *text,int tracking,int fg,int bg){(void)tracking;ttf_draw_text_px(fb,x,y,px,text,0,fg,bg);}
static void ttf_draw_text_px_fitted(uint8_t *fb,int x,int y,int px,const char *text,int tracking,int target,int fg,int bg){(void)target;ttf_draw_text_px_spaced(fb,x,y,px,text,tracking,fg,bg);}
// 内链命中表：book_layout_draw_page 之后的内链记录代码引用，声明在文件前部、测试切片
// 未覆盖。这里按产品同构声明，测试不验证命中内容，只要求能编译链接。
// Link hit tables: referenced by the link-recording code after book_layout_draw_page, declared
// earlier in the file and outside the test slice. Declared identically here; the test never
// asserts their contents, only that they link.
#define LINK_HIT_MAX 64u
typedef struct {int16_t link;int16_t x,y,width,height;} link_hit_t;
static const html_link_t* s_links;
static size_t s_link_count;
static link_hit_t s_link_hits[LINK_HIT_MAX], s_link_hits_next[LINK_HIT_MAX];
static size_t s_link_hit_count, s_link_hit_count_next;
static size_t s_link_hit_page, s_link_hit_page_next;
static uint32_t s_link_hit_generation, s_link_hit_generation_next;
static link_hit_t* s_link_hits_out = s_link_hits;
static size_t* s_link_hits_out_count = &s_link_hit_count;
static void record_line_links(size_t line_start,size_t line_end,int x,int y,int px,bool heading,int line_width,int target){
 (void)line_start;(void)line_end;(void)x;(void)y;(void)px;(void)heading;(void)line_width;(void)target;
}
void book_layout_set_links(const html_link_t* links,size_t count){s_links=(links&&count)?links:NULL;s_link_count=s_links?count:0;}
'''+source[start:end]+r'''
int main(void){
 uint8_t fb=0;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==45);
 painted=0;s_page_img_start[1]=0;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==10);
 painted=0;s_page_img_start[1]=1;s_images[0].image=7;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==10);
 puts("PASS: PR9 missing/mismatched image slots skip the unmatched record and continue prose");
}
'''
with tempfile.TemporaryDirectory(prefix='book-image-record-') as directory:
    work=Path(directory)
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(root/'tools/book_layout_stubs'),'-I'+str(root/'tools/book_source_host_stubs'),'-I'+str(root/'main/book'),
                    str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
