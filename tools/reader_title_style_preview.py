#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：用真实绘制代码渲染四种「章节标题样式」和「标题样式」选择面板，生成 PNG 供设计核对。
# English: Render the four chapter heading styles and the heading-style picker with the real drawing
# code as PNGs for review.
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
HEADING = ROOT / 'main/ui/ui_reader_heading.c'
HEADING_HEADER = ROOT / 'main/ui/ui_reader_heading.h'
KIT = ROOT / 'main/ui/ui_kit.c'
FONT = ROOT / 'main/assets/builtin.ttf'

# 题头带顶：窗口化 + 状态栏打开的正文首行，与阅读页一致。
# Band top: the windowed body with the status bar on, matching the reader page.
BAND_TOP = 176
CROP = (0, 150, 684, 410)
# 卡片里贴的是题头带的窗口 [y+4, y+176]：四种样式的内容都落在这 172 像素里（含静页余白的下横线）。
# Cards hold the band window [y+4, y+176]: all four styles fit in those 172 pixels, hairline included.
BAND_WINDOW_TOP = 4
BAND_CROP_HEIGHT = 172
STYLES = [('0-emblem', '中央章徽', 0), ('1-frame', '边框索引', 1),
          ('2-mark', '左锋题签', 2), ('3-plain', '静页余白', 3)]


def function(name, path):
    """切出单个函数体，注释与字符串先抹平，括号深度配平。"""
    text = path.read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), text)
    match = re.search(r'^(?:static )?(?:[A-Za-z_]\w*(?:\s+|\s*\*\s*))+?' + name + r'\([^;{}]*\)\s*\{',
                      scan, re.M)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[match.start():at]


def enum_block(path, first, name):
    """从真实源码里取枚举定义，序号不另抄一份。"""
    found = re.search(r'typedef enum \{\s*' + first + r'.*?\} ' + name + r';', path.read_text(), re.S)
    assert found, name
    return found.group(0) + '\n'


def string_table(path, name):
    """取 static const char* const NAME[] 文案表，面板的文字与固件同一份。"""
    found = re.search(r'static const char\* const ' + name + r'\[\] = \{[\s\S]*?\};', path.read_text())
    assert found, name
    return found.group(0) + '\n'


def host_preamble():
    """宿主替身头与内存、像素替身：两档渲染共用。"""
    return r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ttf_font.h"
#include "epdiy.h"
static struct {void *p;size_t bytes;} allocations[8192];
static size_t live,capacity=8*1024*1024;
void *heap_caps_malloc(size_t n,int caps){(void)caps;if(n>capacity-live)return NULL;void *v=malloc(n);if(!v)return NULL;for(int i=0;i<8192;++i)if(!allocations[i].p){allocations[i].p=v;allocations[i].bytes=n;live+=n;return v;}abort();}
void *heap_caps_calloc(size_t n,size_t z,int caps){if(z&&n>SIZE_MAX/z)return NULL;void *v=heap_caps_malloc(n*z,caps);if(v)memset(v,0,n*z);return v;}
void heap_caps_free(void *v){if(!v)return;for(int i=0;i<8192;++i)if(allocations[i].p==v){live-=allocations[i].bytes;allocations[i].p=NULL;free(v);return;}abort();}
size_t heap_caps_get_free_size(int c){(void)c;return capacity-live;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return capacity-live;}
void lock_pin_wipe(void *p,unsigned n){volatile uint8_t *b=p;while(n--)*b++=0;}
static enum EpdRotation display_rotation=EPD_ROT_PORTRAIT;
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
int epd_rotated_display_width(void){return 684;}
int epd_rotated_display_height(void){return 1216;}
enum EpdRotation epd_get_rotation(void){return display_rotation;}
typedef struct {uint16_t x,y;} Coord_xy;
#define _swap_int(a,b) {int temp=a;a=b;b=temp;}
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_MARGIN 40
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
#define BOOK_KIND_EPUB 1
#define READER_SHEET_BACK_PX 34
#define UI_NAV_BACK_BORDER_GRAY 0x58
#define UI_CLICK_BACK 2
#define UI_ICON_CHEVRON_RIGHT 1
#define UI_ICON_CHEVRON_LEFT 2
#define UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT 3
// 预览走真实 TTF 路径，和用户导入字体时设备上的画法一致；加粗也走覆盖率膨胀。
// Previews take the real TTF path, matching the device when a user font is active, so the bold
// also goes through the coverage dilation.
static bool title_uses_active_font(const char *s){(void)s;return true;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_font_title_px(int preferred,const char *s){(void)s;return preferred;}
// 补充位图字面：默认没有；用例里置真来验证「整行走补充基准字号」的接线。
// Bitmap supplements: none by default; a case turns this on to check the whole line taking the
// supplement's base size.
static bool s_font_has_supplement;
static bool ui_font_title_uses_supplement(const char *s){(void)s;return s_font_has_supplement;}
static int ui_font_text_width_px(int px,const char *s){return ttf_text_width_px(px,s);}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){ttf_measure_line_px(px,s,above,below);}
static void ui_font_draw_text_px(uint8_t *fb,int x,int baseline,int px,const char *s,int align,uint8_t ink,uint8_t paper,bool aa){(void)aa;ttf_draw_text_px(fb,x,baseline,px,s,align,ink,paper);}
static char s_chapter_heading_title[128],s_chapter_heading_label[32];
static bool s_system_text_scale=true,s_system_ttf;
uint8_t ui_contrast_gray(uint8_t gray);
void ui_fill_rect_fast(EpdRect rect,uint8_t gray,uint8_t *framebuffer);
uint8_t ui_contrast_gray(uint8_t gray){return gray;}
void ui_fill_rect_fast(EpdRect rect,uint8_t gray,uint8_t *framebuffer){epd_fill_rect(rect,gray,framebuffer);}
static uint8_t pixel_at(const uint8_t *frame,int x,int y){int px=x,py=y;switch(epd_get_rotation()){case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;default:break;}return epd_get_pixel(px,py,epd_width(),epd_height(),frame);}
static void save(const char *path,uint8_t *frame){FILE *f=fopen(path,"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x){uint8_t v=pixel_at(frame,x,y);assert(fwrite(&v,1,1,f)==1);}assert(!fclose(f));}
'''


def geometry_slices():
    """题头带与四款样式的绘制函数：阅读页与面板预览用的是同一份。"""
    return ''.join(function(n, HEADING) + '\n' for n in (
        'copy_text', 'reader_heading_band', 'heading_len', 'heading_line', 'reader_heading_fit',
        'draw_heading_plain', 'draw_heading_emblem', 'draw_heading_frame', 'draw_heading_mark',
        'draw_heading_single', 'heading_prefix_width', 'heading_wrap_break', 'heading_truncate_px',
        'draw_heading_wrapped', 'draw_reader_title_style'))


def kit_slices():
    # 加粗步长取真实源码，预览与固件不会各调一个值。
    # The bold step count comes from the real source so the preview cannot tune a different value.
    unit = re.search(r'#define UI_TITLE_BOLD_STEPS \d+', KIT.read_text()).group(0) + '\n'
    return unit + ''.join(function(n, KIT) + '\n' for n in (
        'clamp_radius', 'draw_arc', 'ui_draw_round_rect_raw', 'ui_draw_round_rect',
        'ui_fill_round_rect_raw', 'ui_fill_round_rect', 'ui_inset_rect',
        'ui_draw_control_frame', 'ui_draw_separator', 'ui_hairline', 'ui_content_width',
        'title_face_px', 'ui_text_title_fit_sized', 'ui_text_title_fit',
        'ui_text_title_background_vc', 'ui_text_title_vc', 'ui_text_title_bold_vc',
        'ui_text_title_measure_sized', 'ui_text_title_measure',
        'ui_text_title_width_px', 'ui_text_title_line_height_px', 'ui_text_title_ink_vc'))


def epdiy_slices():
    return ''.join(function(n, ROOT / 'components/epdiy/src/epdiy.c') + '\n' for n in (
        '_rotate', 'epd_draw_pixel', 'epd_get_pixel', 'epd_draw_hline', 'epd_draw_vline',
        'epd_fill_rect', 'epd_draw_circle', 'epd_fill_circle_helper', 'epd_fill_circle',
        'epd_write_line', 'epd_draw_line'))


def write_shims(work):
    (work / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
    (work / 'esp_log.h').write_text('#pragma once\nstatic inline void host_log(const char *t,const char *f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) host_log(__VA_ARGS__)\n#define ESP_LOGW(...) host_log(__VA_ARGS__)\n#define ESP_LOGE(...) host_log(__VA_ARGS__)\n')
    (work / 'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
    (work / 'esp_heap_caps.h').write_text('#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\nvoid *heap_caps_calloc(size_t,size_t,int);\nvoid heap_caps_free(void*);\nheap_caps_get_free_size_placeholder\n')
    (work / 'esp_heap_caps.h').write_text((work / 'esp_heap_caps.h').read_text().replace(
        'heap_caps_get_free_size_placeholder',
        'size_t heap_caps_get_free_size(int);\nsize_t heap_caps_get_largest_free_block(int);'))
    (work / 'epdiy.h').write_text((ROOT / 'tools/ui_gesture_stubs/epdiy.h').read_text())
    builtin = FONT.read_bytes()
    assets = '#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={' + ','.join(map(str, builtin)) + '};\n'
    assets += f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(builtin)}");\n'
    (work / 'assets.c').write_text(assets)


def compile_unit(work, unit):
    (work / 'test.c').write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
             '-Wno-unused-function',
             '-I' + str(work), '-I' + str(ROOT / 'main/ui'), '-I' + str(ROOT / 'main/font'),
             '-I' + str(ROOT / 'main'), "-I" + str(ROOT / "main/boot"), "-I" + str(ROOT / "main/diagnostics"), "-I" + str(ROOT / "main/display"), "-I" + str(ROOT / "main/settings")]
    subprocess.run(flags + ['-c', str(ROOT / 'main/font/ttf_font.c'), '-o', str(work / 'ttf.o')], check=True)
    subprocess.run(flags + [str(work / 'test.c'), str(work / 'assets.c'), str(work / 'ttf.o'), str(ROOT / 'main/font/ttf_font_source.c'), str(ROOT / 'main/font/ttf_block_cache.c'), str(ROOT / 'main/font/ttf_glyph_cache.c'),
                            '-Wl,-dead_strip', '-lm', '-o', str(work / 'test')], check=True)


def style_unit():
    """四款样式各自整页画一次，存原始像素。"""
    unit = host_preamble() + epdiy_slices() + kit_slices()
    unit += enum_block(ROOT / 'main/settings/settings.h', 'APP_BOOK_TITLE_STYLE_EMBLEM',
                       'app_book_title_style_t')
    unit += re.search(r'#define READER_HEADING_BAND_HEIGHT \d+', HEADING_HEADER.read_text()).group(0) + '\n'
    # 带顶边距与字号档位也取真实源码：题头几何按它们缩放。
    # The band's top margin and the size levels come from the real source too; the geometry scales
    # with them.
    unit += ''.join(m + '\n' for m in re.findall(
        r'#define READER_HEADING_(?:BLOCK_\w+|BOLD_PX|WRAP_\w+|TITLE_BYTES|LABEL_BYTES) \d+', HEADING.read_text() + HEADING_HEADER.read_text()))
    unit += ''.join(m + '\n' for m in re.findall(r'#define APP_BOOK_TITLE_SIZE_\w+ \d+',
                                                 (ROOT / 'main/settings/settings.h').read_text()))
    unit += geometry_slices()
    unit += r'''
int main(int argc,char **argv){
 assert(argc==7);assert(ttf_font_open(argv[1])==ESP_OK);
 snprintf(s_chapter_heading_label,sizeof(s_chapter_heading_label),"%s",argv[2]);
 snprintf(s_chapter_heading_title,sizeof(s_chapter_heading_title),"%s",argv[3]);
 // 档位与带高都是运行期入参：0 表示用源码里的值，其余按调用方给的画。
 // Both the size level and the band height are runtime arguments: zero keeps the source value, any
 // other number paints what the caller asked for.
 int size=atoi(argv[5]),height=atoi(argv[6]);
 if(size<=0)size=APP_BOOK_TITLE_SIZE_DEFAULT;
 EpdRect band=reader_heading_band(176);
 if(height>0)band.height=height;
 uint8_t *fb=malloc(684*1216/2);assert(fb);
 char path[256];
 for(int style=0;style<APP_BOOK_TITLE_STYLE_COUNT;++style){
   memset(fb,0xff,684*1216/2);
   draw_reader_title_style(fb,band,style,size,s_chapter_heading_label,s_chapter_heading_title);
   snprintf(path,sizeof(path),"%s/style%d.raw",argv[4],style);
   save(path,fb);
 }
 puts("PASS: heading styles rendered");
}
'''
    return unit


def panel_unit():
    """选择面板整屏画一次；设置与书本类型由命令行的三档驱动。"""
    unit = host_preamble()
    # 档位常量要排在面板桩前面：桩里报告的「当前档」就读它。
    # The size levels come before the panel stubs, which report the current level.
    unit += ''.join(m + '\n' for m in re.findall(r'#define APP_BOOK_TITLE_SIZE_\w+ \d+',
                                                 (ROOT / 'main/settings/settings.h').read_text()))
    unit += r'''
static unsigned style_selected=3;
static int s_title_style_scroll;
static bool book_vertical;
static int book_kind_value=BOOK_KIND_EPUB;
static unsigned app_settings_book_title_style(void){return style_selected;}
static unsigned app_settings_book_title_size(void){return APP_BOOK_TITLE_SIZE_DEFAULT;}
static bool app_settings_book_vertical(void){return book_vertical;}
static int book_kind(void){return book_kind_value;}
static unsigned app_settings_system_font_size(void){return 100;}
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,int icon){(void)fb;(void)r;(void)kind;(void)icon;}
// 返回键与右箭头的 Lucide 掩模在宿主里没有，用同尺寸折线代替；位置与真实控件一致。
// The Lucide masks are unavailable on the host, so the chevrons are drawn as polylines of the same
// size at the real control positions.
static void ui_draw_icon(uint8_t *fb,int cx,int cy,int px,int icon,uint8_t gray){
 (void)icon;epd_draw_line(cx-px/6,cy-px/4,cx+px/6,cy,gray,fb);epd_draw_line(cx+px/6,cy,cx-px/6,cy+px/4,gray,fb);}
'''
    unit += epdiy_slices() + kit_slices()
    for n in ('ui_text_effective_px', 'ui_text_fixed_context_width_px', 'ui_text_fixed_width_px',
              'ui_text_fixed_context_vc', 'ui_text_fixed_vc', 'ui_text_box'):
        unit += function(n, KIT) + '\n'
    unit += enum_block(ROOT / 'main/settings/settings.h', 'APP_BOOK_TITLE_STYLE_EMBLEM',
                       'app_book_title_style_t')
    unit += re.search(r'#define READER_HEADING_BAND_HEIGHT \d+', HEADING_HEADER.read_text()).group(0) + '\n'
    # 带顶边距与字号档位也取真实源码：题头几何按它们缩放。
    # The band's top margin and the size levels come from the real source too; the geometry scales
    # with them.
    unit += ''.join(m + '\n' for m in re.findall(
        r'#define READER_HEADING_(?:BLOCK_\w+|BOLD_PX|WRAP_\w+|TITLE_BYTES|LABEL_BYTES) \d+', HEADING.read_text() + HEADING_HEADER.read_text()))
    unit += string_table(BOOK, 's_title_style_names')
    unit += string_table(BOOK, 's_title_style_details')
    # 面板的几何常量从源码取，卡片与窗口位置不另抄。
    # Geometry constants come from the source so card and window positions cannot drift.
    for macro in re.findall(r'#define TITLE_STYLE_\w+ \d+', BOOK.read_text()):
        unit += macro + '\n'
    unit += geometry_slices()
    # 面板与题头同序：说明行、卡片矩形、样式名都先定义，再用到面板绘制里。
    # Same order as the heading: note line, card rectangles and style names come before the panel.
    for n in ('reader_panel_text_vc', 'draw_sheet', 'draw_sheet_back', 'title_style_slot_y',
              'title_style_card_rect', 'title_style_window_rect', 'reader_title_style_name',
              'title_style_note', 'title_style_preview_label', 'title_style_preview_title',
              'heading_size_rect', 'draw_heading_size_row', 'draw_title_style_picker'):
        unit += function(n, BOOK) + '\n'
    unit += r'''
int main(int argc,char **argv){
 assert(argc==7);assert(ttf_font_open(argv[1])==ESP_OK);
 style_selected=(unsigned)atoi(argv[2]);
 book_vertical=atoi(argv[3])!=0;
 book_kind_value=atoi(argv[4]);
 s_title_style_scroll=atoi(argv[6]);
 uint8_t *fb=malloc(684*1216/2);assert(fb);
 memset(fb,0xff,684*1216/2);
 draw_title_style_picker(fb);
 save(argv[5],fb);
 puts("PASS: heading style picker rendered");
 return 0;
}
'''
    return unit


def cropped(raw, paper):
    from PIL import Image
    image = paper(Image.frombytes('L', (684, 1216), raw))
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', type=Path, default=FONT)
    parser.add_argument('--label', default='第十二章')
    # 默认标题避开内建 TTF 没有的字（如「塔」）：设备上那份字由位图补充字库补上，宿主预览里会缺字。
    # The default title avoids glyphs missing from the builtin TTF (such as 塔); the device fills those
    # from the bitmap supplement while the host preview would show a blank box.
    parser.add_argument('--title', default='远方来信')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/title-style-preview/all-styles.png')
    # 字号档与带高：默认取源码里的值，给数就画那个档（对比图靠它摆出前后两版）。
    # Size level and band height: the source values by default, any given number paints that level,
    # which is how the before/after sheets are laid out.
    parser.add_argument('--size', type=int, default=0)
    parser.add_argument('--band', type=int, default=0)
    # 滚动量：面板只有三档，给数就按那一档画，用来核对落点不切卡。
    # Scroll offset: the sheet has three positions; any given value paints that one for checking
    # that no card is ever cut.
    parser.add_argument('--scroll', type=int, default=0)
    parser.add_argument('--full', action='store_true', help='每个样式另存整页 PNG')
    args = parser.parse_args()

    from PIL import Image
    out = args.output
    out.parent.mkdir(parents=True, exist_ok=True)
    # 面板白在 4bpp 里是 0xF0（240），预览图拉到 255，看着才像纸；灰阶关系不变。
    # The panel's white is 0xF0 in 4bpp; previews lift it to 255 so they read like paper.
    paper = lambda image: image.point(lambda v: min(255, v * 255 // 240))

    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        write_shims(work)
        compile_unit(work, style_unit())
        subprocess.run([str(work / 'test'), str(args.font.resolve()), args.label, args.title,
                        str(work), str(args.size), str(args.band)], check=True)
        crops, bands = [], []
        for tag, name, style in STYLES:
            page = cropped((work / f'style{style}.raw').read_bytes(), paper)
            crops.append(page.crop(CROP))
            crops[-1].save(out.with_name(f'{tag}.png'))
            if args.full:
                page.save(out.with_name(f'{tag}-page.png'))
            bands.append(page.crop((36, BAND_TOP + BAND_WINDOW_TOP, 648,
                                    BAND_TOP + BAND_WINDOW_TOP + BAND_CROP_HEIGHT)))
        sheet = Image.new('L', (684, crops[0].height * len(crops) + 2 * (len(crops) - 1)), 128)
        for i, image in enumerate(crops):
            sheet.paste(image, (0, i * (image.height + 2)))
        sheet.save(out)

    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        write_shims(work)
        compile_unit(work, panel_unit())
        for tag, selected, vertical, kind in (('picker-plain-selected', 3, 0, 1),
                                              ('picker-emblem-selected', 0, 0, 1),
                                              ('picker-vertical-note', 3, 1, 1)):
            target = work / f'panel-{tag}.raw'
            subprocess.run([str(work / 'test'), str(args.font.resolve()), str(selected),
                            str(vertical), str(kind), str(target), str(args.scroll)], check=True)
            suffix = '' if not args.scroll else f'-scroll{args.scroll}'
            paper(Image.frombytes('L', (684, 1216), target.read_bytes())).save(
                out.with_name(f'{tag}{suffix}.png'))

    print(f"saved {out}, {len(STYLES)} single-style PNGs and 3 picker renders next to it"
          + (f" (size {args.size or 'source'}, band {args.band or 'source'})" if args.size or args.band else ""))


if __name__ == '__main__':
    main()
