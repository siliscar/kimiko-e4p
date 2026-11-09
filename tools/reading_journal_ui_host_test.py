#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实手账页面交互、内置字库与4bpp绘制检查，可导出PNG。/ English: Test real journal interactions, builtin fonts and 4bpp drawing, with optional PNG exports."""
import argparse
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw
import reader_title_style_preview as host
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "main/ui/ui_kit.c"


def source(path):
    return re.sub(r"^#include[^\n]*\n", "", (ROOT / path).read_text(), flags=re.M)


def unit():
    code = host.host_preamble()
    begin, end = code.index("static struct {void *p;"), code.index("static enum EpdRotation")
    code = code[:begin] + r'''
void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t z,int c){(void)c;return calloc(n,z);}
void *heap_caps_realloc(void *p,size_t n,int c){(void)c;return realloc(p,n);}
void heap_caps_free(void *p){free(p);}
size_t heap_caps_get_free_size(int c){(void)c;return 16*1024*1024;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
void lock_pin_wipe(void *p,unsigned n){memset(p,0,n);}
''' + code[end:]
    code = re.sub(r"^static [^\n]*\bui_font_\w+\([^\n]*\n", "", code, flags=re.M)
    code += r'''
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#include <math.h>
#include <time.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "ui_gesture.h"
#include "ui_font.h"
#include "assets/ui_icons.h"
#include "assets/reading_scenes.h"
#include "reading_journal.h"
#include "ui_nav_layout.h"
#define UI_KEY_1 1
#define UI_KEY_2 2
#define UI_KEY_3 3
static unsigned app_settings_system_font_size(void){return 120;}
uint8_t ui_read_pixel(const uint8_t *fb,int x,int y);
static void ui_clear_page(uint8_t *f){memset(f,255,684*1216/2);}
'''
    # ui_font.c本身也参与编译，不以TTF近似替代设备默认字体。/ Compile ui_font.c too, rather than approximating the default font through TTF.
    code += host.epdiy_slices()
    code += host.function("ui_control_gray", ROOT / "main/ui/ui_kit.h")
    code += "\n".join(host.function(name, KIT) for name in (
        "clamp_radius", "draw_arc", "ui_draw_round_rect_raw", "ui_draw_round_rect", "ui_inset_rect",
        "ui_draw_separator", "icon_nibble", "ui_draw_icon", "ui_read_pixel", "ui_text_fixed_context_width_px",
        "ui_text_fixed_width_px", "ui_text_fixed_context_vc", "ui_text_fixed_vc", "ui_text_fixed_context_aa_vc"))
    code += r'''
static unsigned text_checks;
static int allowed_top=0,allowed_bottom=1216;
static void checked_text(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags align,bool white){
    assert(ui_font_has_text(s));
    int above=0,below=0;ui_font_measure_line_px(px,s,&above,&below);
    int width=ui_text_fixed_width_px(px,s),left=x;
    if(align==EPD_DRAW_ALIGN_CENTER)left-=width/2;else if(align==EPD_DRAW_ALIGN_RIGHT)left-=width;
    int baseline=y+(above-below)/2;
    if(left<24||left+width>660||baseline-above<allowed_top||baseline+below>=allowed_bottom){
        fprintf(stderr,"Text out of bounds: %s (%d,%d,%d), width %d, y %d..%d allowed %d..%d\n",s,x,y,px,width,baseline-above,baseline+below,allowed_top,allowed_bottom);abort();
    }
    ++text_checks;ui_text_fixed_vc(f,x,y,px,s,align,white);
}
#define ui_text_fixed_vc checked_text
'''
    code += source("main/services/reading_journal_model.c")
    code += source("main/ui/ui_reading_journal.c")
    code += r'''
#undef ui_text_fixed_vc
static reading_journal_model_t journal;
static reading_journal_date_t today;
static bool poll_changed;
static int poll_calls,scene_calls,flush_calls,nav=-1;
static const reading_journal_model_t *app_reading_journal_model_stub(void){return &journal;}
static const reading_journal_date_t *app_reading_journal_today_stub(void){return &today;}
static const char *app_reading_journal_notice_stub(void){return "";}
static bool app_reading_journal_poll_stub(void){++poll_calls;bool changed=poll_changed;poll_changed=false;return changed;}
static bool app_reading_journal_next_scene_stub(void){++scene_calls;reading_journal_next_scene(&journal);return true;}
static bool app_reading_journal_flush_stub(void){++flush_calls;return true;}
#define app_reading_journal_model app_reading_journal_model_stub
#define app_reading_journal_today app_reading_journal_today_stub
#define app_reading_journal_notice app_reading_journal_notice_stub
#define app_reading_journal_poll app_reading_journal_poll_stub
#define app_reading_journal_next_scene app_reading_journal_next_scene_stub
#define app_reading_journal_flush app_reading_journal_flush_stub
static void ui_nav_status(uint8_t *f){
    ui_text_fixed_vc(f,36,40,28,"08:30",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_fixed_vc(f,342,40,26,"kimiko",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(f,648,40,28,"86%",EPD_DRAW_ALIGN_RIGHT,false);
}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
'''
    code += source("main/apps/app_reading_journal.c")
    code += r'''
static reading_journal_date_t date(int year,int month,int day){
    struct tm d={.tm_year=year-1900,.tm_mon=month-1,.tm_mday=day,.tm_hour=12};
    reading_journal_date_t result;assert(reading_journal_date(((int64_t)timegm(&d)-8*3600)*1000,&result));return result;
}
static void set_date(int year,int month,int day){today=date(year,month,day);(void)reading_journal_sync(&journal,&today);}
static void add(unsigned day,uint64_t book,const char *name,unsigned chapters,uint32_t minutes,uint32_t turns){
    for(unsigned i=0;i<chapters;++i)assert(reading_journal_add(&journal,day,book,name,i,i?0:minutes*60000,i?0:turns));
}
static app_redraw_t tap(app_ctx_t *ctx,int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y};return app_reading_journal.on_gesture(ctx,&e);}
static app_redraw_t day_tap(app_ctx_t *ctx,unsigned day){int i=today.first_weekday+(int)day-1;return tap(ctx,36+(2*(i%7)+1)*612/14,422+(i/7)*50);}
static void interaction(app_ctx_t *ctx){
    set_date(2026,11,4);app_reading_journal.on_enter(ctx);assert(s_selected==4&&!s_page);
    for(unsigned i=0;i<5;++i)add(4,i+1,"书",1,1,1);
    assert(tap(ctx,630,1082)==APP_REDRAW_PAGE&&s_page==1);
    assert(tap(ctx,630,1082)==APP_REDRAW_PAGE&&s_page==2);
    assert(tap(ctx,630,1082)==APP_REDRAW_NONE&&s_page==2);
    ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_R,.y0=900};
    assert(app_reading_journal.on_gesture(ctx,&swipe)==APP_REDRAW_PAGE&&s_page==1);
    swipe.y0=500;assert(app_reading_journal.on_gesture(ctx,&swipe)==APP_REDRAW_NONE&&s_page==1);
    assert(day_tap(ctx,2)==APP_REDRAW_PAGE&&s_selected==2&&!s_page);
    assert(day_tap(ctx,5)==APP_REDRAW_NONE&&s_selected==2);
    assert(tap(ctx,35,422)==APP_REDRAW_NONE&&s_selected==2);
    assert(tap(ctx,500,220)==APP_REDRAW_PAGE&&scene_calls==1&&s_selected==2&&!s_page);
    nav=-1;assert(tap(ctx,86,1164)==APP_REDRAW_NONE&&nav==-1);
    assert(tap(ctx,598,1164)==APP_REDRAW_NONE&&nav==-1);
    assert(tap(ctx,342,1164)==APP_REDRAW_NONE&&nav==0);
    assert(!app_reading_journal.menu_handle_enabled(ctx));
    ctx->request_return=false;app_reading_journal.on_key(ctx,UI_KEY_1);assert(ctx->request_return);
    ctx->request_return=false;app_reading_journal.on_key(ctx,UI_KEY_3);assert(ctx->request_return);
    ctx->request_return=false;nav=-1;app_reading_journal.on_key(ctx,UI_KEY_2);assert(!ctx->request_return&&nav==0);
    assert(app_reading_journal.on_key_long(ctx,UI_KEY_2)==APP_REDRAW_FULL);
    assert(app_reading_journal.on_key_long(ctx,UI_KEY_1)==APP_REDRAW_NONE&&app_reading_journal.defer_middle_short);
    set_date(2026,11,5);poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&s_selected==2);
    day_tap(ctx,5);set_date(2026,11,6);poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&s_selected==6);
    day_tap(ctx,2);set_date(2026,12,1);poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&s_selected==1&&!journal.count);
    memset(&today,0,sizeof(today));poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&!s_selected);
    set_date(2026,11,30);poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&s_selected==30);
    set_date(2026,11,4);poll_changed=true;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_PAGE&&s_selected==4);
    ctx->consumed=true;poll_changed=true;int calls=poll_calls;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_NONE&&poll_calls==calls);ctx->consumed=false;
    app_touch_t touch={.touched=true};ctx->touch=&touch;assert(app_reading_journal.on_tick(ctx)==APP_REDRAW_NONE&&poll_calls==calls);ctx->touch=NULL;
    app_reading_journal.on_before_lock(ctx);app_reading_journal.on_exit(ctx);assert(flush_calls==2);
    app_reading_journal_open(ctx);assert(ctx->request_app==&app_reading_journal);ctx->request_app=NULL;
    reading_journal_clear(&journal);
    puts("PASS: real detail taps, future dates, shared art, five-book paging, swipe zones, Home-only footer, keys/hold, midnight/month/rollback/invalid clock and lifecycle");
}
static void snapshot(uint8_t *fb,const char *folder,const char *name){char path[2048];snprintf(path,sizeof(path),"%s/%s.raw",folder,name);save(path,fb);}
static void detail(app_ctx_t *ctx){allowed_top=0;allowed_bottom=1216;app_reading_journal.render(ctx,ctx->fb);}
static void widget(uint8_t *fb,unsigned slot){
    EpdRect r={0,168+472*(int)slot,684,472};
    memset(fb,0x77,684*1216/2);allowed_top=r.y;allowed_bottom=r.y+r.height;
    ui_reading_journal_widget(fb,r,&journal,&today,"");
    for(int y=0;y<1216;++y)if(y<r.y||y>=r.y+r.height)for(int x=0;x<684;++x)assert(pixel_at(fb,x,y)==0x70);
    allowed_top=0;allowed_bottom=1216;
}
int main(int argc,char **argv){
    assert(argc==2&&ttf_font_open_builtin()==ESP_OK);
    uint8_t *fb=malloc(684*1216/2);assert(fb);app_ctx_t ctx={.fb=fb};interaction(&ctx);
    set_date(2026,11,4);
    add(1,1,"瓦尔登湖",5,44,64);add(2,1,"瓦尔登湖",7,67,102);add(2,2,"月亮与六便士",4,35,49);
    add(3,2,"月亮与六便士",6,80,111);add(4,3,"山茶文具店",3,28,40);add(4,1,"瓦尔登湖",6,58,86);
    assert(reading_journal_totals(&journal,0).milliseconds==312*60000);
    app_reading_journal.on_enter(&ctx);journal.scene=0;
    widget(fb,1);snapshot(fb,argv[1],"widget");widget(fb,0);snapshot(fb,argv[1],"widget-upper");
    detail(&ctx);snapshot(fb,argv[1],"detail-today");
    int future=today.first_weekday+4,cx=36+(2*(future%7)+1)*612/14,cy=422+(future/7)*50;
    bool muted=false;for(int y=cy-23;y<cy+23;++y)for(int x=cx-28;x<cx+28;++x){uint8_t shade=pixel_at(fb,x,y);assert(shade>=0x90);muted|=shade<0xf0;}assert(muted);
    day_tap(&ctx,2);detail(&ctx);snapshot(fb,argv[1],"detail-history");
    for(unsigned scene=0;scene<6;++scene){
        journal.scene=scene;char name[64];widget(fb,1);snprintf(name,sizeof(name),"widget-scene-%u",scene);snapshot(fb,argv[1],name);
        widget(fb,0);snprintf(name,sizeof(name),"widget-upper-scene-%u",scene);snapshot(fb,argv[1],name);
        detail(&ctx);snprintf(name,sizeof(name),"detail-scene-%u",scene);snapshot(fb,argv[1],name);
    }
    day_tap(&ctx,4);for(unsigned i=4;i<=6;++i)add(4,i,"书",2,2,3);s_page=2;detail(&ctx);snapshot(fb,argv[1],"detail-page-3");
    set_date(2026,11,30);app_reading_journal.on_enter(&ctx);detail(&ctx);snapshot(fb,argv[1],"detail-six-rows");
    reading_journal_clear(&journal);journal.scene=0;set_date(2026,11,4);app_reading_journal.on_enter(&ctx);
    widget(fb,1);snapshot(fb,argv[1],"widget-empty");detail(&ctx);snapshot(fb,argv[1],"detail-empty");
    for(unsigned scene=0;scene<6;++scene){
        journal.scene=scene;char name[64];widget(fb,1);snprintf(name,sizeof(name),"widget-empty-scene-%u",scene);snapshot(fb,argv[1],name);
        widget(fb,0);snprintf(name,sizeof(name),"widget-upper-empty-scene-%u",scene);snapshot(fb,argv[1],name);
        detail(&ctx);snprintf(name,sizeof(name),"detail-empty-scene-%u",scene);snapshot(fb,argv[1],name);
    }
    journal.scene=0;
    add(4,1,"这是一个很长很长的书名这是一个很长很长的书名这是一个很长很长的书名",8192,71582,UINT32_MAX);
    widget(fb,1);snapshot(fb,argv[1],"widget-long");detail(&ctx);snapshot(fb,argv[1],"detail-long");
    for(unsigned i=2;i<=512;++i)add((i%4)+1,i,"书",1,71582,UINT32_MAX);
    widget(fb,1);snapshot(fb,argv[1],"widget-max");detail(&ctx);snapshot(fb,argv[1],"detail-max");
    memset(&today,0,sizeof(today));app_reading_journal.on_enter(&ctx);detail(&ctx);snapshot(fb,argv[1],"detail-invalid-time");
    widget(fb,1);snapshot(fb,argv[1],"widget-invalid-time");reading_journal_clear(&journal);free(fb);
    printf("PASS: %u builtin-font text bounds and glyph checks, real 4bpp UI, both slots, six scenes, six-row calendar, empty/long/max/invalid states\n",text_checks);
}
'''
    return code


def run(output):
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "tools/generate_reading_scenes.py"), "--check"], check=True)
    with tempfile.TemporaryDirectory(prefix="journal-ui-") as directory:
        work = Path(directory)
        host.write_shims(work)
        with (work / "esp_heap_caps.h").open("a") as header:
            header.write("\nvoid *heap_caps_realloc(void *,size_t,int);\n")
        (work / "epdiy.h").write_text((work / "epdiy.h").read_text().replace(
            "EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT", "EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4"))
        (work / "miniz.h").write_text('''#pragma once
#include <stddef.h>
#include <zlib.h>
typedef struct {int state;} tinfl_decompressor;
typedef int tinfl_status;
#define TINFL_STATUS_DONE 0
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(s) ((s)->state=0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor *s,const unsigned char *in,size_t *n,unsigned char *base,unsigned char *out,size_t *m,int flags){(void)s;(void)base;(void)flags;uLongf size=*m;int result=uncompress(out,&size,in,*n);*m=size;return result==Z_OK?0:-1;}
''')
        payload = (ROOT / "main/assets/ui-hanzi.bin").read_bytes()
        with (work / "assets.c").open("a") as assets:
            assets.write('const uint8_t hanzi[] asm("_binary_ui_hanzi_bin_start")={' + ",".join(map(str, payload)) + '};\n')
            assets.write(f'asm(".globl _binary_ui_hanzi_bin_end\\n.set _binary_ui_hanzi_bin_end, _binary_ui_hanzi_bin_start + {len(payload)}");\n')
        (work / "test.c").write_text(unit())
        env, compiler = toolchain(work)
        flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
        for folder in (work, ROOT / "main", ROOT / "main/ui", ROOT / "main/font", ROOT / "main/boot", ROOT / "main/diagnostics", ROOT / "main/display", ROOT / "main/settings", ROOT / "main/app", ROOT / "main/services"):
            flags += ["-I" + str(folder)]
        units = [work / "test.c", work / "assets.c"] + [ROOT / "main/font" / name for name in ("ttf_font.c", "ttf_font_source.c", "ttf_block_cache.c", "ttf_glyph_cache.c", "ui_font.c", "ui_hanzi.c")]
        linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
        subprocess.run(flags + list(map(str, units)) + [linker, "-lm", "-lz", "-o", str(work / "test")], env=env, check=True)
        subprocess.run([str(work / "test"), str(output)], env=env, check=True)
    for raw in output.glob("*.raw"):
        image = Image.frombytes("L", (684, 1216), raw.read_bytes()).point(lambda v: v * 17 // 16)
        if raw.stem.startswith("widget"):
            top = 168 if raw.stem.startswith("widget-upper") else 640
            image = image.crop((0, top, 684, top + 472))
        image.save(raw.with_suffix(".png"))
        raw.unlink()
    scenes = json.loads((ROOT / "main/assets/reading-scenes/scenes.json").read_text())["scenes"]
    for view in ("widget", "widget-upper", "detail"):
        box = (438, 171, 648, 333) if view == "detail" else (438, 78, 648, 240)
        for scene, entry in enumerate(scenes):
            expected = Image.open(ROOT / "main/assets/reading-scenes" / (entry["slug"] + ".png")).convert("L").resize((210, 162), Image.Resampling.NEAREST)
            for state in ("", "empty-"):
                actual = Image.open(output / f"{view}-{state}scene-{scene}.png").convert("L").crop(box)
                assert ImageChops.difference(actual, expected).getbbox() is None, (view, state, scene, "source pixels differ")
    for view in ("widget", "widget-upper", "detail"):
        baseline = Image.open(output / f"{view}-scene-0.png").convert("L")
        box = (438, 171, 648, 333) if view == "detail" else (438, 78, 648, 240)
        for scene in range(1, 6):
            difference = ImageChops.difference(baseline, Image.open(output / f"{view}-scene-{scene}.png").convert("L"))
            assert difference.getbbox(), (view, scene, "blank scene")
            ImageDraw.Draw(difference).rectangle((box[0], box[1], box[2]-1, box[3]-1), fill=0)
            assert difference.getbbox() is None, (view, scene, "scene changed data")
    for scene in range(6):
        image = Image.open(output / f"detail-scene-{scene}.png")
        for box in ((40, 1120, 130, 1210), (550, 1120, 645, 1210), (540, 90, 660, 142)):
            assert image.crop(box).getextrema() == (255, 255), (scene, box)
        assert image.crop((310, 1130, 374, 1198)).getextrema()[0] == 0
    sheet = Image.new("RGB", (2 * 716 + 32, 1296), "#eeeeee")
    sheet.paste(Image.open(output / "widget.png").convert("RGB"), (32, 48))
    sheet.paste(Image.open(output / "widget-scene-4.png").convert("RGB"), (32, 576))
    sheet.paste(Image.open(output / "detail-today.png").convert("RGB"), (748, 48))
    sheet.save(output / "review.png")
    (output / "verification.json").write_text(json.dumps({"realProductUI": True, "builtinFont": True, "sampleDataOnly": True, "scenePixelComparisons": 36, "sanitizers": ["ASan", "UBSan"], "deviceAccess": False}, indent=2) + "\n")
    print(f"PASS: 36 exact source-pixel comparisons, native PNGs and scene isolation/removed-control checks: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        run(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="journal-raster-") as directory:
            run(Path(directory))
