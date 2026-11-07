"""中文：执行真实槽位排布和列表点击分支的边界回归。
English: Exercise the real chrome layout and list-action branch at their boundaries.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
chrome = ROOT / 'main/book/reader_chrome.c'
book = ROOT / 'main/apps/app_book.c'
source = chrome.read_text()
page = book.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "reader_chrome.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *fb;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA} app_redraw_t;
enum {UI_GESTURE_MOVE,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum {MODE_GL16,UI_GRAY_BLACK,UI_ICON_CHEVRON_DOWN};
static EpdRect s_area,drawn_card;
static unsigned renders,drawn_rows;
static void render(app_ctx_t *ctx,uint8_t *fb){assert(ctx->fb==fb);++renders;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect card,int radius,int gray){(void)fb;(void)radius;(void)gray;drawn_card=card;++drawn_rows;}
static void ui_draw_control_frame(uint8_t *fb,EpdRect card,int radius,int gray){(void)fb;(void)card;(void)radius;(void)gray;}
static void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inverted){
 (void)fb;(void)px;(void)text;(void)align;(void)inverted;
 assert(box.x>=drawn_card.x&&box.x+box.width<=drawn_card.x+drawn_card.width);
 assert(box.y>=drawn_card.y&&box.y+box.height<=drawn_card.y+drawn_card.height);
 assert(box.y+box.height/2==drawn_card.y+drawn_card.height/2);
}
static void reader_panel_text_vc(uint8_t *fb,int x,int cy,int px,const char *text,enum EpdFontFlags align,bool inverted){
 (void)fb;(void)x;(void)px;(void)text;(void)align;(void)inverted;
 assert(cy==drawn_card.y+drawn_card.height/2);
}
static void ui_draw_icon(uint8_t *fb,int x,int cy,int size,int icon,int gray){
 (void)fb;(void)x;(void)size;(void)icon;(void)gray;
 assert(cy==drawn_card.y+drawn_card.height/2);
}
enum {READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_CHROME_SETTINGS,READER_PANEL_CHROME_PICKER};
static app_reader_slot_item_t items[2][3];
static unsigned s_chrome_pick_bar,s_chrome_pick_slot;
static int s_reader_panel,s_chrome_scroll,s_chrome_pick_scroll,s_mode;
static bool s_chrome_pick_drag,save_ok=true,s_reader_fullscreen;
static bool progress_bar=true,s_reader_progress_bar=true,statusbar=true;
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
static int s_margin[MARGIN_COUNT];
bool app_settings_reader_statusbar(void){return statusbar;}
static int s_chrome_height[2]={80,57};
static bool s_reader_footer_pending,s_reader_image_refresh_pending,s_reader_footer_strong,s_water_turn_pending;
static size_t s_jump_offset;
static char s_message[96];
enum {SHELF, READING};
static int s_view=READING;
static struct {struct {size_t page;} position;} s_reader_session;
static unsigned builds,build_failures,maps,progress_saves,freed;
static EpdRect built_body;
static bool locked;
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static size_t book_layout_page_for_offset(size_t offset){assert(locked);++maps;return offset/80;}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void refresh_chapter_lead(void){assert(locked);}
static void release_page_images(void){}
static void prepare_inline_image(void){}
static void save_progress(void){assert(!locked);++progress_saves;}
static void free_book(void){assert(!locked);++freed;}
static char s_chrome_error[64];
static unsigned saves,invalidates;
static app_reader_slot_item_t app_settings_reader_chrome_item_impl(unsigned bar,unsigned slot){return items[bar][slot];}
#define app_settings_reader_chrome_item app_settings_reader_chrome_item_impl
static bool app_settings_set_reader_chrome_item_impl(unsigned bar,unsigned slot,app_reader_slot_item_t item){++saves;if(save_ok)items[bar][slot]=item;return save_ok;}
#define app_settings_set_reader_chrome_item app_settings_set_reader_chrome_item_impl
static bool reset_default(void){++saves;if(save_ok)progress_bar=true;return save_ok;}
#define app_settings_set_reader_chrome_default reset_default
static bool get_progress_bar(void){return progress_bar;}
#define app_settings_reader_progress_bar get_progress_bar
static bool set_progress_bar(bool on){assert(locked);++saves;if(save_ok)progress_bar=on;return save_ok;}
#define app_settings_set_reader_progress_bar set_progress_bar
static bool set_heights(uint8_t h,uint8_t f){(void)h;(void)f;++saves;return save_ok;}
#define app_settings_set_reader_chrome_heights set_heights
static void invalidate_prep(void){++invalidates;}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int chrome_face_width(int px,const char *text){int width=0;for(const unsigned char *p=(const unsigned char*)text;*p;++p)if((*p&0xc0)!=0x80)width+=*p<128?px/2:px;return width;}
'''
unit += '\n'.join(re.findall(r'^#define CHROME_.*', source, re.M)) + '\n'
unit += re.search(r'typedef struct \{\s*app_reader_slot_item_t item;.*?\} chrome_slot_t;', source, re.S)[0] + '\n'
for name in ('chrome_copy', 'cut_last_utf8', 'slot_text', 'slot_measure', 'slot_trim', 'layout_slots'):
    unit += function(name, chrome) + '\n'
unit += '\n'.join(re.findall(r'^#define CHROME_.*', page, re.M)) + '\n'
unit += re.search(r'static const app_reader_slot_item_t s_chrome_item_order\[\] = \{.*?\};', page, re.S)[0] + '\n'
unit += '#define READER_CHROME_ITEM_ROWS 10\n'
unit += '\n'.join(re.findall(r'^#define UI_LOCK_(?:WIDTH|HEIGHT) .*', (ROOT / 'main/ui/ui_kit.h').read_text(), re.M)) + '\n'
unit += '\n'.join(re.findall(r'^#define UI_BAR_(?:TOP|H|MARGIN) .*', (ROOT / 'main/ui/ui_kit.h').read_text(), re.M)) + '\n'
unit += '\n'.join(re.findall(r'^#define READER_(?:FULLSCREEN_PROGRESS_TOP|FOOTER_PROGRESS_SPACE) .*', page, re.M)) + '\n'
for name in ('reader_footer_height', 'reader_area', 'reader_fullscreen_progress_area', 'body_rect_for_tracking', 'progress_rect'):
    unit += function(name, book) + '\n'
unit += r'''
static bool reader_reflow_build(void){
 assert(locked);++builds;built_body=body_rect_for_tracking(0);
 if(build_failures){--build_failures;return false;}return true;
}
'''
unit += re.search(r'^#define KEY_SETTINGS_TOP .*', page, re.M)[0] + '\n'
for name in ('chrome_row_rect', 'chrome_pick_card'):
    unit += function(name, book) + '\n'
if 'static bool chrome_row_visible(' in page:
    unit += function('chrome_row_visible', book) + '\n'
unit += function('reader_chrome_item_label', chrome) + '\n'
unit += function('draw_chrome_row', book) + '\n'
unit += function('chrome_list_scroll', book) + '\n'
for name in ('apply_reader_chrome_layout', 'apply_reader_chrome_heights'):
    unit += function(name, book) + '\n'
action = function('reader_panel_action', book)
start = action.index('    if (s_reader_panel == READER_PANEL_CHROME_SETTINGS ||')
end = action.index('    if (s_reader_panel == READER_PANEL_TURN_SETTINGS)', start)
unit += 'static app_redraw_t action(app_ctx_t *ctx,int x,int y){(void)ctx;\n' + action[start:end] + 'return APP_REDRAW_NONE;}\n'
unit += r'''
static void check_layout(reader_chrome_data_t *d){
 chrome_slot_t slots[3];layout_slots(d,false,28,36,648,slots);
 int end=36-24;
 for(int i=0;i<3;++i)if(slots[i].drawn&&slots[i].width){
  int x=i==0?36:i==1?342-slots[i].width/2:648-slots[i].width;
  assert(x>=36&&x+slots[i].width<=648&&x>=end+24);
  end=x+slots[i].width;
 }
}
int main(void){
 reader_chrome_data_t data={.title="这是一本标题非常长的测试图书用于确认排布",.chapter="这是一个需要截短的章节名称防止覆盖页码",.clock="12:34",.date="11-01 周日",.page=122,.page_count=123,.percent=99,.battery=100};
 items[1][0]=APP_READER_SLOT_CHAPTER;items[1][1]=APP_READER_SLOT_BLANK;items[1][2]=APP_READER_SLOT_PAGE_PROGRESS;
 check_layout(&data);
 for(unsigned a=0;a<APP_READER_SLOT_COUNT;++a)for(unsigned b=0;b<APP_READER_SLOT_COUNT;++b)for(unsigned c=0;c<APP_READER_SLOT_COUNT;++c){
  items[1][0]=a;items[1][1]=b;items[1][2]=c;check_layout(&data);
  data.page=123455;data.page_count=123456;data.percent=100;check_layout(&data);
  data.clock="";data.date="";check_layout(&data);data.clock="12:34";data.date="11-01 周日";
 }
 app_ctx_t ctx={0};
 assert(CHROME_ROW_VISIBLE==6&&CHROME_ROW_HEIGHT>=80);
 assert(CHROME_ROW_TOP>=KEY_SETTINGS_TOP+92&&CHROME_ROW_TOP<=KEY_SETTINGS_TOP+108);
 assert(CHROME_ROW_TOP+CHROME_ROW_VIEW_HEIGHT<=1090);
 for(int row=0;row<CHROME_ROW_COUNT;++row)
  assert(chrome_row_visible(chrome_row_rect(row),CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT)==(row<6));
 for(s_chrome_scroll=0;s_chrome_scroll<=CHROME_ROW_MAX_SCROLL;s_chrome_scroll+=CHROME_ROW_PITCH){
  drawn_rows=0;
  for(int row=0;row<CHROME_ROW_COUNT;++row)draw_chrome_row(NULL,row);
  assert(drawn_rows==6);
  s_reader_panel=READER_PANEL_CHROME_SETTINGS;unsigned before=saves;
  assert(action(&ctx,300,1112)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_CHROME_SETTINGS&&saves==before);
  assert(action(&ctx,300,CHROME_ROW_TOP-1)==APP_REDRAW_NONE);
  assert(action(&ctx,300,CHROME_ROW_TOP+CHROME_ROW_VIEW_HEIGHT)==APP_REDRAW_NONE);
  assert(action(&ctx,300,CHROME_ROW_TOP+CHROME_ROW_HEIGHT)==APP_REDRAW_NONE);
  for(int row=0;row<CHROME_ROW_COUNT;++row){
   EpdRect card=chrome_row_rect(row);
   if(card.y<CHROME_ROW_TOP||card.y+card.height>CHROME_ROW_TOP+CHROME_ROW_VIEW_HEIGHT)continue;
   s_reader_panel=READER_PANEL_CHROME_SETTINGS;
   assert(action(&ctx,row>=6&&row<8?600:card.x,card.y)==APP_REDRAW_PAGE);
   if(row<6)assert(s_reader_panel==READER_PANEL_CHROME_PICKER&&s_chrome_pick_bar==(unsigned)row/3&&s_chrome_pick_slot==(unsigned)row%3);
  }
 }
 // 显隐整行四角均触发重排，失败恢复旧布局；滑出行边界无效。
 // Every corner reflows visibility, failures restore the old layout, and out-of-row taps are inert.
 s_chrome_scroll=CHROME_ROW_MAX_SCROLL;s_reader_panel=READER_PANEL_CHROME_SETTINGS;
 EpdRect progress=chrome_row_rect(CHROME_PROGRESS_ROW);
 assert(chrome_row_visible(progress,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT));
 const int points[4][2]={{progress.x,progress.y},{progress.x+progress.width-1,progress.y},
  {progress.x,progress.y+progress.height-1},{progress.x+progress.width-1,progress.y+progress.height-1}};
 for(unsigned full=0;full<2;++full)for(unsigned fail=0;fail<2;++fail)for(unsigned i=0;i<4;++i){
  s_reader_fullscreen=full;save_ok=!fail;progress_bar=s_reader_progress_bar=true;s_chrome_error[0]=0;
  unsigned old_builds=builds,old_invalidates=invalidates;
  assert(action(&ctx,points[i][0],points[i][1])==APP_REDRAW_PAGE);
  assert(progress_bar==(bool)fail&&s_reader_progress_bar==progress_bar);
  assert(builds==old_builds+(fail?2:1)&&!locked&&invalidates==old_invalidates+1);
  assert((s_chrome_error[0]!=0)==(bool)fail&&s_reader_panel==READER_PANEL_CHROME_SETTINGS);
 }
 save_ok=true;unsigned progress_saves_before=saves;
 assert(action(&ctx,progress.x,progress.y+progress.height)==APP_REDRAW_NONE&&saves==progress_saves_before);
 progress_bar=s_reader_progress_bar=false;s_reader_fullscreen=false;
 assert(action(&ctx,36,chrome_row_rect(CHROME_ROW_COUNT-1).y)==APP_REDRAW_PAGE&&progress_bar);
 for(s_chrome_pick_scroll=0;s_chrome_pick_scroll<=CHROME_PICK_MAX_SCROLL;s_chrome_pick_scroll+=CHROME_PICK_PITCH){
  s_reader_panel=READER_PANEL_CHROME_PICKER;unsigned before=saves;
  assert(action(&ctx,300,1112)==APP_REDRAW_NONE&&saves==before);
  for(int row=0;row<READER_CHROME_ITEM_ROWS;++row){
   EpdRect card=chrome_pick_card(row);
   if(card.y<CHROME_PICK_TOP||card.y+card.height>CHROME_PICK_TOP+CHROME_PICK_VIEW_HEIGHT)continue;
   s_reader_panel=READER_PANEL_CHROME_PICKER;
   assert(action(&ctx,card.x+1,card.y+1)==APP_REDRAW_PAGE&&items[s_chrome_pick_bar][s_chrome_pick_slot]==s_chrome_item_order[row]);
  }
 }
 s_chrome_scroll=CHROME_ROW_MAX_SCROLL;s_reader_panel=READER_PANEL_CHROME_SETTINGS;save_ok=false;
 EpdRect reset=chrome_row_rect(CHROME_ROW_COUNT-1);action(&ctx,300,reset.y+20);assert(s_chrome_error[0]);
 for(unsigned full=0;full<2;++full)for(unsigned fail=0;fail<4;++fail){
  progress_bar=s_reader_progress_bar=true;
  s_reader_fullscreen=full;s_chrome_height[0]=80;s_chrome_height[1]=57;
  s_reader_session.position.page=3;builds=maps=progress_saves=freed=0;
  save_ok=fail!=3;build_failures=full?0:fail==1?1:fail==2?2:0;s_view=READING;
  apply_reader_chrome_heights(96,73,false);
  bool success=full?save_ok:fail==0;
  assert(!locked&&s_chrome_height[0]==(success?96:80)&&s_chrome_height[1]==(success?73:57));
  assert(s_reader_session.position.page==(!full&&success?300/80:3));
  assert(maps==(!full&&success)&&progress_saves==maps);
  assert(builds==(full?0:success?1:2));assert(freed==(!full&&fail==2));
 }
 // 两种阅读模式、显隐双向、默认恢复及全部失败路径均保留当前位置和持久化边界。
 // Both modes, toggle directions, defaults and all failure paths preserve the anchor and persistence boundary.
 for(unsigned full=0;full<2;++full)for(unsigned previous=0;previous<2;++previous)
 for(unsigned reset=0;reset<2;++reset)for(unsigned fail=0;fail<4;++fail){
  s_reader_fullscreen=full;progress_bar=s_reader_progress_bar=previous;
  s_chrome_height[0]=80;s_chrome_height[1]=57;s_reader_session.position.page=3;
  builds=maps=progress_saves=freed=0;s_jump_offset=42;s_view=READING;s_chrome_error[0]=0;
  bool next=reset||!previous,reflow=next!=previous;
  save_ok=fail!=3;build_failures=reflow?(fail==1?1:fail==2?2:0):0;
  app_redraw_t result=reset?apply_reader_chrome_heights(80,57,true):
      apply_reader_chrome_layout(80,57,next,false);
  bool success=save_ok&&(!reflow||fail==0);
  assert(result==APP_REDRAW_PAGE&&!locked);
  assert(s_reader_progress_bar==(success?next:previous)&&progress_bar==s_reader_progress_bar);
  assert(builds==(reflow?(success?1:2):0)&&maps==(success&&reflow)&&progress_saves==maps);
  assert(s_reader_session.position.page==(success&&reflow?300/80:3));
  assert(s_jump_offset==(success&&reflow?SIZE_MAX:42));
  assert(freed==(reflow&&fail==2)&&s_view==(freed?SHELF:READING));
  if(!success&&!freed)assert(s_chrome_error[0]);
  if(reflow&&!freed){
   EpdRect expected=body_rect_for_tracking(0);
   assert(!memcmp(&built_body,&expected,sizeof(expected)));
  }
 }
 // 排版、页脚与刷新区域共用显隐几何：普通回收19px，全屏回收12px，用户边距不变。
 // Layout, footer and refresh share visibility geometry: reclaim 19px/12px without changing margins.
 for(unsigned full=0;full<2;++full)for(unsigned status=0;status<2;++status)
 for(int header=64;header<=144;header+=8)for(int footer=57;footer<=121;footer+=8){
  s_reader_fullscreen=full;statusbar=status;s_chrome_height[0]=header;s_chrome_height[1]=footer;
  s_margin[0]=18;s_margin[1]=30;s_margin[2]=12;s_margin[3]=24;
  s_reader_progress_bar=true;
  EpdRect shown=body_rect_for_tracking(0),shown_area=reader_area(),shown_footer=progress_rect();
  EpdRect shown_strip=reader_fullscreen_progress_area();
  s_reader_progress_bar=false;
  EpdRect hidden=body_rect_for_tracking(0),hidden_area=reader_area(),hidden_footer=progress_rect();
  EpdRect hidden_strip=reader_fullscreen_progress_area();
  assert(hidden.x==shown.x&&hidden.y==shown.y&&hidden.width==shown.width);
  assert(hidden.height-shown.height==(full?12:19)&&hidden_area.height-shown_area.height==(full?12:19));
  assert(hidden_footer.y-shown_footer.y==19&&shown_footer.height-hidden_footer.height==19);
  assert(shown_strip.y+shown_strip.height==UI_LOCK_HEIGHT&&hidden_strip.height==0);
  assert(hidden_area.y+hidden_area.height==(full?UI_LOCK_HEIGHT:hidden_footer.y));
  assert(hidden.y+hidden.height+s_margin[3]+(full?2:0)==hidden_area.y+hidden_area.height);
 }
 progress_bar=s_reader_progress_bar=true;statusbar=true;
 save_ok=true;s_reader_fullscreen=false;s_chrome_height[0]=APP_READER_HEADER_HEIGHT_MAX;s_chrome_height[1]=57;
 s_chrome_scroll=3*CHROME_ROW_PITCH;s_reader_panel=READER_PANEL_CHROME_SETTINGS;
 EpdRect height=chrome_row_rect(6);unsigned before=saves;
 assert(action(&ctx,602,height.y+48)==APP_REDRAW_NONE&&saves==before);
 assert(action(&ctx,474,height.y+48)==APP_REDRAW_NONE&&saves==before);
 assert(action(&ctx,374,height.y+48)==APP_REDRAW_PAGE&&s_chrome_height[0]==APP_READER_HEADER_HEIGHT_MAX-8);
 int scroll=0;bool drag=false;
 ui_gesture_event_t ev={.type=UI_GESTURE_SWIPE_U,.x0=300,.x=300,.y0=CHROME_ROW_TOP+120,.y=CHROME_ROW_TOP};
 for(int step=0;step<CHROME_ROW_COUNT-CHROME_ROW_VISIBLE;++step){
  unsigned old_renders=renders;
  assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_AREA);
  assert(scroll==(step+1)*CHROME_ROW_PITCH&&renders==old_renders+1);
  assert(s_area.x==0&&s_area.y==CHROME_ROW_TOP&&s_area.width==UI_LOCK_WIDTH&&s_area.height==CHROME_ROW_VIEW_HEIGHT&&s_mode==MODE_GL16);
 }
 assert(scroll==CHROME_ROW_MAX_SCROLL);
 s_chrome_scroll=scroll;
 assert(chrome_row_visible(chrome_row_rect(CHROME_ROW_COUNT-1),CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT));
 unsigned old_renders=renders;
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_NONE&&renders==old_renders);
 ev.type=UI_GESTURE_SWIPE_D;
 while(scroll)assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_AREA);
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_NONE);
 ev.type=UI_GESTURE_SWIPE_U;ev.y0=CHROME_ROW_TOP-1;
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_NONE&&scroll==0);
 ev.y0=CHROME_ROW_TOP+CHROME_ROW_VIEW_HEIGHT;
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_NONE&&scroll==0);
 ev.type=UI_GESTURE_MOVE;ev.y0=CHROME_ROW_TOP+120;ev.y=ev.y0-108;
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_AREA&&drag&&scroll==CHROME_ROW_PITCH);
 ev.type=UI_GESTURE_SWIPE_U;
 assert(chrome_list_scroll(&ctx,&ev,CHROME_ROW_TOP,CHROME_ROW_VIEW_HEIGHT,CHROME_ROW_PITCH,CHROME_ROW_MAX_SCROLL,&scroll,&drag)==APP_REDRAW_NONE&&!drag&&scroll==CHROME_ROW_PITCH);
 puts("PASS: all chrome slot combinations stay bounded and disjoint; visible rows alone accept taps; reset reports failures");
 (void)s_mode;
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(ROOT/'main/settings'), '-I'+str(ROOT/'main/book'), '-I'+str(ROOT/'tools/settings_backup_stubs'),
                    str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)

# 复用分页测试的字体替身，正文尺寸与分页器均取真实源码。
# Reuse the layout test's font fixture with real reader geometry and the real paginator.
layout_unit = (ROOT / 'tools/book_layout_host_test.c').read_text().split('static void session_layout_lifecycle(void) {')[0]
layout_unit += r'''
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_BAR_TOP 1096
#define UI_BAR_H 96
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
#define READER_FOOTER_PROGRESS_SPACE 19
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
static bool s_reader_fullscreen,s_reader_progress_bar=true;
static int s_chrome_height[2]={80,57},s_margin[MARGIN_COUNT]={36,36,0,0};
static bool app_settings_reader_statusbar(void){return true;}
'''
for name in ('reader_footer_height', 'body_rect_for_tracking'):
    layout_unit += function(name, book) + '\n'
layout_unit += r'''
int main(void){
 char text[4097];for(size_t i=0;i<sizeof(text)-1;++i)text[i]="abcdefghijklmnopqrstuvxyz"[i%25];text[4096]=0;
 const int sizes[]={18,24,36,48,60,72,90};unsigned gained[2]={0};uint8_t fb=0;
 for(unsigned full=0;full<2;++full)for(unsigned size=0;size<sizeof(sizes)/sizeof(sizes[0]);++size)
 for(unsigned line=110;line<=150;line+=20)for(int footer=57;footer<=121;footer+=64){
  int px=sizes[size];s_reader_fullscreen=full;s_chrome_height[1]=footer;s_reader_progress_bar=true;
  book_layout_set_spacing(line,0);book_layout_set_first_line_indent(0);
  EpdRect shown=body_rect_for_tracking(0);
  assert(book_layout_build(text,strlen(text),shown,px));
  size_t shown_end=book_layout_page_start_offset(1),anchor=book_layout_page_start_offset(2);
  size_t shown_pages=book_layout_page_count();
  assert(shown_end&&shown_pages>2);
  book_layout_draw_cursor_t old_cursor={0};drawn[0]=0;
  assert(book_layout_draw_slice(&fb,0,shown,px,&old_cursor)==0);
  s_reader_progress_bar=false;EpdRect hidden=body_rect_for_tracking(0);
  assert(book_layout_build(text,strlen(text),hidden,px));
  assert(book_layout_draw_slice(&fb,0,shown,px,&old_cursor)==-1);
  size_t hidden_end=book_layout_page_start_offset(1),page=book_layout_page_for_offset(anchor);
  assert(hidden_end>=shown_end&&book_layout_page_count()<=shown_pages);
  if(hidden_end>shown_end)++gained[full];
  assert(book_layout_page_start_offset(page)<=anchor&&anchor<book_layout_page_start_offset(page+1));
  drawn[0]=0;
  for(size_t p=0;p<book_layout_page_count();++p){
   book_layout_draw_page(&fb,p,hidden,px);
   assert(last_draw_y<=hidden.y+hidden.height);
  }
  assert(!strcmp(drawn,text));
  capture_lines=true;captured_count=0;drawn[0]=0;book_layout_draw_page(&fb,0,hidden,px);
  assert(captured_count>0);
  for(unsigned i=0;i<captured_count;++i)assert(captured_y[i]<=hidden.y+hidden.height);
  char expected[sizeof(drawn)];strcpy(expected,drawn);capture_lines=false;drawn[0]=0;
  book_layout_draw_cursor_t cursor={0};int done,steps=0;
  do{done=book_layout_draw_slice(&fb,0,hidden,px,&cursor);assert(done>=0&&++steps<128);}while(!done);
  assert(!strcmp(expected,drawn));
  assert(book_layout_build(text,strlen(text),shown,px));
  assert(book_layout_page_start_offset(1)==shown_end&&book_layout_page_count()==shown_pages);
 }
 assert(gained[0]&&gained[1]);book_layout_free();
 printf("PASS: real pagination reclaims hidden progress space (extra-line samples: footer=%u fullscreen=%u), retains anchors, continuous text, idle drawing and round trips\n",gained[0],gained[1]);
 return 0;
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    path = Path(folder)
    (path / 'pagination.c').write_text(layout_unit)
    subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-Itools/book_layout_stubs', '-Itools/book_source_host_stubs', '-Imain/book', '-Imain',
                    str(path/'pagination.c'), 'main/book/book_layout.c', 'main/book/html_text.c',
                    '-o', str(path/'pagination')], cwd=ROOT, check=True)
    subprocess.run([str(path/'pagination')], check=True)
