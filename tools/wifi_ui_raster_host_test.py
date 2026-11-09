#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""真实 WiFi 页面、字库与图标像素回归，可导出 PNG。/ Real WiFi page/font/icon raster regression with PNG export.
"""
import argparse
from pathlib import Path
import tempfile

import file_ui_host_test as native
import reader_title_style_preview as host

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "main/apps/app_transfer.c"
KIT = ROOT / "main/ui/ui_kit.c"


def unit():
    base = native.unit()
    marker = "typedef struct {char path[288];"
    assert marker in base
    code = base.split(marker, 1)[0]
    code += r'''
#include "ui_text_edit.h"
#include "../components/wifi_transfer/include/wifi_transfer.h"
#define UI_BTN_H 84
#define UI_BTN_RADIUS 14
#define UI_PX_BODY 38
#define UI_PX_CAPTION 28
#define UI_SEL_RING 3
#define UI_SEL_INSET 4
static int s_pressed=-1;
static wifi_transfer_status_t s_status;
static int ui_content_width(void){return 604;}
static void ui_draw_button(uint8_t *fb,EpdRect r,const char *s,bool on){
    (void)on;ui_draw_control_frame(fb,r,8,0);ui_text_box(fb,r,24,s,EPD_DRAW_ALIGN_CENTER,false);
}
static EpdRect ui_row_rect(int i,int n,int y,int h){int w=(604-(n-1)*12)/n;return(EpdRect){40+i*(w+12),y,w,h};}
'''.replace('"../components/', '"' + str(ROOT / 'components') + '/')
    source = PAGE.read_text()
    code += source[source.index("typedef enum { TRANSFER_METHODS"):source.index("static void render(app_ctx_t* ctx")]
    for name, path in (("ui_draw_selected_round_rect", KIT), ("ui_draw_pressed_round_rect", KIT),
                       ("ui_hairline", KIT), ("ui_nav_wifi_icon", ROOT / "main/ui/ui_nav.c"),
                       *((name, PAGE) for name in (
                           "transfer_header", "fit_label", "network_control_rect", "provisioning_button",
                           "network_border", "network_button", "saved_network_index", "draw_networks"))):
        code += host.function(name, path) + "\n"
    code += r'''
static void fixture(void){
    s_saved_count=8;s_saved_configured=true;strcpy(s_saved_ssid,"103");
    s_network_count=16;s_network_page=0;s_scan_pending=false;s_scan_error=ESP_OK;
    s_show_saved=s_saved_detail=s_forget_confirm=false;s_network_message[0]=0;
    memset(&s_status,0,sizeof(s_status));strcpy(s_status.wifi_ssid,"103");
    const char *names[]={"103","xiaoping","PengGang","501","401","家庭无线网络","Office","Guest"};
    for(size_t i=0;i<s_network_count;++i){
        s_networks[i]=(wifi_transfer_network_t){.supported=true,.requires_password=true,.rssi=-55-(int)i};
        if(i<8)strcpy(s_networks[i].ssid,names[i]);else snprintf(s_networks[i].ssid,33,"network-%u",(unsigned)i);
    }
    for(size_t i=0;i<s_saved_count;++i){strcpy(s_saved_networks[i].ssid,names[i]);s_saved_networks[i].requires_password=true;}
}
static void arrows(uint8_t *fb){
    for(int id=7;id<=9;id+=2){
        EpdRect r=network_control_rect(id);unsigned ink=0;
        for(int y=r.y+17;y<r.y+49;++y)for(int x=r.x+16;x<r.x+48;++x)if(ui_read_pixel(fb,x,y)<0xf0)++ink;
        assert(ink>20);
    }
}
int main(int argc,char **argv){
    assert(argc==2);assert(ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);char path[1024];
    for(font_percent=100;font_percent<=200;font_percent+=10){
        fixture();draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/nearby-%u.raw",argv[1],font_percent);save(path,fb);
        s_network_page=1;draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/nearby-page2-%u.raw",argv[1],font_percent);save(path,fb);
        s_show_saved=true;s_network_page=0;draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/saved-%u.raw",argv[1],font_percent);save(path,fb);
        s_network_page=1;draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/saved-page2-%u.raw",argv[1],font_percent);save(path,fb);
        s_saved_detail=true;s_selected=s_networks[1];draw_networks(fb);
        snprintf(path,sizeof(path),"%s/saved-detail-%u.raw",argv[1],font_percent);save(path,fb);
        s_forget_confirm=true;strcpy(s_forget_ssid,"xiaoping");draw_networks(fb);
        snprintf(path,sizeof(path),"%s/forget-%u.raw",argv[1],font_percent);save(path,fb);
        fixture();s_saved_count=s_network_count=0;s_saved_configured=false;draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/empty-%u.raw",argv[1],font_percent);save(path,fb);
        fixture();strcpy(s_saved_ssid,"12345678901234567890123456789012");
        strcpy(s_networks[0].ssid,"12345678901234567890123456789012");
        s_status.network_ready=true;strcpy(s_status.wifi_ssid,s_saved_ssid);
        strcpy(s_network_message,"连接失败，请检查密码或信号");draw_networks(fb);arrows(fb);
        snprintf(path,sizeof(path),"%s/long-name-%u.raw",argv[1],font_percent);save(path,fb);
    }
    free(fb);ttf_font_unload();puts("PASS: WiFi native page boxes, six rows, 100..200% scaling and nonblank glyph-independent paging icons");
}
'''
    return code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        native.run(args.output.resolve(), unit())
    else:
        with tempfile.TemporaryDirectory(prefix="wifi-raster-") as directory:
            native.run(Path(directory), unit())
