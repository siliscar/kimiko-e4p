#!/usr/bin/env python3
"""中文：四向重力、触点、真实分页及方向事务的宿主回归。
English: Host regressions for four-way gravity, touch, real pagination and orientation transactions.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "main/apps/app_book.c"

policy = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "reader_orientation.h"
#include "epdiy.h"
static enum EpdRotation rotation = EPD_ROT_INVERTED_PORTRAIT;
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return rotation;}
void epd_set_rotation(enum EpdRotation value){rotation=value;}
#define APP_TOUCH_MAX_POINTS 2
typedef struct {bool active;uint16_t x,y;} point_t;
typedef struct {bool touched;uint16_t x,y;point_t points[2];uint8_t raw[16];} app_touch_t;
'''
policy += function("app_input_rotate_touch", ROOT / "main/app/app_input.c")
policy += r'''
static void physical(int x,int y,int *px,int *py){
 switch(rotation){case 0:*px=x;*py=y;break;case 1:*px=1215-y;*py=x;break;
 case 2:*px=1215-x;*py=683-y;break;default:*px=y;*py=683-x;break;}
}
static bool stable(reader_orientation_t *s,int x,int y,int z,int64_t t,display_orientation_t *next){
 for(int dt=0;dt<500;dt+=50)assert(!reader_orientation_feed(s,x,y,z,false,t+dt,next));
 return reader_orientation_feed(s,x,y,z,false,t+500,next);
}
int main(void){
 for(int d=0;d<4;++d){
  display_orientation_set(d);assert(display_orientation_get()==(display_orientation_t)d);
  // 每个屏内像素都必须落到原竖屏触点对应的同一物理像素。
  // Every on-panel coordinate must hit the same physical pixel as the native portrait contact.
  for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
   uint16_t xx=x,yy=y;display_orientation_touch(&xx,&yy);int px,py;physical(xx,yy,&px,&py);
   assert(px==y&&py==683-x);
  }
  app_touch_t touch={.touched=true,.x=15,.y=34,.points={{true,15,34},{true,650,1100}}};
  memset(touch.raw,0xa6,sizeof(touch.raw));uint16_t x=650,y=1100;display_orientation_touch(&x,&y);
  app_input_rotate_touch(&touch);assert(touch.x==touch.points[0].x&&touch.y==touch.points[0].y);
  assert(touch.points[1].x==x&&touch.points[1].y==y);
  for(unsigned i=0;i<sizeof(touch.raw);++i)assert(touch.raw[i]==0xa6);
  touch=(app_touch_t){.touched=true,.x=240,.y=1500,.points={{true,240,1500},{false,12,13}}};
  app_input_rotate_touch(&touch);assert(touch.x==240&&touch.y==1500&&touch.points[0].y==1500);
  assert(touch.points[1].x==12&&touch.points[1].y==13);
 }
 reader_orientation_t state={0};display_orientation_t next=DISPLAY_ORIENTATION_PORTRAIT;
 assert(!stable(&state,-1000,0,0,1000,&next));
 state.automatic=true;
 assert(stable(&state,-1000,0,0,2000,&next)&&next==DISPLAY_ORIENTATION_RIGHT);state.locked=next;
 assert(!stable(&state,-1000,0,0,3000,&next));
 assert(stable(&state,0,1000,0,4000,&next)&&next==DISPLAY_ORIENTATION_INVERTED);state.locked=next;
 assert(stable(&state,1000,0,0,5000,&next)&&next==DISPLAY_ORIENTATION_LEFT);state.locked=next;
 assert(stable(&state,0,-1000,0,6000,&next)&&next==DISPLAY_ORIENTATION_PORTRAIT);state.locked=next;
 const int bad[][3]={{0,0,1000},{700,700,0},{500,0,0},{1400,0,0},{1000,0,900},{0,0,-1000}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(!stable(&state,bad[i][0],bad[i][1],bad[i][2],7000+i*1000,&next));
 for(int dt=0;dt<=1000;dt+=50)assert(!reader_orientation_feed(&state,dt%100?-1100:-800,0,0,false,14000+dt,&next));
 assert(!reader_orientation_feed(&state,-1000,0,0,false,16000,&next));
 assert(!reader_orientation_feed(&state,-1000,0,0,true,16400,&next)&&!state.candidate);
 assert(!reader_orientation_feed(&state,-1000,0,0,false,17000,&next));
 assert(!reader_orientation_feed(&state,-1000,0,0,false,17600,&next));
 assert(!reader_orientation_feed(&state,-1000,0,0,false,17500,&next));
 reader_orientation_cancel(&state);assert(stable(&state,-1000,0,0,18000,&next));
 state.automatic=false;assert(!stable(&state,1000,0,0,19000,&next));
 puts("PASS: exhaustive four-way touch/pixel agreement, multipoint/raw/cover keys, stable gravity, flat/diagonal/motion/touch/gap/clock rejection and lock");
}
'''

# 复用假字体，但方向事务、会话与分页器均为真实实现。
# Reuse the fake font while exercising the real orientation transaction, session and paginator.
layout = (ROOT / "tools/book_layout_host_test.c").read_text().split("static void session_layout_lifecycle(void) {")[0]
layout += r'''
#include "reader_orientation.h"
#define UI_BAR_H 96
#define UI_BAR_MARGIN 24
#define UI_LOCK_WIDTH 684
#define READER_WIDTH (display_orientation_get()%2?1216:684)
#define READER_HEIGHT (display_orientation_get()%2?684:1216)
#define READER_BAR_TOP (READER_HEIGHT-UI_BAR_MARGIN-UI_BAR_H)
#define READER_FULLSCREEN_PROGRESS_TOP (READER_HEIGHT-12)
#define READER_FOOTER_PROGRESS_SPACE 19
enum {MARGIN_LEFT,MARGIN_RIGHT,MARGIN_TOP,MARGIN_BOTTOM,MARGIN_COUNT};
enum {READING,SHELF,TOC};
enum {READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_SETTINGS};
typedef struct {int unused;} book_shake_gate_t;
typedef struct {void *acc;uint8_t *fb;} app_ctx_t;
static reader_session_t s_reader_session;
static reader_orientation_t s_orientation;
static display_orientation_t direction;
display_orientation_t display_orientation_get(void){return direction;}
void display_orientation_set(display_orientation_t next){direction=next;}
static int64_t s_orientation_retry_ms;
static book_shake_gate_t s_shake_gate;
static const char *s_text;
static size_t s_jump_offset=SIZE_MAX,s_jump_page,s_selected_toc;
static bool s_reader_fullscreen,s_reader_progress_bar=true,s_clear_confirm;
static bool s_reader_chrome_fp_valid,s_reader_footer_pending,s_reader_image_refresh_pending;
static bool s_reader_footer_strong,s_water_turn_pending,s_reader_turn_pending;
static bool s_reader_text_frame,s_reader_stable_frame,s_excerpt_forward;
static int s_chrome_height[2]={80,57},s_margin[4]={36,36,0,0},s_px=36;
static int s_view=READING,s_reader_panel=READER_PANEL_NONE;
static int s_chapter_lead_skip,s_chapter_lead_height,s_chapter_lead_width;
static char s_chapter_heading_title[128],s_chapter_heading_label[128];
static bool statusbar=true,background;
static int locks,invalidates,saves,releases,prepared,rendered,build_attempt,fail_mask;
static bool app_settings_reader_statusbar(void){return statusbar;}
static int app_settings_book_tracking(void){return 0;}
static const char *app_settings_book_background_path(void){return background?"image":"";}
static bool ui_reader_background_prepare(const char *path){assert(path);return background;}
static int64_t esp_timer_get_time(void){return 123000000;}
static void lock_draw(void){assert(!locks);++locks;}
static void unlock_draw(void){assert(locks==1);--locks;test_zero_width=false;}
static void invalidate_prep(void){++invalidates;}
static void release_page_images(void){++releases;}
static void release_warm_images(void){}
static void prepare_inline_image(void){++prepared;}
static void save_progress(void){++saves;}
static void refresh_chapter_lead(void){bool fail=build_attempt<2&&(fail_mask&(1u<<build_attempt));++build_attempt;book_layout_set_chapter_lead(0,fail?10000:0);}
static void render(app_ctx_t *ctx,uint8_t *fb){assert(ctx->fb==fb);++rendered;}
static void dictionary_cancel(void){}
static int book_excerpt_flush(const char *path){(void)path;return 0;}
static void book_excerpt_reset(void){}
static void flush_ticket_stats(void){}
static bool app_reading_journal_flush(void){return true;}
static void e0470_page_turn_release(void){}
typedef struct {bool dirty;}pending_progress_t;
static pending_progress_t *pending_find(const char *path){(void)path;return NULL;}
static void pending_discard(const char *path){(void)path;}
typedef struct {int unused;}reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;
static void reader_bind_chapter(void){s_text=s_reader_session.content.utf8;}
static void app_font_activate_system(void){}
'''
for name in ("reader_footer_height", "body_rect_for_tracking", "body_rect", "reader_area",
             "reader_status_area", "progress_rect", "reader_tools_area", "reader_page_offset",
             "free_book", "reader_reflow_build", "reader_orientation_apply", "reader_orientation_portrait",
             "reader_orientation_sync", "rotation_available", "rotation_enabled", "on_rotation_toggle", "on_system_overlay"):
    layout += function(name, BOOK) + "\n"
layout += r'''
static char text[24001];
static void open_book(void){
 assert(reader_session_open(&s_reader_session,"orientation.txt")==ESP_OK);
 html_text_t chapter={.utf8=strdup(text),.len=strlen(text)};
 assert(reader_session_build_layout(&s_reader_session,&chapter,body_rect(),s_px));
 reader_session_adopt_chapter(&s_reader_session,&chapter);reader_bind_chapter();s_view=READING;s_reader_panel=READER_PANEL_NONE;
 s_reader_session.position.page=2;s_jump_offset=SIZE_MAX;fail_mask=build_attempt=0;
}
static void in_screen(EpdRect r){assert(r.x>=0&&r.y>=0&&r.width>0&&r.height>0&&r.x+r.width<=READER_WIDTH&&r.y+r.height<=READER_HEIGHT);}
int main(void){
 memset(text,'a',sizeof(text)-1);uint8_t fb=0;app_ctx_t ctx={.acc=&fb,.fb=&fb};open_book();
 assert(rotation_available(&ctx)&&!rotation_enabled(&ctx));on_rotation_toggle(&ctx);assert(rotation_enabled(&ctx));
 size_t anchor=reader_page_offset(s_reader_session.position.page)+11;s_jump_offset=anchor;s_jump_page=s_reader_session.position.page;
 // 多次换向及临时系统竖屏往返不漂移字节锚点，覆盖横排/竖排、全屏/窗口及状态栏。
 // Repeated rotation and temporary system portrait preserve the byte anchor across writing modes, fullscreen and status-bar modes.
 for(int vertical=0;vertical<2;++vertical)for(int full=0;full<2;++full)for(int status=0;status<2;++status){
  book_layout_set_vertical(vertical);s_reader_fullscreen=full;statusbar=status;
  for(int d=1;d<=4;++d){
   display_orientation_t next=(display_orientation_t)(d%4);assert(reader_orientation_apply(next));s_orientation.locked=next;
   assert(!locks&&reader_page_offset(s_reader_session.position.page)==anchor);
   size_t page=s_reader_session.position.page;assert(book_layout_page_start_offset(page)<=anchor&&book_layout_page_start_offset(page+1)>anchor);
   in_screen(body_rect());in_screen(reader_area());in_screen(reader_status_area());in_screen(progress_rect());in_screen(reader_tools_area());
   if(next){int before=rendered;on_system_overlay(&ctx);assert(direction==0&&rendered==before+1&&s_orientation.locked==next&&s_orientation.automatic);
    assert(reader_orientation_sync()&&direction==next&&reader_page_offset(s_reader_session.position.page)==anchor);}
  }
 }
 s_reader_fullscreen=false;statusbar=true;book_layout_set_vertical(false);
 assert(reader_orientation_apply(DISPLAY_ORIENTATION_RIGHT));s_orientation.locked=DISPLAY_ORIENTATION_RIGHT;
 on_rotation_toggle(&ctx);assert(!s_orientation.automatic&&s_orientation.locked==DISPLAY_ORIENTATION_RIGHT);
 s_reader_panel=READER_PANEL_SETTINGS;assert(!rotation_available(&ctx)&&reader_orientation_sync()&&direction==0);
 s_reader_panel=READER_PANEL_NONE;assert(reader_orientation_sync()&&direction==1);
 int before=saves;fail_mask=1;build_attempt=0;
 assert(!reader_orientation_apply(DISPLAY_ORIENTATION_LEFT));assert(direction==1&&saves==before&&!locks);
 assert(reader_page_offset(s_reader_session.position.page)==anchor&&s_orientation_retry_ms==125000);
 // 竖屏目标失败但原布局恢复：系统入口仍必须清理到竖屏；双失败也不能留下无效借用。
 // If portrait fails but the old layout restores, the system boundary still closes in portrait; double failures must also release invalid borrows.
 fail_mask=1;build_attempt=0;on_system_overlay(&ctx);assert(direction==0&&s_view==SHELF&&!s_text&&!s_orientation.automatic);
 open_book();s_orientation.automatic=true;fail_mask=3;build_attempt=0;
 assert(reader_orientation_apply(DISPLAY_ORIENTATION_RIGHT));assert(direction==0&&s_view==SHELF&&!s_text&&!s_reader_session.layout_owned&&!s_orientation.automatic);
 fail_mask=0;open_book();assert(reader_orientation_apply(DISPLAY_ORIENTATION_LEFT));s_orientation.automatic=true;
 free_book();assert(direction==0&&!s_text&&!s_reader_session.layout_owned&&!s_orientation.automatic&&!s_orientation_retry_ms&&!locks);
 assert(invalidates>10&&releases>10&&prepared>10&&saves>10);
 puts("PASS: real reader/session/paginator preserve text anchors through four orientations, chrome bounds, system round trips, manual lock, rollback, double failure and close cleanup");
}
'''

with tempfile.TemporaryDirectory(prefix="reader-orientation-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    stub = work / "display"
    stub.mkdir()
    (stub / "epdiy.h").write_text("#pragma once\nenum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};\nint epd_width(void);\nint epd_height(void);\nenum EpdRotation epd_get_rotation(void);\nvoid epd_set_rotation(enum EpdRotation);\n")
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
             "-g", "-fsanitize=address,undefined", "-Imain/book", "-Imain/display"]
    for name, source, extra in (
        ("policy", policy, ["-I" + str(stub), "main/display/display_orientation.c", "main/book/reader_orientation.c"]),
        ("layout", layout, ["-Itools/book_layout_stubs", "-Itools/book_source_host_stubs", "main/book/reader_session.c",
                            "main/book/book_layout.c", "main/book/html_text.c", "main/book/reader_orientation.c"]),
    ):
        path, binary = work / f"{name}.c", work / name
        path.write_text(source)
        subprocess.run(compiler + flags + [str(path)] + extra + ["-o", str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
