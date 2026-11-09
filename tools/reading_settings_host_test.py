#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证已确认的阅读设置共用矩形、点按周期、二级页与间隙不穿透。
# English: Validate approved reading-sheet geometry, cycles, secondary destinations and inert gaps.
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from reader_settings_test_support import geometry

ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {unsigned chapter,page,offset;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS,READER_PANEL_CHROME_SETTINGS};
static int s_reader_panel, s_turns, invalidates, toggles[3], full_pages = 10, effect;
static int s_key_scroll,s_chrome_scroll;
static bool s_key_drag,s_chrome_drag;
static char s_chrome_error[64];
static bool ui_rect_hit(EpdRect r,unsigned x,unsigned y){return x>=(unsigned)r.x&&x<(unsigned)(r.x+r.width)&&y>=(unsigned)r.y&&y<(unsigned)(r.y+r.height);}
static unsigned app_settings_reader_full_pages(void){return full_pages;}
static void app_settings_set_reader_full_pages(unsigned n){full_pages=n;}
static unsigned app_settings_reader_turn_effect(void){return effect;}
static void app_settings_set_reader_turn_effect(unsigned n){effect=n;}
static app_redraw_t apply_reader_option(app_ctx_t *ctx,int i){(void)ctx;assert(i>=0&&i<3);++toggles[i];return APP_REDRAW_PAGE;}
static void invalidate_prep(void){++invalidates;}
'''
source = ROOT/'main/apps/app_book.c'
unit+=geometry()
unit+='static unsigned scale=100;\nstatic unsigned app_settings_system_font_size(void){return scale;}\n'
for name in ('reading_settings_top', 'reading_setting_rect', 'reading_settings_action'):
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
    app_ctx_t ctx={2,3,1234};
    // 八个控件共用命中矩形且不重叠。/ Eight controls share nonoverlapping hit rectangles.
    for(int i=0;i<8;++i){
        EpdRect a=reading_setting_rect(i);assert(a.x>=36&&a.x+a.width<=648&&a.y>=reading_settings_top()+100&&a.y+a.height<1141);
        for(int j=i+1;j<8;++j){EpdRect b=reading_setting_rect(j);assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);}
    }
    EpdRect mode=reading_setting_rect(2),keys=reading_setting_rect(3);
    assert(keys.x==mode.x+314&&keys.y==mode.y);
    const unsigned expected[]={15,30,0,5,10};
    EpdRect interval=reading_setting_rect(0),ripple=reading_setting_rect(1);
    for(unsigned i=0;i<5;++i){s_turns=100;assert(reading_settings_action(&ctx,interval.x+10,interval.y+10)==APP_REDRAW_PAGE);assert(full_pages==(int)expected[i]&&!s_turns);}
    full_pages=77;reading_settings_action(&ctx,interval.x+10,interval.y+10);assert(full_pages==5);
    for(unsigned i=0;i<6;++i){reading_settings_action(&ctx,ripple.x+10,ripple.y+10);assert(effect==(int)((i+1)%2));}
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,mode.x+10,mode.y+10);assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,keys.x+10,keys.y+10);assert(s_reader_panel==READER_PANEL_KEY_SETTINGS);
    EpdRect chrome=reading_setting_rect(7);s_chrome_scroll=100;s_chrome_drag=true;s_chrome_error[0]='x';
    reading_settings_action(&ctx,chrome.x,chrome.y);
    assert(s_reader_panel==READER_PANEL_CHROME_SETTINGS&&!s_chrome_scroll&&!s_chrome_drag&&!s_chrome_error[0]);
    for(int i=0;i<3;++i){EpdRect a=reading_setting_rect(4+i);assert(reading_settings_action(&ctx,a.x+a.width-5,a.y+a.height-5)==APP_REDRAW_PAGE&&toggles[i]==1);}
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;int before=invalidates;
    assert(reading_settings_action(&ctx,342,interval.y+10)==APP_REDRAW_NONE);
    assert(reading_settings_action(&ctx,120,interval.y+interval.height)==APP_REDRAW_NONE);
    assert(reading_settings_action(&ctx,120,1130)==APP_REDRAW_NONE);
    assert(s_reader_panel==READER_PANEL_REFRESH_SETTINGS&&invalidates==before);
    reading_settings_action(&ctx,342,1170);assert(s_reader_panel==READER_PANEL_NONE);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,342,reading_settings_top()-1);assert(s_reader_panel==READER_PANEL_NONE);
    for(scale=100;scale<=200;++scale)for(int i=0;i<8;++i){
        EpdRect a=reading_setting_rect(i);
        assert(a.y>=reading_settings_top()+100&&a.y+a.height<1141);
        for(int j=0;j<i;++j){EpdRect b=reading_setting_rect(j);assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);}
        const int points[4][2]={{a.x,a.y},{a.x+a.width-1,a.y},{a.x,a.y+a.height-1},{a.x+a.width-1,a.y+a.height-1}};
        for(int k=0;k<4;++k){
            s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
            assert(reading_settings_action(&ctx,points[k][0],points[k][1])==APP_REDRAW_PAGE);
            if(i==2)assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
            else if(i==3)assert(s_reader_panel==READER_PANEL_KEY_SETTINGS);
            else if(i==7)assert(s_reader_panel==READER_PANEL_CHROME_SETTINGS);
        }
        s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
        int previous=invalidates;
        assert(reading_settings_action(&ctx,a.x+a.width,a.y)==APP_REDRAW_NONE);
        assert(reading_settings_action(&ctx,a.x,a.y+a.height)==APP_REDRAW_NONE);
        assert(invalidates==previous&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS);
    }
    puts("PASS: 100-200% eight-setting geometry, cycles, secondary pages, Done and inert gaps");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,exe=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
