#!/usr/bin/env python3
"""中文：真实全宽天气组件的矩形边界、旋转、动作与内存回归。
English: Real full-width weather bounds, rotation, actions and memory regressions.
"""
from pathlib import Path
import argparse
import os
import re
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont
import subprocess
import tempfile
from zipfile import ZipFile

import reader_title_style_preview as host
from run_reader_baseline import toolchain
from generate_weather_backgrounds import SCENES, host_definition

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path, help="Export native host previews after passing regressions")
args = parser.parse_args()
samples = (
    (100, "sun", "晴", 14, 22, 52),
    (305, "rain", "小雨", 13, 17, 86),
    (101, "cloud", "多云", 12, 19, 68),
    (400, "snow", "小雪", -4, 1, 82),
    (501, "fog", "雾", 9, 14, 94),
    (302, "storm", "雷阵雨", 15, 21, 88),
    (507, "dust", "沙尘暴", 10, 18, 26),
    (502, "haze", "霾", 10, 18, 60),
    (404, "sleet", "雨夹雪", -1, 3, 85),
    (104, "overcast", "阴", 11, 17, 76),
    (999, "neutral", "未知", 12, 18, 68),
)
asset_samples = tuple(sample for sample in samples[:-1] if sample[1] != "haze")
assert tuple(sample[1] for sample in asset_samples) == SCENES
subprocess.run([os.sys.executable, str(ROOT / "tools/generate_weather_backgrounds.py"), "--check"], check=True)
backgrounds = (ROOT / "main/assets/weather_backgrounds.bin").read_bytes()
assert len(backgrounds) == len(SCENES) * 77 * 448
assert len({backgrounds[i * 77 * 448:(i + 1) * 77 * 448] for i in range(len(SCENES))}) == len(SCENES)
for scene in range(len(SCENES)):
    mask = backgrounds[scene * 77 * 448:(scene + 1) * 77 * 448]
    assert any(mask[y * 77] & 1 for y in range(448)), (scene, "left edge was clipped")
    assert any(mask[y * 77 + 76] & 8 for y in range(448)), (scene, "right edge was clipped")
    assert all(mask[y * 77 + 76] & 0xf0 == 0 for y in range(448)), (scene, "nonzero row padding")
# 主闪电必须从云底连续到下沿，不能仅在最后一行留下孤立像素。/ The main channel must connect cloud base to bottom, not just leave isolated bottom pixels.
storm_index = SCENES.index("storm")
storm_mask = backgrounds[storm_index * 77 * 448:(storm_index + 1) * 77 * 448]
frontier = [(x, 447) for x in range(612) if storm_mask[447 * 77 + x // 8] & (1 << (x % 8))]
visited = set(frontier)
while frontier:
    x, y = frontier.pop()
    if y <= 149:
        break
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 612 and 149 <= ny < 448 and (nx, ny) not in visited:
                if storm_mask[ny * 77 + nx // 8] & (1 << (nx % 8)):
                    visited.add((nx, ny))
                    frontier.append((nx, ny))
else:
    raise AssertionError("storm lightning must connect cloud base to the bottom edge")
KIT = ROOT / "main/ui/ui_kit.c"
with TTFont(ROOT / "main/assets/builtin.ttf") as font:
    codepoints = set(font.getBestCmap())
assert all(ord(ch) in codepoints for ch in "石家庄河北省"), "missing default-font weather-city glyphs"
assert all(ord(ch) in codepoints for sample in samples for ch in sample[2]), "missing weather-preview glyphs"
for source in ("main/ui/ui_weather.c", "main/apps/app_components.c", "main/services/weather_service.c"):
    strings = []
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', (ROOT / source).read_text()):
        if token[0].startswith('"'):
            strings.append(token[0])
    missing = sorted({ch for ch in "".join(strings) if ord(ch) > 127 and ord(ch) not in codepoints})
    assert not missing, (source, "missing built-in UI glyphs", "".join(missing))
unit = host.host_preamble() + host.epdiy_slices()
unit += host_definition()
unit += r'''
void *heap_caps_realloc(void *p,size_t n,int caps){
    if(!p)return heap_caps_malloc(n,caps);
    size_t old=0;
    for(int i=0;i<8192;i++)if(allocations[i].p==p){old=allocations[i].bytes;break;}
    void *q=heap_caps_malloc(n,caps);
    if(q){memcpy(q,p,old<n?old:n);heap_caps_free(p);}return q;
}
'''
unit += """
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#include "assets/ui_icons.h"
#include "weather_service.h"
#include "ui_weather.h"
#include "esp_heap_caps.h"
#include <math.h>
uint8_t ui_read_pixel(const uint8_t *,int,int);
static uint8_t ui_control_gray(uint8_t g){return g<0x80?0:g>=0xf0?255:0x90;}
"""
unit += "\n".join(host.function(name, KIT) for name in (
    "clamp_radius", "draw_arc", "ui_draw_round_rect_raw", "ui_draw_round_rect",
    "ui_fill_round_rect_raw", "ui_fill_round_rect", "ui_inset_rect", "icon_nibble",
    "ui_draw_icon", "ui_read_pixel"))
unit += r"""
static unsigned temperature_labels;
static unsigned loading_labels,cached_labels,failed_labels,clock_labels,city_labels,refresh_labels;
void ui_text_fixed_context_aa_vc(uint8_t *f,int x,int y,int px,const char *s,const char *sample,
                                enum EpdFontFlags align,uint8_t ink,uint8_t paper){
    (void)sample;int a,b;ttf_measure_line_px(px,s,&a,&b);
    if(px>=58&&strchr(s,'/'))++temperature_labels;
    if(strstr(s,"正在"))++loading_labels;
    if(strstr(s,"缓存"))++cached_labels;
    if(strstr(s,"更新失败"))++failed_labels;
    if(strstr(s,"请先校准"))++clock_labels;
    if(!strcmp(s,"选择城市"))++city_labels;
    if(!strcmp(s,"刷新")){assert(px>=30);++refresh_labels;}
    int width=ttf_text_width_px(px,s);
    int left=align==EPD_DRAW_ALIGN_RIGHT?x-width:align==EPD_DRAW_ALIGN_CENTER?x-width/2:x;
    assert(left>=0&&left+width<=684);
    ttf_draw_text_px(f,x,y+(a-b)/2,px,s,align,ink>>4,paper>>4);
}
int ui_text_fixed_width_px(int px,const char *s){return ttf_text_width_px(px,s);}
"""
production = (ROOT / "main/ui/ui_weather.c").read_text()
unit += "\n".join(line for line in production.splitlines() if not line.startswith("#include"))
unit += '\nstatic const struct {int code; const char *name,*condition; int low,high,humidity;} samples[]={\n'
unit += ",\n".join(f'{{{code},"{name}","{label}",{low},{high},{humidity}}}'
                    for code, name, label, low, high, humidity in samples)
unit += "\n};\n"
unit += r"""
#include "weather_test_data.h"
static void previews(uint8_t *f,const char *directory){
    app_weather_view_t view={.has_city=true,.has_forecast=true,.state=APP_WEATHER_READY};
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&view.city));
    int64_t fixture_now=weather_timestamp("2026-11-02T00:30:00Z");
    assert(weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),fixture_now,&view.city,&view.forecast));
    strcpy(view.city.id,"101090101");strcpy(view.city.name,"石家庄");
    strcpy(view.city.admin1,"河北省");strcpy(view.city.admin2,"石家庄");
    view.city.lat=38.0428;view.city.lon=114.5149;view.forecast.location=view.city;
    const int64_t now=weather_timestamp("2026-11-04T00:30:00Z");
    view.forecast.fetched=now-600;view.forecast.fresh=now+3600;view.forecast.serve=view.forecast.fetched+21600;
    display_rotation=EPD_ROT_PORTRAIT;ttf_set_antialias(true);
    for(unsigned i=0;i<sizeof(samples)/sizeof(*samples);++i){
        for(int day=0;day<2;++day){
            weather_day_t *d=&view.forecast.days[day];
            d->date=weather_local_day(now)+day;d->code=samples[i].code;
            d->min_c=samples[i].low;d->max_c=samples[i].high;d->humidity=samples[i].humidity;
            snprintf(d->condition,sizeof(d->condition),"%s",samples[i].condition);
            d->sunrise=weather_timestamp("2026-11-03T22:15:00Z")+day*86400;
            d->sunset=weather_timestamp("2026-11-04T09:12:00Z")+day*86400;
        }
        assert(weather_forecast_valid(&view.forecast,now));
        for(int tomorrow=0;tomorrow<2;++tomorrow){
            assert(weather_day_for(&view.forecast,now,tomorrow));
            memset(f,255,684*1216/2);
            unsigned old_labels=temperature_labels;
            ui_weather_draw(f,(EpdRect){0,640,684,472},tomorrow,&view,now);
            assert(temperature_labels==old_labels+1);
            char path[4096];
            int n=snprintf(path,sizeof(path),"%s/%s%s.raw",directory,samples[i].name,tomorrow?"-tomorrow":"");
            assert(n>0&&(size_t)n<sizeof(path));save(path,f);
        }
    }
}
static void equivalent_halo(uint8_t *f,uint8_t *reference){
    memset(s_scratch,255,684*472/2);
    for(size_t i=0;i<684u*472u/2u;i+=97)s_scratch[i]=i%3?0xdd:0xef;
    for(int y=7;y<16;++y)memset(s_scratch+y*342+2,0,120);
    s_scratch[0]=s_scratch[684*472/2-1]=0;
    for(int rotation=0;rotation<2;++rotation)for(int pass=0;pass<3;++pass){
        display_rotation=rotation?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT;
        EpdRect region={0,pass==0?168:pass==1?640:236,684,472};
        memset(f,0x99,684*1216/2);memset(reference,0x99,684*1216/2);
        clear_foreground_halo(f,region);
        for(int y=0;y<472;++y)for(int x=0;x<684;++x)if(scratch_pixel(x,y)<224)
            for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)
                if(dx*dx+dy*dy<=9&&x+dx>=0&&x+dx<684&&y+dy>=0&&y+dy<472)
                    epd_draw_pixel(x+dx,y+region.y+dy,255,reference);
        assert(!memcmp(f,reference,684*1216/2));
    }
}
static void outside(const uint8_t *f,const uint8_t *before,EpdRect region){
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)
        if(x<region.x||x>=region.x+region.width||y<region.y||y>=region.y+region.height)
            assert(pixel_at(f,x,y)==pixel_at(before,x,y));
}
static void perimeter(const uint8_t *f,EpdRect region,int code){
    weather_scene_t scene=weather_scene(code);
    const uint8_t *mask=scene>=WEATHER_SCENE_SUN&&scene<WEATHER_SCENE_COUNT?
        weather_backgrounds_asset+(scene-WEATHER_SCENE_SUN)*77*448:NULL;
    for(int y=0;y<region.height;++y)for(int x=0;x<region.width;++x){
        if(x!=0&&x!=region.width-1&&y!=0&&y!=region.height-1)continue;
        int sx=x*612/684,sy=y*448/472;
        uint8_t expected=mask&&(mask[sy*77+sx/8]&(1u<<(sx%8)))?0x50:0xf0;
        assert(pixel_at(f,x,region.y+y)==expected);
    }
}
static void actions(EpdRect r){
    for(int y=-1;y<=r.height;++y)for(int x=-1;x<=r.width;++x){
        ui_weather_action_t expected=UI_WEATHER_NONE;
        if(x>=0&&x<r.width&&y>=0&&y<r.height){
            if(y>=16&&y<70){
                if(x>=448&&x<648)expected=x<548?UI_WEATHER_TODAY:UI_WEATHER_TOMORROW;
                else if(x>=24&&x<432)expected=UI_WEATHER_CITY;
            }else if(y>=r.height-80&&y<r.height-16){
                if(x>=600&&x<664)expected=UI_WEATHER_REFRESH;
            }
        }
        assert(ui_weather_hit(r,x,r.y+y)==expected);
    }
}
int main(int argc,char **argv){
    assert((argc==2||argc==3)&&ttf_font_open(argv[1])==ESP_OK);
    uint8_t *f=malloc(684*1216/2),*before=malloc(684*1216/2);assert(f&&before);
    app_weather_view_t view={.has_city=true,.has_forecast=true};
    int64_t now=weather_timestamp("2026-11-02T00:30:00Z");
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&view.city));
    assert(weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),now,&view.city,&view.forecast));
    size_t old=live;assert(ui_weather_prepare()&&live-old==684u*472u/2u);
    assert(ui_weather_prepare()&&live-old==684u*472u/2u);
    equivalent_halo(f,before);
    const int codes[]={100,305,101,400,501,302,507,502,404,104,999};
    const int scene_codes[]={100,305,101,400,501,302,507,404,104};
    assert(sizeof(codes)/sizeof(*codes)==sizeof(samples)/sizeof(*samples));
    assert(sizeof(scene_codes)/sizeof(*scene_codes)==WEATHER_SCENE_COUNT-1);
    assert(sizeof(weather_backgrounds_asset)==(WEATHER_SCENE_COUNT-1)*77u*448u);
    for(int i=0;i<WEATHER_SCENE_COUNT-1;++i)assert(weather_scene(scene_codes[i])==(weather_scene_t)(i+1));
    const EpdRect regions[]={{0,168,684,472},{0,640,684,472},{0,236,684,472}};
    display_rotation=EPD_ROT_PORTRAIT;
    view.forecast.days[0].code=501;
    memset(f,255,684*1216/2);ui_weather_draw(f,regions[0],false,&view,now);
    const int haze_codes[]={502,511,512,513};
    for(unsigned i=0;i<sizeof(haze_codes)/sizeof(*haze_codes);++i){
        view.forecast.days[0].code=haze_codes[i];
        memset(before,255,684*1216/2);ui_weather_draw(before,regions[0],false,&view,now);
        assert(!memcmp(f,before,684*1216/2));
    }
    for(unsigned rotation=0;rotation<2;rotation++){
        display_rotation=rotation?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT;
        for(unsigned slot=0;slot<3;++slot){
            EpdRect region=regions[slot];
            actions(region);
            for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);i++)for(int tomorrow=0;tomorrow<2;tomorrow++){
                view.forecast.days[tomorrow].code=codes[i];
                memset(f,0xaa,684*1216/2);memcpy(before,f,684*1216/2);
                app_weather_view_t original=view;
                uint8_t *scratch=s_scratch;
                ui_weather_draw(f,region,tomorrow,&view,now);outside(f,before,region);
                assert(!memcmp(&view,&original,sizeof(view))&&s_scratch==scratch);
                assert(memcmp(f,before,684*1216/2)!=0);
                perimeter(f,region,codes[i]);
            }
            view.has_forecast=false;
            ui_weather_draw(f,region,false,&view,0);outside(f,before,region);
            perimeter(f,region,999);
            view.has_forecast=true;
            for(int state=APP_WEATHER_IDLE;state<=APP_WEATHER_FAILED;++state){
                view.state=state;
                ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
                view.has_forecast=false;
                ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
                view.has_forecast=true;
            }
            view.forecast.stale=true;strcpy(view.notice,"更新失败");
            view.forecast.days[0].min_c=-90;view.forecast.days[0].max_c=65;
            ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
            view.forecast.stale=false;view.notice[0]=0;
            view.has_city=false;ui_weather_draw(f,region,false,&view,0);outside(f,before,region);
            view.has_city=true;
        }
    }
    if(argc==3)previews(f,argv[2]);
    ui_weather_release();
    display_rotation=EPD_ROT_PORTRAIT;
    EpdRect region=regions[1];
    view.forecast.days[0].code=104;
    memset(before,255,684*1216/2);ui_weather_draw(before,region,false,&view,now);
    assert(ui_weather_prepare());memset(f,255,684*1216/2);ui_weather_draw(f,region,false,&view,now);
    unsigned backdrop_pixels=0;
    for(int y=region.y;y<region.y+region.height;y++)for(int x=0;x<684;x++)
        if(pixel_at(before,x,y)==0xf0&&pixel_at(f,x,y)!=0xf0){
            assert(pixel_at(f,x,y)<=0x60);++backdrop_pixels;
        }
    assert(backdrop_pixels>1000);
    old=live;ui_weather_release();assert(old-live==684u*472u/2u);
    size_t available=capacity;capacity=live;assert(!ui_weather_prepare());capacity=available;
    memset(f,255,684*1216/2);memcpy(before,f,684*1216/2);ui_weather_draw(f,region,false,&view,now);outside(f,before,region);
    perimeter(f,region,999);
    const EpdRect invalid[]={{0,-1,684,472},{0,745,684,472},{1,640,684,472},{0,640,612,448}};
    for(unsigned i=0;i<4;++i){
        memcpy(before,f,684*1216/2);ui_weather_draw(f,invalid[i],false,&view,now);
        assert(!memcmp(before,f,684*1216/2));
        assert(ui_weather_hit(invalid[i],630,1086)==UI_WEATHER_NONE);
    }
    ui_weather_release();free(f);free(before);
    assert(temperature_labels>=28);
    assert(loading_labels&&cached_labels&&failed_labels&&clock_labels&&city_labels&&!refresh_labels);
    puts("PASS: full-width weather, nine backgrounds plus neutral, pixel-identical fog/haze backgrounds, two slots and detail, two rotations, rectangular perimeters, navigation isolation, exhaustive action boundaries, 161424-byte scratch and allocation fallback");
}
"""
with tempfile.TemporaryDirectory(prefix="weather-raster-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    os.environ.update(env)
    host.write_shims(work)
    with (work / "esp_heap_caps.h").open("a") as output:
        output.write("\nvoid *heap_caps_realloc(void *,size_t,int);\n")
    (work / "test.c").write_text(unit)
    flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
        "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
    for directory in (work, ROOT / "main/ui", ROOT / "main/font", ROOT / "main", ROOT / "main/boot",
                      ROOT / "main/diagnostics", ROOT / "main/display", ROOT / "main/settings",
                      ROOT / "main/services", ROOT / "tools", ROOT / "managed_components/espressif__cjson/cJSON"):
        flags += ["-I" + str(directory)]
    subprocess.run(flags + ["-c", str(ROOT / "main/font/ttf_font.c"), "-o", str(work / "ttf.o")], check=True)
    sources = [work / "test.c", work / "assets.c", work / "ttf.o",
        ROOT / "main/font/ttf_font_source.c", ROOT / "main/font/ttf_block_cache.c",
        ROOT / "main/font/ttf_glyph_cache.c", ROOT / "main/services/weather_model.c",
        ROOT / "managed_components/espressif__cjson/cJSON/cJSON.c"]
    subprocess.run(flags + list(map(str, sources)) + ["-Wl,-dead_strip", "-lm", "-o", str(work / "test")], check=True)
    command = [str(work / "test"), str(ROOT / "main/assets/builtin.ttf")]
    if args.output_dir:
        args.output_dir = args.output_dir.resolve()
        args.output_dir.mkdir(parents=True, exist_ok=True)
        command.append(str(args.output_dir))
    subprocess.run(command, check=True)

if args.output_dir:
    out = args.output_dir
    for _, name, _, _, _, _ in samples:
        for suffix in ("", "-tomorrow"):
            raw = (out / f"{name}{suffix}.raw").read_bytes()
            assert len(raw) == 684 * 1216
            frame = Image.frombytes("L", (684, 1216), raw).point(lambda v: min(255, v * 255 // 240))
            frame.crop((0, 640, 684, 1112)).save(out / f"{name}{suffix}.png")

    caption = ImageFont.truetype(str(ROOT / "main/assets/builtin.ttf"), 26)
    note = ImageFont.truetype(str(ROOT / "main/assets/builtin.ttf"), 22)

    def sheet(selected, filename, tomorrow=False, masks=False):
        rows = (len(selected) + 1) // 2
        canvas = Image.new("RGB", (1420, 60 + rows * 532), "#eeeeee")
        draw = ImageDraw.Draw(canvas)
        draw.text((18, 14), "HOST 示例数据 · 原生绘制，非实时天气或实屏照片", font=note, fill="black")
        for i, (code, name, label, _, _, _) in enumerate(selected):
            x, y = 18 + i % 2 * 700, 60 + i // 2 * 532
            draw.text((x, y), f"{label} · {code}", font=caption, fill="black")
            if masks:
                picture = Image.open(ROOT / "main/assets/weather" / f"{name}.png")
                picture = picture.resize((684, 472), Image.Resampling.NEAREST)
            else:
                picture = Image.open(out / f'{name}{"-tomorrow" if tomorrow else ""}.png')
            canvas.paste(picture.convert("RGB"), (x, y + 42))
        canvas.save(out / filename)

    sheet(samples[:-1], "all-weather-today.png")
    sheet(samples[:-1], "all-weather-tomorrow.png", tomorrow=True)
    sheet(samples[6:10], "new-weather.png")
    sheet(asset_samples, "all-backgrounds.png", masks=True)
    sheet((samples[0], samples[-1]), "neutral-comparison.png")
    with ZipFile(out / "weather-host-previews.zip", "w") as archive:
        for path in sorted(out.glob("*.png")):
            archive.write(path, path.name)
    print(f"PASS: 22 native weather previews and five comparison sheets exported to {out}")
