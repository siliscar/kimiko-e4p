#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实已显示正文摘句、进度节流与双缓存故障回归。/ English: Regress confirmed-text excerpts, progress cadence and checked-copy failures."""
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *root;
static unsigned io_count, writes;
static bool fail_write, fail_rename;
static void mapped(const char *p,char out[1024]){assert(!strncmp(p,"/sdcard",7));assert(snprintf(out,1024,"%s%s",root,p+7)<1024);}
static FILE *mapped_open(const char *p,const char *m){++io_count;char out[1024];mapped(p,out);if(strchr(m,'w')){++writes;if(fail_write)return NULL;}return fopen(out,m);}
static int mapped_stat(const char *p,struct stat *s){++io_count;char out[1024];mapped(p,out);return stat(out,s);}
static int mapped_mkdir(const char *p,mode_t mode){++io_count;char out[1024];mapped(p,out);return mkdir(out,mode);}
static int mapped_remove(const char *p){++io_count;char out[1024];mapped(p,out);return remove(out);}
static int mapped_rename(const char *p,const char *q){++io_count;char a[1024],b[1024];mapped(p,a);mapped(q,b);if(fail_rename){errno=EIO;return -1;}return rename(a,b);}
#define fopen mapped_open
#define stat(p,s) mapped_stat(p,s)
#define mkdir mapped_mkdir
#define remove mapped_remove
#define rename mapped_rename
#include "book_index_cache.c"
#include "book_excerpt.c"
#undef fopen
#undef stat
#undef mkdir
#undef remove
#undef rename
static const char *book="/sdcard/book.epub";
static const char *const lines[]={
 "窗外的微风带来了春天的消息。", "远方的山峦在薄雾中若隐若现。", "清晨的小路上留下了浅浅脚印。",
 "沿着河岸慢慢走就能看见大海。", "每一个平凡日子都藏着小惊喜。", "灯火照亮了那条通往家的小路。",
 "夜色中的城市终于安静了下来。", "书页之间夹着一片金黄的落叶。", "他在山顶终于看见第一缕阳光。"
};
static html_text_t text;
static blk_t paragraphs[16];
static char body[8192];
static void fixture(void){
 body[0]=0;
 for(unsigned i=0;i<sizeof(lines)/sizeof(lines[0]);++i){size_t off=strlen(body);strcat(body,lines[i]);strcat(body,"\n");paragraphs[i]=(blk_t){.offset=off,.len=strlen(lines[i]),.image=-1};}
 text=(html_text_t){.utf8=body,.len=strlen(body),.blocks=paragraphs,.count=sizeof(lines)/sizeof(lines[0])};
}
static void touch_book(const char *source){char p[1024];mapped(source,p);FILE *f=fopen(p,"wb");assert(f);fputs("original fixture",f);assert(!fclose(f));}
static void page(unsigned i,bool forward){unsigned count=io_count;book_excerpt_observe(&text,0,paragraphs[i].offset,i+1<text.count?paragraphs[i+1].offset:text.len,forward);assert(io_count==count);}
static void expect(const char *wanted){char out[BOOK_EXCERPT_CAP];assert(book_excerpt_load(book,out)&&!strcmp(out,wanted));}
static void disk_expect(const char *wanted){book_excerpt_reset();expect(wanted);book_excerpt_begin(book);}
static void fresh(void){
 book_excerpt_reset();char path[160];book_index_cache_header_t key;
 for(unsigned i=0;i<2;++i){assert(book_index_cache_prepare(book,s_tags[i],2,path,sizeof(path),&key));mapped_remove(path);}
 assert(book_index_cache_prepare(book,"quote",1,path,sizeof(path),&key));mapped_remove(path);
 book_excerpt_begin(book);
}
static void legacy(void){
 char path[160],tmp[176];book_index_cache_header_t key;assert(book_index_cache_prepare(book,"quote",1,path,sizeof(path),&key));
 legacy_excerpt_t value={0};strcpy(value.text,lines[0]);value.checksum=checksum(value.text);
 FILE *file=book_index_cache_open_write(path,&key,tmp,sizeof(tmp));assert(file&&book_index_cache_finish_write(file,tmp,path,fwrite(&value,1,sizeof(value),file)==sizeof(value)));
}
static void cadence(void){
 fresh();char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(book,out)&&!out[0]);
 unsigned before=writes;page(0,false);expect(lines[0]);assert(writes==before);assert(book_excerpt_flush(book));disk_expect(lines[0]);
 for(unsigned i=1;i<4;++i){page(i,true);expect(lines[0]);assert(book_excerpt_flush(book));book_excerpt_reset();book_excerpt_begin(book);page(i,false);expect(lines[0]);}
 page(4,true);expect(lines[4]);assert(book_excerpt_flush(book));disk_expect(lines[4]);
 for(unsigned i=0;i<10;++i){page(4,false);assert(book_excerpt_flush(book));expect(lines[4]);}
 page(3,false);page(4,true);page(3,false);page(4,true);expect(lines[4]);assert(s_value.pages==1);
 page(8,false);expect(lines[4]);assert(s_value.pages==0);
 // 跳转后的已读范围不能覆盖中间未显示的正文。/ A jump must not make skipped paragraphs eligible.
 for(unsigned i=0;i<4;++i){book_excerpt_observe(&text,i+1,paragraphs[8].offset,text.len,true);}
 expect(lines[8]);
 puts("PASS: four forward pages, cross-chapter cadence, persisted partial intervals, reopen/redraw stability and jump isolation");
}
static void reflow(void){
 fresh();
 book_excerpt_observe(&text,0,0,paragraphs[4].offset,false);expect(lines[3]);
 // 放大字号后，旧大页内的新分页仍须按实际翻页累计。/ Count actual turns after a large page reflows into smaller pages.
 book_excerpt_observe(&text,0,0,paragraphs[1].offset,false);expect(lines[3]);
 for(unsigned i=1;i<4;++i){
  page(i,true);expect(lines[3]);
  for(unsigned j=0;j<3;++j)page(i,false);
  assert(book_excerpt_flush(book));book_excerpt_begin(book);page(i,false);expect(lines[3]);
 }
 page(4,true);expect(lines[4]);
 // 缩小字号露出完整句子后，无书摘状态可立即选句。/ Expanding the displayed range can reveal the first complete sentence.
 fresh();book_excerpt_observe(&text,0,0,3,false);
 char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(book,out));
 book_excerpt_observe(&text,0,0,paragraphs[1].offset,false);expect(lines[0]);
 puts("PASS: reflow preserves real forward cadence across redraws/reopens and can reveal the first excerpt");
}
static void clipped_periods(void){
 char *const samples[]={"After waiting 3.14 seconds, they left.","He saved 12345.678 dollars."};
 for(unsigned blocks=0;blocks<2;++blocks)for(unsigned i=0;i<2;++i){
  fresh();char *body=samples[i];size_t len=strlen(body),split=(size_t)(strchr(body,'.')-body)+1;
  blk_t paragraph={.offset=0,.len=len,.image=-1};
  html_text_t value={.utf8=body,.len=len,.blocks=blocks?&paragraph:NULL,.count=blocks};
  book_excerpt_observe(&value,0,0,split,false);
  char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(book,out));
  book_excerpt_observe(&value,0,split,len,true);expect(body);
 }
 fresh();char spaced[]="A clear sentence ends here. More words follow.";
 html_text_t value={.utf8=spaced,.len=strlen(spaced)};
 book_excerpt_observe(&value,0,0,(size_t)(strchr(spaced,'.')-spaced)+1,false);expect("A clear sentence ends here.");
 // 没有后续字节的真实结尾仍有效，且不需要终止 NUL。/ A genuine text end remains valid without a trailing NUL.
 fresh();const char *ending="A clear sentence ends here.";size_t len=strlen(ending);char *exact=malloc(len);assert(exact);memcpy(exact,ending,len);
 value=(html_text_t){.utf8=exact,.len=len};book_excerpt_observe(&value,0,0,len,false);expect(ending);free(exact);
 fresh();char blocked[]="A clear sentence ends here.123";blk_t paragraph={.offset=0,.len=len,.image=-1};
 value=(html_text_t){.utf8=blocked,.len=strlen(blocked),.blocks=&paragraph,.count=1};
 book_excerpt_observe(&value,0,0,len,false);expect(ending);
 puts("PASS: clipped decimal periods stay incomplete while real sentence, paragraph and buffer endings remain eligible");
}
static void boundaries(void){
 fresh();
 char part[1024];snprintf(part,sizeof(part),"%s%s",lines[0],lines[1]);html_text_t value={.utf8=part,.len=strlen(part)};
 book_excerpt_observe(&value,0,0,strlen(lines[0]),false);expect(lines[0]);
 // 没有完整显示的句尾不能入选；中途跳入的句首残片也不能。/ Neither an unseen ending nor a mid-sentence landing can be selected.
 fresh();book_excerpt_observe(&value,0,3,strlen(lines[0]),false);char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(book,out));
 book_excerpt_observe(&value,0,strlen(lines[0]),value.len,true);expect(lines[1]);
 fresh();size_t split=strlen(lines[0])-3;
 book_excerpt_observe(&value,0,0,split,false);assert(!book_excerpt_load(book,out));
 book_excerpt_observe(&value,0,split,strlen(lines[0]),true);expect(lines[0]);
 fresh();page(0,false);
 char only[128];snprintf(only,sizeof(only),"%s",lines[0]);html_text_t duplicate={.utf8=only,.len=strlen(only)};
 for(unsigned i=1;i<=7;++i)book_excerpt_observe(&duplicate,i,0,duplicate.len,true);
 expect(lines[0]);assert(s_value.pages==BOOK_EXCERPT_INTERVAL);
 book_excerpt_observe(&text,8,paragraphs[4].offset,paragraphs[5].offset,true);expect(lines[4]);
 fresh();fixture();paragraphs[0].heading=true;paragraphs[1].auxiliary=true;paragraphs[2].linked=true;paragraphs[3].image=0;
 book_excerpt_observe(&text,0,0,paragraphs[5].offset,false);expect(lines[4]);fixture();
 fresh();char notices[1024];snprintf(notices,sizeof(notices),"Copyright all rights reserved.\n本书版权归原作者所有。\n%s",lines[1]);
 value=(html_text_t){.utf8=notices,.len=strlen(notices)};book_excerpt_observe(&value,0,0,value.len,false);expect(lines[1]);
 fresh();char invalid[]="\xc0\xaf" "broken ending.\nAn intact sentence.";value=(html_text_t){.utf8=invalid,.len=sizeof(invalid)-1};
 book_excerpt_observe(&value,0,0,value.len,false);expect("An intact sentence.");
 fresh();char english[]="too short. A much more readable sentence!";value=(html_text_t){.utf8=english,.len=strlen(english)};
 book_excerpt_observe(&value,0,0,value.len,false);expect("A much more readable sentence!");
 fresh();unsigned chapter=0;page(0,false);
 for(unsigned i=1;i<4;++i){
  value=(html_text_t){.utf8=(char*)lines[i],.len=strlen(lines[i])};
  for(unsigned j=0;j<4;++j)book_excerpt_observe(&value,++chapter,0,value.len,true);
  expect(lines[i]);
 }
 value=(html_text_t){.utf8=(char*)lines[0],.len=strlen(lines[0])};
 for(unsigned j=0;j<4;++j)book_excerpt_observe(&value,++chapter,0,value.len,true);
 expect(lines[3]);
 value=(html_text_t){.utf8=(char*)lines[4],.len=strlen(lines[4])};
 book_excerpt_observe(&value,++chapter,0,value.len,true);expect(lines[4]);
 value=(html_text_t){.utf8=(char*)lines[0],.len=strlen(lines[0])};
 for(unsigned j=0;j<4;++j)book_excerpt_observe(&value,++chapter,0,value.len,true);
 expect(lines[0]);
 fresh();char *large=malloc(34000);assert(large);
 strcpy(large,"原文原文原文原文原文原文原文原文。\n");size_t used=strlen(large);
 memset(large+used,'a',33000-used);strcpy(large+33000,"\n");strcat(large,lines[1]);
 value=(html_text_t){.utf8=large,.len=strlen(large)};book_excerpt_observe(&value,0,0,value.len,false);expect(lines[1]);free(large);
 puts("PASS: displayed-only sentences, spanning pages, mid-sentence jumps, filters, malformed UTF-8 and duplicate fallback");
}
static void storage(void){
 fresh();page(0,false);assert(book_excerpt_flush(book));for(unsigned i=1;i<=4;++i)page(i,true);expect(lines[4]);
 fail_write=true;assert(!book_excerpt_flush(book));fail_write=false;expect(lines[4]);assert(s_dirty);assert(book_excerpt_flush(book));disk_expect(lines[4]);
 for(unsigned i=5;i<=8;++i)page(i,true);expect(lines[8]);fail_rename=true;assert(!book_excerpt_flush(book));fail_rename=false;
 // 写入目标失败时，最近已校验副本仍然可读。/ A failed replacement retains the last checked copy.
 disk_expect(lines[4]);
 for(unsigned i=5;i<=8;++i)page(i,true);assert(book_excerpt_flush(book));expect(lines[8]);
 char path[160],mapped_path[1024];book_index_cache_header_t key;assert(book_index_cache_prepare(book,s_tags[s_slot],2,path,sizeof(path),&key));mapped(path,mapped_path);
 FILE *file=fopen(mapped_path,"r+b");assert(file);assert(!fseek(file,sizeof(key)+sizeof(uint32_t),SEEK_SET));fputc(0xff,file);fclose(file);disk_expect(lines[4]);
 fresh();legacy();book_excerpt_begin(book);expect(lines[0]);page(0,false);for(unsigned i=1;i<=4;++i)page(i,true);assert(book_excerpt_flush(book));disk_expect(lines[4]);
 const char *other="/sdcard/other.txt";touch_book(other);char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(other,out));
 book_excerpt_begin(other);page(0,false);assert(book_excerpt_flush(other));book_excerpt_reset();assert(book_excerpt_load(other,out)&&!strcmp(out,lines[0]));expect(lines[4]);
 book_excerpt_begin(book);page(5,true);char actual[1024];mapped(book,actual);file=fopen(actual,"ab");assert(file);fputs("changed",file);fclose(file);
 assert(!book_excerpt_load(book,out)&&!out[0]&&!book_excerpt_flush(book));book_excerpt_reset();assert(!book_excerpt_load(book,out));
 book_excerpt_begin("/flash/book.txt");page(0,false);assert(!book_excerpt_load(NULL,out));assert(!book_excerpt_load(book,NULL));
 puts("PASS: deferred IO, alternate-copy write/rename/corruption recovery, legacy migration, book isolation and changed-source invalidation");
}
int main(int argc,char **argv){assert(argc==2);root=argv[1];touch_book(book);fixture();cadence();reflow();clipped_periods();boundaries();storage();}
'''

BOOK = ROOT / "main/apps/app_book.c"
present = function("present", BOOK)
assert present.index("reader_present_output") < present.index("reader_prefetch_end") < present.index("reader_excerpt_presented")
save = function("save_progress", BOOK)
assert save.index("return;") < save.index("book_excerpt_flush") < save.index("pending_find")
release = function("free_book", BOOK)
assert release.index("book_excerpt_flush") < release.index("book_excerpt_reset") < release.index("reader_session_close")
lost = function("book_on_media_lost", BOOK)
assert lost.index("book_excerpt_reset") < lost.index("save_progress") < lost.index("free_book")
GATE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "html_text.h"
typedef struct {int x,y,width,height;} EpdRect;
enum {EPD_DRAW_SUCCESS,EPD_DRAW_FAILED,READING,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,
      APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_AREA};
static struct {struct {size_t chapter,page;} position;} s_reader_session={.position={2,1}};
static html_text_t content;
static const html_text_t *reader_session_content(const void *session){assert(session==&s_reader_session);return &content;}
static bool s_excerpt_forward,tools_band,s_toolbar,s_clear_confirm;
static struct {bool active;} s_dictionary_selection;
static struct {bool open;} s_dictionary_view;
static char s_reader_notice[64];
static int64_t s_dictionary_notice_until;
static int err,s_view,s_reader_panel,redraw,images,fallback_image;
static const char *s_text="body";
static size_t s_jump_page=1,s_jump_offset=SIZE_MAX;
static EpdRect s_area;
static const struct {EpdRect body_area;} output_request={.body_area={10,20,100,200}};
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static int book_layout_page_image_count(size_t page){assert(page==1);return images;}
static int book_layout_page_image(size_t page){assert(page==1);return fallback_image;}
static unsigned observations;
static bool observed_forward;
static void book_excerpt_observe(const html_text_t *text,size_t chapter,size_t begin,size_t end,bool forward){
 assert(text==&content&&chapter==2&&begin==(s_jump_offset==SIZE_MAX?100:s_jump_offset)&&end==200);
 observed_forward=forward;++observations;
}
'''
GATE += function("reader_page_offset", BOOK) + "\n" + function("reader_excerpt_presented", BOOK)
GATE += "\n" + function("reader_image_slots", BOOK)
GATE += "\nstatic void confirmed(void){\n" + present[present.index("    const bool reader_frame_ok"):present.index("    s_reader_text_frame =")] + "}\n"
GATE += r'''
int main(void){
 for(int scenario=0;scenario<22;++scenario){
  err=EPD_DRAW_SUCCESS;s_view=READING;s_text="body";s_reader_panel=READER_PANEL_NONE;
  s_dictionary_selection.active=s_dictionary_view.open=tools_band=s_toolbar=s_clear_confirm=false;
  images=0;fallback_image=-1;redraw=APP_REDRAW_PAGE;s_area=output_request.body_area;s_excerpt_forward=true;
  s_reader_notice[0]=0;s_dictionary_notice_until=0;
  switch(scenario){
   case 1:err=EPD_DRAW_FAILED;break;case 2:s_view=SHELF;break;case 3:s_text=NULL;break;
   case 4:s_reader_panel=READER_PANEL_TOOLS;break;case 5:s_toolbar=true;break;
   case 6:s_clear_confirm=true;break;case 7:s_dictionary_selection.active=true;break;
   case 8:s_dictionary_view.open=true;break;case 9:images=1;break;case 10:tools_band=true;break;
   case 11:redraw=APP_REDRAW_AREA;s_area.height=10;break;
   case 12:redraw=APP_REDRAW_AREA;break;case 13:redraw=APP_REDRAW_FULL;break;
   case 14:s_excerpt_forward=false;s_jump_offset=120;break;
   case 15:redraw=APP_REDRAW_AREA;s_area.x++;break;
   case 16:redraw=APP_REDRAW_AREA;s_area.y++;break;
   case 17:strcpy(s_reader_notice,"bookmark saved");break;
   case 18:s_dictionary_notice_until=2400;break;
   case 19:strcpy(s_reader_notice,"bookmark saved");s_dictionary_notice_until=2400;break;
   case 20:break;
   case 21:fallback_image=0;break;
  }
  assert(reader_image_slots(1)==(images?images:fallback_image>=0?1:0));
  unsigned before=observations;confirmed();
  bool eligible=scenario==0||(scenario>=12&&scenario<=14)||scenario==20;
  assert(observations-before==(unsigned)eligible&&!s_excerpt_forward);
  if(eligible)assert(observed_forward==(scenario!=14));
 }
 puts("PASS: actual output gate rejects errors, overlays, images and partial regions; prefetch joins before observation and saves precede release");
}
'''

NAV = UNIT[:UNIT.index("int main(int argc")]
NAV += r'''
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,TOC,SHELF,READER_PANEL_NONE};
static struct {struct {size_t chapter,page;} position;struct {char chapter_title[64];} book;} s_reader_session;
static size_t s_jump_page,s_jump_offset=SIZE_MAX,s_selected_toc;
static bool s_excerpt_forward,load_failed,empty_landing;
static int s_reader_panel,s_view;
static const char *s_text="body";
static char s_message[64];
static size_t book_layout_page_start_offset(size_t page){return empty_landing?0:page<text.count?paragraphs[page].offset:text.len;}
static size_t book_chapter_count(void){return 10;}
static size_t book_navigation_chapter(size_t target){return target;}
static const char *book_navigation_anchor(size_t target){(void)target;return NULL;}
static size_t book_navigation_source_offset(size_t target){(void)target;return SIZE_MAX;}
#define ESP_OK 0
static int book_navigation_title(size_t target,char *out,size_t cap){(void)target;snprintf(out,cap,"chapter");return ESP_OK;}
static void copy_text(char *out,size_t cap,const char *in){snprintf(out,cap,"%s",in);}
static void set_reader_view(int view){s_view=view;}
static void save_progress(void){assert(book_excerpt_flush(book));}
static bool load_chapter_at(app_ctx_t *ctx,size_t chapter,size_t offset,bool last,const char *anchor,size_t source){
 (void)ctx;(void)last;(void)anchor;(void)source;if(load_failed)return false;
 s_reader_session.position.chapter=chapter;s_reader_session.position.page=offset;s_jump_offset=SIZE_MAX;return true;
}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,size_t offset,bool last){return load_chapter_at(ctx,chapter,offset,last,NULL,SIZE_MAX);}
typedef struct {size_t chapter,byte_off;} reader_bookmark_entry_t;
'''
NAV += function("reader_page_offset", BOOK) + "\n" + function("reader_excerpt_reposition", BOOK)
toc = function("action_at", BOOK)
toc = toc[toc.index("        if (target >= 0 && !s_message[0]) {"):toc.index("    if (s_view == MANAGE)")]
NAV += "\nstatic app_redraw_t toc_jump(app_ctx_t *ctx,int target){\n" + toc[:toc.rfind("    }")] + "}\n"
panel = function("reader_panel_action", BOOK)
start = panel.index("                    else if (mark.chapter < book_chapter_count()")
NAV += "\nstatic void bookmark_jump(app_ctx_t *ctx,reader_bookmark_entry_t mark){if(false){}\n"
NAV += panel[start:panel.index("\n                }", start)] + "\n}\n"
NAV += r'''
int main(int argc,char **argv){
 assert(argc==2);root=argv[1];touch_book(book);fixture();app_ctx_t ctx={0};
 for(unsigned bookmark=0;bookmark<2;++bookmark)for(unsigned reopen=0;reopen<2;++reopen){
  fresh();page(0,false);for(unsigned i=1;i<=3;++i)page(i,true);assert(s_value.pages==3);
  s_excerpt_forward=true;unsigned before=io_count;
  if(bookmark)bookmark_jump(&ctx,(reader_bookmark_entry_t){.chapter=2,.byte_off=0});
  else assert(toc_jump(&ctx,2)==APP_REDRAW_PAGE);
  assert(io_count>before&&!s_excerpt_forward);expect(lines[0]);
  assert(s_value.pages==0&&!s_visible&&s_value.chapter==2);
  if(reopen){book_excerpt_begin(book);expect(lines[0]);assert(!s_visible&&s_value.pages==0);}
  // 跳到图页后不观察落点，后续必须重新前进四个合格正文页。/ Rejected image landings never inherit the old three-page interval.
  for(unsigned i=1;i<=4;++i){
   unsigned count=io_count;book_excerpt_observe(&text,2,paragraphs[i].offset,book_layout_page_start_offset(i+1),true);
   assert(io_count==count);expect(lines[i<4?0:4]);
  }
 }
 for(unsigned bookmark=0;bookmark<2;++bookmark){
  fresh();page(0,false);for(unsigned i=1;i<=3;++i)page(i,true);load_failed=true;
  if(bookmark)bookmark_jump(&ctx,(reader_bookmark_entry_t){.chapter=2,.byte_off=0});else (void)toc_jump(&ctx,2);
  assert(s_value.pages==3);load_failed=false;
 }
 fresh();page(0,false);for(unsigned i=1;i<=3;++i)page(i,true);
 s_reader_session.position.chapter=0;s_reader_session.position.page=1;
 unsigned before=io_count;reader_excerpt_reposition();assert(io_count==before&&s_value.pages==0&&!s_visible);
 assert(book_excerpt_flush(book));book_excerpt_begin(book);expect(lines[0]);
 for(unsigned i=2;i<=5;++i){book_excerpt_observe(&text,0,paragraphs[i].offset,book_layout_page_start_offset(i+1),true);expect(lines[i<5?0:5]);}
 // 折叠全部标题的成功落点可有空范围，也必须保存重新累计的起点。/ A title-only landing has an empty range and must still restart cadence durably.
 fresh();page(0,false);for(unsigned i=1;i<=3;++i)page(i,true);
 empty_landing=true;assert(toc_jump(&ctx,2)==APP_REDRAW_PAGE);empty_landing=false;
 assert(s_value.chapter==2&&s_value.pages==0&&!s_visible);book_excerpt_begin(book);
 expect(lines[0]);assert(s_value.chapter==2&&s_value.pages==0);
 for(unsigned i=1;i<=4;++i){book_excerpt_observe(&text,3,paragraphs[i].offset,book_layout_page_start_offset(i+1),true);expect(lines[i<4?0:4]);}
 // 无旧句时，导航锚点本身不代表显示，首次真实显示仍可摘句。/ An anchor never selects; the first actual frame may select without a prior quote.
 fresh();before=io_count;book_excerpt_reposition(0,0,paragraphs[1].offset);assert(io_count==before);
 char out[BOOK_EXCERPT_CAP];assert(!book_excerpt_load(book,out));
 assert(book_excerpt_flush(book));book_excerpt_begin(book);page(0,false);expect(lines[0]);
 assert(s_dirty&&book_excerpt_flush(book));disk_expect(lines[0]);
 puts("PASS: actual TOC/bookmark jumps and reverse anchors reset cadence across rejected landings/reopens; failed loads preserve cadence");
}
'''

with tempfile.TemporaryDirectory(prefix="book-excerpt-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    source, binary = work / "test.c", work / "test"
    source.write_text(UNIT)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                              "-I" + str(ROOT / "main/book"), "-I" + str(ROOT / "tools/html_text_stubs"),
                              str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary), str(work)], env=env, check=True)
    source.write_text(GATE)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                              "-I" + str(ROOT / "main/book"), "-I" + str(ROOT / "tools/html_text_stubs"),
                              str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
    source.write_text(NAV)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                              "-I" + str(ROOT / "main/book"), "-I" + str(ROOT / "tools/html_text_stubs"),
                              str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary), str(work)], env=env, check=True)
    subprocess.run(["python3", str(ROOT / "tools/test_home_recent.py")], env=env, check=True)
