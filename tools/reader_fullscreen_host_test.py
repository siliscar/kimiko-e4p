"""真实翻页与全屏重排回归，确保不跨过未读文字。/ Actual turns/reflow must not skip unread text.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;int leaf;bool user_activity;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,TOC,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS};
#define BOOK_TOC_ROWS 7
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 96
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
static int s_margin[MARGIN_COUNT], s_line_spacing;
#define MODE_GL16 1
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define ESP_LOGI(...) ((void)0)
/* F4 触摸诊断埋点与题头重算：本测试不验证其行为，桩保持调用面即可。
   F4 touch-diag logging and the heading refresh are stubbed; this test never asserts them. */
#define TD_LOGI(...) ((void)0)
#define TD_TIME(tag, expr) ((void)(tag), (expr))
static char s_requested_open[288];
static bool s_requested_fullscreen=true,s_requested_open_home;
static const char *s_text="text";
static size_t s_text_len=900,s_block_count,s_jump_offset;
static void *s_blocks;
static int s_px=48,s_reader_panel=READER_PANEL_NONE,s_reader_slider=-1,s_view=READING;
#define s_toolbar (s_reader_panel!=READER_PANEL_NONE)
static bool s_reader_fullscreen,s_clear_confirm,s_reader_text_frame,s_reader_turn_pending;
static bool s_reader_stable_frame,s_reader_footer_strong;
static bool s_water_turn_pending,s_reader_cleanup,draw_locked,load_failed;
static int s_water_turn_dir;
static unsigned s_stats_pending_turns,s_session_turns,s_turns,s_unsaved,journal_turns;
static void app_reading_journal_record(const char *path,const char *title,uint32_t size,unsigned chapter,uint32_t ms,uint32_t turns){
 assert(!strcmp(path,"/book.epub")&&!strcmp(title,"Book")&&size==500&&chapter==s_chapter&&!ms&&turns==1);journal_turns+=turns;
}
static int64_t s_last_turn_ms,s_stats_activity_ms;
static bool s_save_failed;
static char s_message[128];
static unsigned layout_span=100,reflows,paints,saves,failed_reflows;
static uint8_t full_pages;
static const char *app_settings_book_background_path(void){return "";}
static bool test_open_full=true;
static bool app_settings_reader_open_fullscreen(void){return test_open_full;}
static void app_settings_set_reader_open_fullscreen(bool on){test_open_full=on;}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static void lock_draw(void){assert(!draw_locked);draw_locked=true;}
static void unlock_draw(void){assert(draw_locked);draw_locked=false;}
static void invalidate_prep(void){}
static bool book_file_path_supported(const char *path){return path&&*path;}
// 合并后的 toggle/turn 正文会调题头重算；本测试不验证它，桩为空并把依赖的全局补齐。
// The merged toggle/turn bodies call the heading refresh; this test never asserts it, so stub
// it out and provide the globals the body touches.
static void refresh_chapter_lead(void){}
static EpdRect body_rect(void){return(EpdRect){0,0,684,s_reader_fullscreen?150:100};}
static size_t book_layout_page_start_offset(size_t page){return page*layout_span;}
static size_t book_layout_page_for_offset(size_t off){return off/layout_span;}
static size_t book_layout_page_count(void){return (s_text_len+layout_span-1)/layout_span;}
static bool book_layout_build_blocks(const char *text,size_t len,const void *blocks,size_t count,EpdRect r,int px){
 (void)text;(void)len;(void)blocks;(void)count;(void)px;assert(draw_locked);++reflows;
 if(failed_reflows){--failed_reflows;return false;}layout_span=r.height;return true;
}
static void release_page_images(void){}
static void prepare_inline_image(void){}
static void save_progress(void){++saves;}
static void free_book(void){assert(!draw_locked);s_text=NULL;}
static size_t book_chapter_count(void){return 2;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,int off,bool last){(void)ctx;(void)off;if(load_failed)return false;s_chapter=chapter;s_page=last?book_layout_page_count()-1:0;return true;}
static void set_reader_view(int view){s_view=view;}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static uint8_t app_settings_reader_full_pages(void){return full_pages;}
static int effect=1;
static int app_settings_reader_turn_effect(void){return effect;}
static int test_images;
static int book_layout_page_image_count(size_t page){(void)page;return test_images;}
static app_redraw_t paint_reading(app_ctx_t *ctx,int mode){(void)ctx;assert(mode==MODE_GL16);++paints;return APP_REDRAW_AREA;}
// 优化①在翻页时给了 15s 高压保持提示；本测试不验证电源行为，桩保持调用面即可。
// Optimization ① hints a 15 s HV hold on each turn; this test never asserts power, so stub the call.
static void epd_power_hold_hint(uint32_t ms){(void)ms;}
'''
unit += r"""
static int indent=2,adjustment,layout_indent=2,layout_adjustment;
static int app_settings_book_indent(void){return indent;}
static int app_settings_book_indent_adjust(void){return adjustment;}
static void app_settings_set_book_indent(uint8_t em){indent=em;}
static void app_settings_set_book_indent_adjust(int8_t px){adjustment=px;}
static void book_layout_set_first_line_indent(unsigned em){assert(draw_locked);layout_indent=em;}
static void book_layout_set_first_line_indent_adjust(int px){assert(draw_locked);layout_adjustment=px;}
"""
unit += f'\n#include "{ROOT / "main/book/reader_turn.c"}"\n'
unit += function('reader_turn_save', BOOK) + '\n'
unit += function('reader_turn_load', BOOK) + '\n'
from reader_reflow_test_code import reflow_code
unit += reflow_code("indent", "fullscreen")
unit += function('apply_reader_indent', BOOK) + '\n'
for name in ('app_book_request_open', 'app_book_request_open_from_home', 'app_book_request_resume',
             'toggle_reader_fullscreen', 'reader_turn_chrome', 'turn_page'):
    if name == 'turn_page':
        unit += '#define turn_page actual_turn_page\n' + function(name, BOOK) + '\n#undef turn_page\n'
        unit += 'static app_redraw_t turn_page(app_ctx_t *ctx,int dir){app_redraw_t result=actual_turn_page(ctx,dir);assert(journal_turns==s_stats_pending_turns);return result;}\n'
    else:
        unit += function(name, BOOK) + '\n'
unit += r'''
static void reset(bool full,bool tools,size_t page,size_t chapter){
 s_text="text";s_view=READING;s_reader_fullscreen=full;layout_span=full?150:100;s_page=page;s_chapter=chapter;
 strcpy(s_reader_session.book.path,"/book.epub");strcpy(s_reader_session.book.title,"Book");s_reader_session.book.file_size=500;journal_turns=0;
 s_reader_panel=tools?READER_PANEL_TOOLS:READER_PANEL_NONE;s_reader_slider=2;
 reflows=paints=saves=s_stats_pending_turns=s_session_turns=s_turns=s_unsaved=0;
 failed_reflows=0;load_failed=false;full_pages=0;s_reader_text_frame=s_reader_stable_frame=true;
 s_reader_turn_pending=s_water_turn_pending=s_reader_cleanup=false;s_jump_offset=0;
}
int main(void){
 app_ctx_t ctx={.now_ms=1000};
 reset(true,false,2,0);size_t anchor=book_layout_page_start_offset(s_page);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_PAGE&&adjustment==-7&&layout_adjustment==-7&&indent==2);
 assert(book_layout_page_start_offset(s_page)==anchor&&saves==1);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_NONE&&saves==1);
 failed_reflows=1;
 assert(apply_reader_indent(&ctx,3,12)==APP_REDRAW_NONE&&indent==2&&adjustment==-7&&layout_indent==2&&layout_adjustment==-7&&saves==1);
 assert(book_layout_page_start_offset(s_page)==anchor&&!draw_locked);
 assert(apply_reader_indent(&ctx,2,101)==APP_REDRAW_NONE&&saves==1);

 assert(app_book_request_open("/sdcard/book.epub")&&s_requested_fullscreen&&!s_requested_open_home);
 assert(app_book_request_resume("/sdcard/book.epub",false)&&!s_requested_fullscreen&&s_requested_open_home);
 assert(app_book_request_open_from_home("/sdcard/other.epub")&&s_requested_fullscreen);
 assert(!app_book_request_open(NULL)&&!app_book_request_open(""));
 // Shelf entries follow the remembered preference; toggling persists it immediately.
 test_open_full=false;
 assert(app_book_request_open("/sdcard/book.epub")&&!s_requested_fullscreen&&!s_requested_open_home);
 assert(app_book_request_open_from_home("/sdcard/other.epub")&&!s_requested_fullscreen);
 test_open_full=true;
 assert(app_book_request_open("/sdcard/book.epub")&&s_requested_fullscreen);
 reset(false,true,2,0);size_t fs_anchor=book_layout_page_start_offset(s_page);
 assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_PAGE&&s_reader_fullscreen&&test_open_full);
 assert(book_layout_page_start_offset(s_page)<=fs_anchor&&fs_anchor<book_layout_page_start_offset(s_page)+layout_span&&saves==1);
 reset(true,false,2,0);assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_PAGE&&!s_reader_fullscreen&&!test_open_full);
 // 书架点击不经过 request_open，只读 s_requested_fullscreen；切换全屏后这个待开值必须跟着变，
 // 否则关书再进来还是切换前的模式。
 // A shelf tap skips the open request path and reads s_requested_fullscreen alone, so a toggle
 // has to move it too; otherwise reopening after closing the book restores the pre-toggle mode.
 assert(!s_requested_fullscreen);
 // 重排恢复失败才退回书架；失败不落盘，也不清掉原面板和跳转锚点。
 // Fall back to the shelf only when restoration fails; failed toggles neither persist nor clear the original panel/jump anchor.
 for(int full=0;full<2;++full)for(unsigned fail=1;fail<=2;++fail){
  reset(full,true,2,0);failed_reflows=fail;s_jump_offset=37;
  test_open_full=!full;s_requested_fullscreen=!full;
  app_redraw_t expected=fail==1?APP_REDRAW_NONE:APP_REDRAW_PAGE;
  assert(toggle_reader_fullscreen(&ctx)==expected&&s_reader_fullscreen==!!full);
  assert(reflows==2&&!draw_locked&&!saves&&s_page==2);
  assert(test_open_full==!full&&s_requested_fullscreen==!full&&s_jump_offset==37);
  assert(s_reader_panel==READER_PANEL_TOOLS);
  assert(fail==1?(s_text&&s_view==READING):(!s_text&&s_view==SHELF));
 }
 reset(true,true,2,0);s_text=NULL;
 assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_NONE&&!reflows&&!saves&&!draw_locked);
 // 12:55 a+b 之后翻页保持当前模式：非全屏不再自动切全屏；reset 留着 slider=2，
 // 翻页收滑块即返回 PAGE 且不跑水波；水波正路径由下方 slider=-1 的场景覆盖。
 // Since a+b a turn keeps the current mode. reset leaves slider=2, so every turn collapses
 // chrome and returns PAGE without the ripple; the ripple path is covered below with slider=-1.
 for(int full=0;full<2;++full)for(int tools=0;tools<2;++tools)for(int dir=-1;dir<=1;dir+=2){
  reset(full,tools,2,0);size_t destination=book_layout_page_start_offset(s_page+dir);
  app_redraw_t r=turn_page(&ctx,dir);
  assert(s_reader_fullscreen==!!full&&s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1);
  assert(s_stats_pending_turns==1&&s_session_turns==1&&paints==1);
  assert(book_layout_page_start_offset(s_page)==destination);
  assert(!reflows&&r==APP_REDRAW_PAGE&&!s_water_turn_pending&&!s_reader_turn_pending);
 }
 // 普通文字恢复正式版的翻页与周期清屏标志。/ Restore release text-turn and periodic-cleanup state.
 effect=0;
 for(int tools=0;tools<2;++tools)for(int full=0;full<2;++full)for(int prior=0;prior<2;++prior){
  reset(full,tools,2,0);s_reader_text_frame=prior;full_pages=5;s_turns=4;
  assert(turn_page(&ctx,1)!=APP_REDRAW_NONE&&!s_water_turn_pending&&s_reader_cleanup);
 }
 reset(true,false,2,0);test_images=1;s_reader_slider=-1;assert(turn_page(&ctx,1)==APP_REDRAW_AREA);
 test_images=0;effect=1;
 // Mixed/image targets and leaving an image never run the text ripple waveform.
 reset(true,false,2,0);test_images=2;s_reader_slider=-1;assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 reset(true,false,2,0);test_images=0;s_reader_text_frame=s_reader_stable_frame=false;s_reader_slider=-1;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 test_images=0;
 reset(false,true,0,0);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE);
 assert(!s_reader_fullscreen&&!s_toolbar&&s_page==0&&!paints&&!s_stats_pending_turns);
 reset(false,true,8,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(!s_reader_fullscreen&&!s_toolbar&&s_page==8&&!s_stats_pending_turns);
 reset(true,true,5,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_toolbar&&!reflows);
 reset(false,true,8,0);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_chapter==1&&s_page==0&&!s_reader_fullscreen&&!s_toolbar);
 reset(false,true,0,1);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE&&s_chapter==0&&s_page==8&&!s_reader_fullscreen&&!s_toolbar);
 reset(false,true,8,0);load_failed=true;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_view==TOC&&!paints&&!s_stats_pending_turns);
 reset(false,true,2,0);failed_reflows=1;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(s_text&&!s_reader_fullscreen&&!s_toolbar&&layout_span==100&&s_page==3&&reflows==0);
 reset(false,true,2,0);failed_reflows=2;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(s_text&&s_view==READING&&s_page==3&&reflows==0&&paints==1&&s_stats_pending_turns==1);
 puts("PASS: full-screen entry follows the remembered preference; toggles persist it; turns keep the current windowed mode; toolbar dismissal; chapter/boundary turns");
}
'''
from reader_session_test_state import position_state
unit = position_state(page=0) + unit
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    c, exe = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined',str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
