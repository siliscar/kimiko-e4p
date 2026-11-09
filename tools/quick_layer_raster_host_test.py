"""实际快捷层/点击缓存/汉字缩放的像素与内存检查。
Actual quick-layer, pressed-tile and Han scaling pixel/memory checks.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]

def body(path):
    return re.sub(r'^#include[^\n]*\n', '', (root/path).read_text(), flags=re.M)

def function(path, name):
    text=(root/path).read_text()
    start=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*\)\s*\{',text,re.M)
    assert start, name
    at=start.end(); depth=1
    while depth:
        depth += (text[at]=='{')-(text[at]=='}'); at+=1
    return text[start.start():at]+'\n'

code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui/ui_nav_layout.h"
#include "ui/ui_image_dither.h"
#include "assets/ui_icons.h"
#include "font/ui_hanzi_scale.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_TOUCH_SLOP_PX 24
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 0x90
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;
typedef enum {UI_CLICK_BACK,UI_CLICK_BACK_SMALL} ui_click_kind_t;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static int rotation;
#define W 1216
#define H 684
#define BYTES (W*H/2)
static uint8_t frame[BYTES],saved[BYTES],again[BYTES],fresh[BYTES];
static int live,allocs,max_bytes;
static bool oom,fast;
static int epd_width(void){return W;}
static int epd_height(void){return H;}
static int epd_get_rotation(void){return rotation;}
static int epd_rotated_display_width(void){return rotation&1?H:W;}
static int epd_rotated_display_height(void){return rotation&1?W:H;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){assert(w==W&&h==H&&x>=0&&x<w&&y>=0&&y<h);unsigned value=fb[(size_t)y*w/2+x/2];return (uint8_t)((x&1?value>>4:value&15)<<4);}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 if(x<0||y<0||x>=epd_rotated_display_width()||y>=epd_rotated_display_height())return;
 int px=x,py=y;
 switch(rotation){case 1:px=W-y-1;py=x;break;case 2:px=W-x-1;py=H-y-1;break;case 3:px=y;py=H-x-1;break;}
 size_t i=(size_t)py*W/2+px/2;unsigned shift=(px&1)*4;
 fb[i]=(uint8_t)((fb[i]&~(15u<<shift))|((gray>>4)<<shift));
}
static void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x)epd_draw_pixel(r.x+x,r.y+y,gray,fb);}
static void epd_draw_line(int x,int y,int xx,int yy,uint8_t gray,uint8_t *fb){assert(x==xx);for(;y<=yy;++y)epd_draw_pixel(x,y,gray,fb);}
static void epd_draw_circle(int cx,int cy,int r,uint8_t gray,uint8_t *fb){for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)if(x*x+y*y<=r*r&&x*x+y*y>(r-1)*(r-1))epd_draw_pixel(cx+x,cy+y,gray,fb);}
static void epd_fill_circle(int cx,int cy,int r,uint8_t gray,uint8_t *fb){for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)if(x*x+y*y<=r*r)epd_draw_pixel(cx+x,cy+y,gray,fb);}
static void ui_nav_status(uint8_t *fb){epd_fill_rect((EpdRect){36,29,52,16},0,fb);}
enum {EPD_DRAW_ALIGN_LEFT=0x2,EPD_DRAW_ALIGN_RIGHT=0x4,EPD_DRAW_ALIGN_CENTER=0x8};
static bool app_settings_main_fast_refresh(void){return fast;}
static void *heap_caps_malloc(size_t n,unsigned caps){assert(caps==3&&n<=4096);++allocs;if((int)n>max_bytes)max_bytes=(int)n;if(oom)return NULL;++live;return malloc(n);}
static void heap_caps_free(void *p){if(p){--live;free(p);}assert(live>=0);}
'''
code+='static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}\n'
header=(root/'main/ui/ui_quick_menu.h').read_text()
code+='\n'.join(re.findall(r'^#define UI_QUICK[^\n]*',header,re.M))+'\n'
code+=re.search(r'typedef enum \{\s*UI_QUICK_CTL_NONE.*?\} ui_quick_control_t;',header,re.S).group(0)+'\n'
code+=re.search(r'typedef struct \{.*?\} ui_quick_menu_t;',header,re.S).group(0)+'\n'
code+='static void epd_draw_vline(int x,int y,int n,uint8_t c,uint8_t *fb){for(int i=0;i<n;++i)epd_draw_pixel(x,y+i,c,fb);}'
code+=function('components/epdiy/src/epdiy.c','epd_fill_circle_helper')
code+=function('main/ui/ui_kit.h','ui_control_gray')
for name in ('ui_read_pixel','ui_image_bw_rect','icon_nibble','ui_draw_icon','ui_inset_rect',
             'clamp_radius','draw_arc','ui_draw_round_rect_raw','ui_draw_round_rect',
             'ui_fill_round_rect_raw','ui_fill_round_rect'):
    code+=function('main/ui/ui_kit.c',name)
code+=body('main/ui/ui_quick_menu.c')+body('main/ui/ui_click_feedback.c')
code+=r'''
static uint8_t pixel(uint8_t *fb,int x,int y){return ui_read_pixel(fb,x,y)*17u;}
static bool icon_edge(int x,int y){
 for(int i=0;i<4;++i)if(abs(x-center_x(i))<=22&&abs(y-UI_QUICK_CIRCLE_Y)<=22)return true;
 return abs(x-UI_QUICK_FL_ICON_CX)<=UI_QUICK_FL_ICON_SIZE/2&&
  (abs(y-UI_QUICK_FL_BRI_CY)<=UI_QUICK_FL_ICON_SIZE/2||abs(y-UI_QUICK_FL_CCT_CY)<=UI_QUICK_FL_ICON_SIZE/2);
}
static void patterned(void){for(size_t i=0;i<BYTES;++i)frame[i]=(uint8_t)(i*13u+7u);memcpy(saved,frame,BYTES);}
int main(void){
 for(rotation=0;rotation<4;++rotation){
  for(int policy=0;policy<3;++policy){
   // 三种层状态：无前光、开灯展开卡片、有前光但熄灯收起。
   // Three layer states: no frontlight, card expanded while on, collapsed while off.
   for(int fl=0;fl<3;++fl){
    ui_quick_menu_t menu={0};menu.frontlight=fl!=0;menu.fl_on=fl==1;menu.fl_bri=60;menu.fl_cct=30;
    const int layer_height=fl==1?UI_QUICK_FRONTLIGHT_HEIGHT:UI_QUICK_HEIGHT;
    patterned();int before=allocs;ui_quick_menu_draw(frame,&menu,true,false);
    assert(allocs==before&&!live);int grays=0;
    for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
     bool mask=y<layer_height&&layer_pixel(x,y,epd_rotated_display_width(),layer_height);
     uint8_t p=pixel(frame,x,y);
     if(!mask)assert(p==pixel(saved,x,y));
     else {if(!icon_edge(x,y))assert(p==0||p==153||p==255);grays+=p==153;}
    }
    assert(grays>1000);
    assert(ui_quick_menu_rect(&menu).height==layer_height);
    memcpy(again,frame,BYTES);memset(frame,0x33,BYTES);ui_quick_menu_draw(frame,&menu,true,false);
    for(int y=0;y<layer_height;++y)for(int x=0;x<epd_rotated_display_width();++x)
     if(layer_pixel(x,y,epd_rotated_display_width(),layer_height))assert(pixel(frame,x,y)==pixel(again,x,y));
   }
  }
  // 图标固定，双向拖动仅改变黑白像素；松手重绘无变化且不越出脏区。
  // Fixed icons leave only binary changes during bidirectional drags; release is idempotent and stays within dirty bounds.
  ui_quick_menu_t slider={.frontlight=true,.fl_on=true};
  memset(frame,255,BYTES);ui_quick_menu_draw(frame,&slider,false,false);
  const uint8_t values[]={1,25,80,100,70,20,0};
  for(int c=UI_QUICK_CTL_BRIGHTNESS;c<=UI_QUICK_CTL_CCT;++c)for(unsigned i=0;i<sizeof(values);++i){
   memcpy(again,frame,BYTES);if(c==UI_QUICK_CTL_BRIGHTNESS)slider.fl_bri=values[i];else slider.fl_cct=values[i];
   EpdRect dirty;assert(ui_quick_menu_draw_control(frame,&slider,c,&dirty));
   for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
    uint8_t a=pixel(again,x,y),b=pixel(frame,x,y);if(a==b)continue;
    assert(x>=dirty.x&&x<dirty.x+dirty.width&&y>=dirty.y&&y<dirty.y+dirty.height);
    assert((a==0&&b==255)||(a==255&&b==0));
   }
   memset(fresh,255,BYTES);ui_quick_menu_draw(fresh,&slider,false,false);
   assert(!memcmp(frame,fresh,BYTES));
   memcpy(again,frame,BYTES);assert(ui_quick_menu_draw_control(frame,&slider,c,&dirty));assert(!memcmp(frame,again,BYTES));
  }
  for(fast=false;;fast=true){
   patterned();ui_click_feedback_begin(frame);
   ui_click_feedback_register(again,(EpdRect){26,82,64,64},UI_CLICK_BACK,UI_ICON_CHEVRON_LEFT);
   EpdRect r;assert(!ui_click_feedback_press(frame,58,114,&r));
   ui_click_feedback_register(frame,(EpdRect){26,82,64,64},UI_CLICK_BACK,UI_ICON_CHEVRON_LEFT);
   assert(ui_click_feedback_press(frame,58,114,&r)&&live==1&&max_bytes==4096);
   assert(memcmp(frame,saved,BYTES));assert(!ui_click_feedback_press(frame,58,114,&r));
   for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x)
    if(x<26||x>=90||y<82||y>=146)assert(pixel(frame,x,y)==pixel(saved,x,y));
   assert(!ui_click_feedback_cancel_at(58,114)&&ui_click_feedback_cancel_at(100,114));
   assert(ui_click_feedback_release(frame,&r)&&!live&&!memcmp(frame,saved,BYTES));
   assert(!ui_click_feedback_release(frame,&r));
   oom=true;assert(!ui_click_feedback_press(frame,58,114,&r)&&!live&&!memcmp(frame,saved,BYTES));oom=false;
   ui_click_feedback_reset();assert(!ui_click_feedback_press(frame,58,114,&r));
   patterned();ui_click_feedback_begin(frame);
   ui_click_feedback_register(frame,(EpdRect){36,84,44,44},UI_CLICK_BACK_SMALL,UI_ICON_CHEVRON_LEFT);
   assert(ui_click_feedback_press(frame,58,106,&r));
   assert(r.x>=36&&r.y>=84&&r.x+r.width<=80&&r.y+r.height<=128);
   for(int yy=0;yy<epd_rotated_display_height();++yy)for(int xx=0;xx<epd_rotated_display_width();++xx)
    if(xx<36||xx>=80||yy<84||yy>=128)assert(pixel(frame,xx,yy)==pixel(saved,xx,yy));
   assert(ui_click_feedback_release(frame,&r)&&!live&&!memcmp(frame,saved,BYTES));
   if(fast)break;
  }
 }
 // 原尺寸保持精确位图；缩放产生中间覆盖率且不改变空白与满墨端点。
 // Native sizes remain exact; scaling produces intermediate coverage with preserved blank/full endpoints.
 uint8_t han[UI_HANZI_RECORD]={0};han[2]=han[3]=24;int intermediate=0;
 for(int y=0;y<24;++y)for(int x=0;x<24;++x)if(x==y){unsigned b=(unsigned)y*24+x;han[5+b/8]|=1u<<(b&7);}
 for(int y=0;y<24;++y)for(int x=0;x<24;++x)assert(ui_hanzi_coverage(han,24,24,x,y)==(x==y?255:0));
 for(int y=0;y<39;++y)for(int x=0;x<39;++x){unsigned v=ui_hanzi_coverage(han,39,39,x,y);intermediate+=v>0&&v<255;}
 assert(intermediate>100);
 memset(han+5,0,sizeof(han)-5);for(int y=0;y<72;++y)for(int x=0;x<72;++x)assert(!ui_hanzi_coverage(han,72,72,x,y));
 memset(han+5,255,sizeof(han)-5);assert(ui_hanzi_coverage(han,39,39,19,19)==255);
 puts("PASS: actual quick layer uses no allocations, fixed palette and underlay-independent masks in all three modes across four rotations; click cache <=4096 bytes restores exact pixels, ignores prefetch and tolerates OOM; Han scaling retains native bits and smooths enlarged edges");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'test.c';src.write_text(code)
    exe=Path(tmp)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-I'+str(root/'main'), "-I" + str(root / "main/display"), "-I" + str(root / "main/settings"),str(src),str(root/'main/ui/ui_image_dither.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
