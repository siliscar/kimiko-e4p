#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：用真实绘制代码渲染阅读页眉两档（状态栏开/关），生成 PNG 供视觉核对。
# English: Render the reader header in both modes (status bar on/off) with real drawing code as PNGs.
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from home_cover_cache_host_test import function
a=argparse.ArgumentParser();a.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf');a.add_argument('--output',type=Path,default=ROOT/'build/reader_header_preview.png');args=a.parse_args()

with tempfile.TemporaryDirectory() as folder:
    p=Path(folder)
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
    (p/'esp_log.h').write_text('#pragma once\nstatic inline void host_log(const char *t,const char *f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) host_log(__VA_ARGS__)\n#define ESP_LOGW(...) host_log(__VA_ARGS__)\n#define ESP_LOGE(...) host_log(__VA_ARGS__)\n')
    (p/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
    (p/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,int);
void *heap_caps_calloc(size_t,size_t,int);
void heap_caps_free(void*);
size_t heap_caps_get_free_size(int);
size_t heap_caps_get_largest_free_block(int);
''')
    (p/'app.h').write_text((ROOT/'tools/ui_gesture_stubs/app.h').read_text())
    (p/'app_touch.h').write_text((ROOT/'main/app/app_touch.h').read_text())
    (p/'epdiy.h').write_text((ROOT/'tools/ui_gesture_stubs/epdiy.h').read_text())
    builtin=(ROOT/'main/assets/builtin.ttf').read_bytes()
    assets='#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={'+','.join(map(str,builtin))+'};\n'
    assets+=f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(builtin)}");\n';(p/'assets.c').write_text(assets)
    kit=ROOT/'main/ui/ui_kit.c'; nav=ROOT/'main/ui/ui_nav.c'; book=ROOT/'main/apps/app_book.c'
    unit=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "ttf_font.h"
#include "epdiy.h"
// ttf_font 的宿主内存替身。/ Host memory shims for ttf_font.
static struct {void *p;size_t bytes;} allocations[8192];
static size_t live,capacity=8*1024*1024;
void *heap_caps_malloc(size_t n,int caps){(void)caps;if(n>capacity-live)return NULL;void *v=malloc(n);if(!v)return NULL;for(int i=0;i<8192;++i)if(!allocations[i].p){allocations[i].p=v;allocations[i].bytes=n;live+=n;return v;}abort();}
void *heap_caps_calloc(size_t n,size_t z,int caps){if(z&&n>SIZE_MAX/z)return NULL;void *v=heap_caps_malloc(n*z,caps);if(v)memset(v,0,n*z);return v;}
void heap_caps_free(void *v){if(!v)return;for(int i=0;i<8192;++i)if(allocations[i].p==v){live-=allocations[i].bytes;allocations[i].p=NULL;free(v);return;}abort();}
size_t heap_caps_get_free_size(int c){(void)c;return capacity-live;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return capacity-live;}
void lock_pin_wipe(void *p,unsigned n){volatile uint8_t *b=p;while(n--)*b++=0;}
// PMU 快照最小替身：ui_nav 只用 present/quick_ok/qb_flags/qb_soc。
// Minimal PMU snapshot shim: ui_nav touches only present/quick_ok/qb_flags/qb_soc.
typedef struct {bool present,quick_ok;uint8_t qb_flags;uint16_t qb_soc;} pmu_snapshot_t;
static enum EpdRotation display_rotation=EPD_ROT_PORTRAIT;
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
int epd_rotated_display_width(void){return 684;}
int epd_rotated_display_height(void){return 1216;}
enum EpdRotation epd_get_rotation(void){return display_rotation;}
typedef struct {uint16_t x,y;} Coord_xy;
#define _swap_int(a,b) {int temp=a;a=b;b=temp;}
uint8_t ui_contrast_gray(uint8_t gray);
'''
    for n in ('_rotate','epd_draw_pixel','epd_get_pixel','epd_draw_hline','epd_draw_vline','epd_fill_rect','epd_draw_circle','epd_fill_circle_helper','epd_fill_circle','epd_write_line','epd_draw_line'):
        unit+=function(n,ROOT/'components/epdiy/src/epdiy.c')+'\n'
    for n in ('clamp_radius','draw_arc','ui_draw_round_rect','ui_fill_round_rect','ui_inset_rect','ui_draw_control_frame','ui_draw_separator','ui_hairline'):
        unit+=function(n,kit)+'\n'
    unit+=r'''
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define UI_NAV_TOP 1096
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
#define UI_STATUS_TIME_PX 28
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX 26
#define UI_NAV_NETWORK_ICON_PX 28
#define UI_NAV_BOLT_ICON_PX 26
enum {UI_ICON_ZAP=1,UI_ICON_WIFI,UI_ICON_BLUETOOTH};
// 传书状态替身：预览时 WiFi/蓝牙均不显示。
// Transfer status shim: neither WiFi nor Bluetooth shows in previews.
typedef struct {bool network_ready;int mode;} wifi_transfer_status_t;
enum {WIFI_TRANSFER_MODE_STA=1};
static void wifi_transfer_get_status(wifi_transfer_status_t *out){out->network_ready=false;out->mode=0;}
static void ui_nav_wifi_icon(uint8_t *fb,int cx,int cy,int size,uint8_t gray){(void)fb;(void)cx;(void)cy;(void)size;(void)gray;}
// 预览用简单闪电占位，替代 Lucide 掩模；电池框/电量条/百分比走真实代码。
// A simple bolt placeholder replaces the Lucide mask in previews; the battery frame, fill and
// percentage still run real code.
static void ui_draw_icon(uint8_t *fb,int cx,int cy,int px,int icon,uint8_t gray){(void)icon;epd_draw_line(cx-px/3,cy-6,cx+2,cy+2,gray,fb);epd_draw_line(cx+2,cy+2,cx-3,cy+3,gray,fb);epd_draw_line(cx-3,cy+3,cx+px/4,cy+9,gray,fb);}
// 与固件一致的内联电量读数；替身结构只保留 ui_nav 触及的字段。
// Inline battery reads matching firmware; the shim struct keeps only ui_nav's fields.
static inline int pmu_battery_percent(const pmu_snapshot_t *p){if(!p||!p->present||!p->quick_ok)return -1;return p->qb_soc<=1000?(p->qb_soc+5)/10:-1;}
static inline bool pmu_battery_charging(const pmu_snapshot_t *p){return p&&p->present&&p->quick_ok&&(p->qb_flags&4)!=0;}
static bool s_reader_fullscreen;
static int statusbar_on=1;
#include "reader_book.h"
static struct {reader_book_t book;} s_reader_session = {.book = {.title = "灵葫芦修仙记", .chapter_title = "第一章"}};
static bool app_settings_reader_statusbar(void){return statusbar_on;}
static const char *app_settings_status_signature(void){return "";}
static bool app_settings_ble_turner(void){return false;}
static int app_settings_main_refresh_mode(void){return 0;}
static int app_settings_system_contrast(void){return 100;}
// 预览不做对比度缩放，恒等返回。/ Previews skip contrast scaling and return unchanged.
uint8_t ui_contrast_gray(uint8_t gray){return gray;}
static void ui_text_fixed_vc(uint8_t *fb,int x,int cy,int px,const char *v,enum EpdFontFlags align,bool inv){(void)fb;(void)inv;int a=0,b=0;ttf_measure_line_px(px,v,&a,&b);ttf_draw_text_px(fb,x,cy+(a-b)/2,px,v,align,0,15);}
static int ui_text_fixed_width_px(int px,const char *v){return ttf_text_width_px(px,v);}
static void copy_text(char *d,size_t n,const char *v){snprintf(d,n,"%s",v);}
static void epd_draw_bitmap_region_stub(void){}
static time_t fake_now;
static int fake_percent=88,fake_charging;
static const pmu_snapshot_t *device_power_get(void){static pmu_snapshot_t s;s.present=1;s.quick_ok=1;s.qb_flags=3|(fake_charging?4:0);s.qb_soc=fake_percent*10;return &s;}
static time_t status_time(void){return fake_now;}
int64_t ui_nav_clock_minute(void){return fake_now/60;}
'''
    # ui_nav.c battery/clock/status: compile the real file? It pulls many deps; slice instead.
    for n in ('clock_text','ui_nav_clock_draw','ui_nav_clock_text','ui_nav_battery_draw'):
        unit+=function(n,nav)+'\n'
    unit+=function('ui_nav_status',nav).replace('void ui_nav_status','static void ui_nav_status_orig')+'\n'
    # 标题测量/绘制用 ttf 同款桩：对位断言已由 ota_ui_host_test 锁定，这里只看布局。
    # Title measuring/painting uses the same ttf shims; ota_ui_host_test locks the offsets, so this
    # preview only needs the layout.
    unit+=r'''
static bool title_uses_active_font(const char *s){(void)s;return false;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_font_title_px(int preferred,const char *s){(void)s;return preferred;}
bool ui_font_title_uses_supplement(const char *s){(void)s;return false;}
static int ui_font_text_width_px(int px,const char *s){return ttf_text_width_px(px,s);}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){ttf_measure_line_px(px,s,above,below);}
static void ui_font_draw_text_px(uint8_t *fb,int x,int baseline,int px,const char *s,int align,uint8_t ink,uint8_t paper,bool aa){(void)aa;ttf_draw_text_px(fb,x,baseline,px,s,align,ink,paper);}'''
    unit+=function('ui_text_title_fit',kit)+'\n'+function('ui_text_title_vc',kit)+'\n'
    unit+=function('draw_reader_header',book)+'\n'
    unit+=r'''
static uint8_t pixel_at(const uint8_t *frame,int x,int y){int px=x,py=y;switch(epd_get_rotation()){case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;default:break;}return epd_get_pixel(px,py,epd_width(),epd_height(),frame);}
static void save(const char *path,uint8_t *frame){FILE *f=fopen(path,"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x){uint8_t v=pixel_at(frame,x,y);assert(fwrite(&v,1,1,f)==1);}assert(!fclose(f));}
int main(int argc,char **argv){
 (void)argc;assert(ttf_font_open(argv[1])==ESP_OK);
 uint8_t *fb=malloc(684*1216/2);assert(fb);
 // 档位一：窗口化 + 状态栏开（页眉在 80..159，中心线 121）。
 fake_now=1760000000;fake_percent=88;fake_charging=0;statusbar_on=1;
 memset(fb,0xff,684*1216/2);
 ui_nav_status_orig(fb);draw_reader_header(fb);
 save("/tmp/header_statusbar_on.raw",fb);
 // 档位二：窗口化 + 状态栏关（页眉贴顶，中心线 41）。
 statusbar_on=0;
 memset(fb,0xff,684*1216/2);
 draw_reader_header(fb);
 save("/tmp/header_statusbar_off.raw",fb);
 puts("PASS: header rendered");
}
'''
    (p/'test.c').write_text(unit)
    flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function',
           '-I'+str(p),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'main'), "-I" + str(ROOT / "main/boot"), "-I" + str(ROOT / "main/diagnostics"), "-I" + str(ROOT / "main/display"), "-I" + str(ROOT / "main/settings"),
           '-I'+str(ROOT/'components/device_power/include'),'-I'+str(ROOT/'main/book')]
    subprocess.run(flags+['-c',str(ROOT/'main/font/ttf_font.c'),'-o',str(p/'ttf.o')],check=True)
    subprocess.run(flags+[str(p/'test.c'),str(p/'assets.c'),str(p/'ttf.o'), str(ROOT / 'main/font/ttf_font_source.c'), str(ROOT / 'main/font/ttf_block_cache.c'), str(ROOT / 'main/font/ttf_glyph_cache.c'),str(ROOT/'main/ui/ui_gesture.c'),'-Wl,-dead_strip','-lm','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(args.font.resolve())],check=True)
    from PIL import Image
    out=args.output; out.parent.mkdir(parents=True,exist_ok=True)
    for tag in ('on','off'):
        raw=Path(f'/tmp/header_statusbar_{tag}.raw').read_bytes()
        Image.frombytes('L',(684,1216),raw).crop((0,0,684,320)).save(out.with_name(f'{out.stem}_{tag}.png'))
    print(f"saved {out.with_name(out.stem+'_on.png')} and {out.with_name(out.stem+'_off.png')}")
