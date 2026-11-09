#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：锁定题头样式的两条硬约束——四种样式都落在排版预留的题头带内；
#      默认「静页余白」与历史渲染逐像素一致（老用户升级画面不变）。
# English: Lock the two hard promises of the heading styles -- every style stays inside the band the
# layout reserves, and the plain default is pixel-identical to the historical rendering so upgrading
# changes nothing on screen.
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


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


book = ROOT / 'main/ui/ui_reader_heading.c'
heading_header = ROOT / 'main/ui/ui_reader_heading.h'
kit = ROOT / 'main/ui/ui_kit.c'
settings = ROOT / 'main/settings/settings.h'
source = book.read_text() + heading_header.read_text()

# 样式表从真实源码取，序号与固件一致。
# Take the style table from the real source so the indices cannot drift from the firmware.
styles = re.search(r'typedef enum \{\s*APP_BOOK_TITLE_STYLE_EMBLEM.*?\} app_book_title_style_t;',
                   settings.read_text(), re.S)
assert styles, 'heading style enum'
band = re.search(r'#define READER_HEADING_BAND_HEIGHT \d+', source)
assert band, 'heading band height'
band_height = int(band.group(0).split()[-1])


def numeric(name, path):
    """从真实源码取数值常量：字号档位与带顶边距都不在测试里另抄一份。"""
    found = re.search(r'#define ' + name + r' (\d+)', path.read_text())
    assert found, name
    return int(found.group(1))


size_min = numeric('APP_BOOK_TITLE_SIZE_MIN', settings)
size_max = numeric('APP_BOOK_TITLE_SIZE_MAX', settings)
size_step = numeric('APP_BOOK_TITLE_SIZE_STEP', settings)
size_default = numeric('APP_BOOK_TITLE_SIZE_DEFAULT', settings)
block_top = numeric('READER_HEADING_BLOCK_TOP', heading_header)
block_height = numeric('READER_HEADING_BLOCK_HEIGHT', heading_header)
bold_px = numeric('READER_HEADING_BOLD_PX', heading_header)
bold_steps = numeric('UI_TITLE_BOLD_STEPS', kit)
LEVELS = list(range(size_min, size_max + 1, size_step))

with tempfile.TemporaryDirectory() as folder:
    p = Path(folder)
    (p / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
    (p / 'esp_log.h').write_text('#pragma once\nstatic inline void host_log(const char *t,const char *f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) host_log(__VA_ARGS__)\n#define ESP_LOGW(...) host_log(__VA_ARGS__)\n#define ESP_LOGE(...) host_log(__VA_ARGS__)\n')
    (p / 'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
    (p / 'esp_heap_caps.h').write_text('#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\nvoid *heap_caps_calloc(size_t,size_t,int);\nvoid *heap_caps_realloc(void*,size_t,int);\nvoid heap_caps_free(void*);\nsize_t heap_caps_get_free_size(int);\nsize_t heap_caps_get_largest_free_block(int);\n')
    (p / 'epdiy.h').write_text((ROOT / 'tools/ui_gesture_stubs/epdiy.h').read_text())
    builtin = (ROOT / 'main/assets/builtin.ttf').read_bytes()
    assets = '#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={' + ','.join(map(str, builtin)) + '};\n'
    assets += f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(builtin)}");\n'
    (p / 'assets.c').write_text(assets)

    unit = r'''
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
#define UI_GRAY_LIGHT 192
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
// 宿主只走 TTF：内建位图字体分支换成同尺寸的真字体调用。
// The host goes through the TTF only: builtin-bitmap branches are redirected to the real face.
static bool s_active_face;
static bool title_uses_active_font(const char *s){(void)s;return s_active_face;}
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
uint8_t ui_contrast_gray(uint8_t gray);
void ui_fill_rect_fast(EpdRect rect,uint8_t gray,uint8_t *framebuffer);
'''
    unit += function('ui_control_gray',ROOT/'main/ui/ui_kit.h')+'\n'
    for n in ('_rotate', 'epd_draw_pixel', 'epd_get_pixel', 'epd_draw_hline', 'epd_draw_vline',
              'epd_fill_rect', 'epd_draw_circle', 'epd_fill_circle_helper', 'epd_fill_circle',
              'epd_write_line', 'epd_draw_line'):
        unit += function(n, ROOT / 'components/epdiy/src/epdiy.c') + '\n'
    for n in ('clamp_radius', 'draw_arc', 'ui_draw_round_rect_raw', 'ui_draw_round_rect',
        'ui_fill_round_rect_raw', 'ui_fill_round_rect', 'ui_inset_rect',
              'ui_draw_control_frame', 'ui_draw_separator', 'ui_hairline', 'ui_content_width'):
        unit += function(n, kit) + '\n'
    unit += f'#define UI_TITLE_BOLD_STEPS {bold_steps}\n'
    for n in ('title_face_px', 'ui_text_title_fit_sized', 'ui_text_title_fit',
              'ui_text_title_background_vc', 'ui_text_title_vc',
              'ui_text_title_bold_vc', 'ui_text_title_measure_sized', 'ui_text_title_measure',
              'ui_text_title_width_px', 'ui_text_title_line_height_px',
              'ui_text_title_ink_vc'):
        unit += function(n, kit) + '\n'
    unit += styles.group(0) + '\n'
    unit += band.group(0) + '\n'
    unit += f'#define READER_HEADING_BLOCK_TOP {block_top}\n'
    unit += f'#define READER_HEADING_BLOCK_HEIGHT {block_height}\n'
    unit += f'#define READER_HEADING_BOLD_PX {bold_px}\n'
    # 折行参数也取真实源码：缩字号下限与两行间距不另抄。
    # Wrapping constants come from the real source too, so the floor and the gap cannot drift.
    for macro in re.findall(r'#define READER_HEADING_(?:WRAP_\w+|TITLE_BYTES|LABEL_BYTES) \d+', source):
        unit += macro + '\n'
    unit += f'#define BAND_HEIGHT_EXPECTED {band_height}\n'
    unit += ''.join(m + '\n' for m in re.findall(r'#define APP_BOOK_TITLE_SIZE_\w+ \d+',
                                                settings.read_text()))
    for n in ('copy_text', 'reader_heading_band', 'heading_len', 'heading_line', 'reader_heading_fit',
              'draw_heading_plain', 'draw_heading_emblem', 'draw_heading_frame', 'draw_heading_mark',
              'draw_heading_single', 'heading_prefix_width', 'heading_wrap_break',
              'heading_truncate_px', 'draw_heading_wrapped', 'draw_reader_title_style'):
        unit += function(n, book) + '\n'
    unit += r'''
uint8_t ui_contrast_gray(uint8_t gray){return gray;}
void ui_fill_rect_fast(EpdRect rect,uint8_t gray,uint8_t *framebuffer){epd_fill_rect(rect,gray,framebuffer);}
// 改样式之前 draw_reader_outer 里那段横排题头：默认样式必须与它逐像素一致。
// 章名走合成加粗，所以基准也按同一截短宽度与加粗绘制。
// The inline horizontal heading draw_reader_outer used before styles existed: the default style has
// to match it pixel for pixel. The title is synthetically bold, so the baseline fits and paints it
// the same way.
static void historical_plain(uint8_t *fb,EpdRect body){
    char heading[128];copy_text(heading,sizeof(heading),s_chapter_heading_title);
    int heading_px=ui_text_title_fit(heading,48,ui_content_width()-READER_HEADING_BOLD_PX,s_chapter_heading_title);
    char label[sizeof(s_chapter_heading_label)];copy_text(label,sizeof(label),s_chapter_heading_label);
    int label_px=ui_text_title_fit(label,24,ui_content_width(),s_chapter_heading_label);
    ui_text_title_vc(fb,UI_LOCK_WIDTH/2,body.y+29,label_px,label,s_chapter_heading_label,EPD_DRAW_ALIGN_CENTER);
    ui_text_title_bold_vc(fb,UI_LOCK_WIDTH/2,body.y+112,heading_px,heading,s_chapter_heading_title,EPD_DRAW_ALIGN_CENTER);
    ui_hairline(fb,body.y+174,210,264,UI_GRAY_LIGHT);
}
static const size_t FB_BYTES=(size_t)UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2;
// 48 档的题头就是把历史那块整体移到带心：两帧必须只差这个纵向位移。
// 平移在逻辑坐标里比：帧缓冲是旋转过的，整行搬字节并不等于图像下移。
// The 48 px level is the historical block moved to the band's centre, so the two frames may differ
// by that vertical offset alone. The comparison runs in logical coordinates: the framebuffer is
// rotated, so copying whole byte rows is not an image shift.
static const int CENTER_SHIFT=(READER_HEADING_BAND_HEIGHT-READER_HEADING_BLOCK_HEIGHT)/2-READER_HEADING_BLOCK_TOP;
static uint8_t pixel_at(const uint8_t *frame,int x,int y){int px=x,py=y;switch(epd_get_rotation()){case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;default:break;}return epd_get_pixel(px,py,epd_width(),epd_height(),frame);}
// 4bpp 的纸色经 epd_get_pixel 就是这个值；位移带出画面的行必须是纸色。
// The 4bpp paper value as epd_get_pixel reports it; rows the shift moves off the frame must be paper.
static const uint8_t FRAME_PAPER=240;
static bool equals_shifted(const uint8_t *moved,const uint8_t *origin,int dy){
    for(int y=0;y<UI_LOCK_HEIGHT;++y)for(int x=0;x<UI_LOCK_WIDTH;++x){
        int oy=y-dy;
        uint8_t want=(oy>=0&&oy<UI_LOCK_HEIGHT)?pixel_at(origin,x,oy):FRAME_PAPER;
        if(pixel_at(moved,x,y)!=want)return false;
    }
    return true;
}
// 墨迹范围：题头带内的最上、最下、最左、最右非白像素；阈值用来区分纯黑与任意灰。
// Ink extents: the topmost, lowest, leftmost and rightmost non-white pixel of the heading band; the
// threshold separates solid black from any gray.
static void ink_bounds_at(const uint8_t *fb,int limit,int *top,int *bottom,int *left,int *right){
    *top=INT32_MAX;*bottom=-1;*left=INT32_MAX;*right=-1;
    for(int y=0;y<UI_LOCK_HEIGHT;++y)for(int x=0;x<UI_LOCK_WIDTH;++x)
        if(pixel_at(fb,x,y)<limit){
            if(y<*top)*top=y;if(y>*bottom)*bottom=y;
            if(x<*left)*left=x;if(x>*right)*right=x;
        }
}
static void ink_bounds(const uint8_t *fb,int *top,int *bottom,int *left,int *right){
    ink_bounds_at(fb,15,top,bottom,left,right);
}
struct heading_case_t { const char *label; const char *title; };
static const struct heading_case_t CASES[]={
    {"第十二章","远方来信"},
    {"","序章"},
    {"第十二章","一个相当长的章节名称需要在带宽内截短之后再画出来"},
    {"Chapter 12","The Long Way Home"},
};
int main(int argc,char **argv){
    assert(argc==2);assert(ttf_font_open(argv[1])==ESP_OK);
    uint8_t *styled=malloc(FB_BYTES),*plain=malloc(FB_BYTES);
    assert(styled&&plain);
    const EpdRect band=reader_heading_band(176);
    assert(band.height==READER_HEADING_BAND_HEIGHT);
    assert(band.height==BAND_HEIGHT_EXPECTED);
    unsigned combos=0;
    for(unsigned c=0;c<sizeof(CASES)/sizeof(CASES[0]);++c){
        snprintf(s_chapter_heading_label,sizeof(s_chapter_heading_label),"%s",CASES[c].label);
        snprintf(s_chapter_heading_title,sizeof(s_chapter_heading_title),"%s",CASES[c].title);
        int previous[APP_BOOK_TITLE_STYLE_COUNT]={0};
        for(int size=APP_BOOK_TITLE_SIZE_MIN;size<=APP_BOOK_TITLE_SIZE_MAX;size+=APP_BOOK_TITLE_SIZE_STEP){
            for(int style=0;style<APP_BOOK_TITLE_STYLE_COUNT;++style){
                memset(styled,0xff,FB_BYTES);
                draw_reader_title_style(styled,band,style,size,s_chapter_heading_label,s_chapter_heading_title);
                int top,bottom,left,right;ink_bounds(styled,&top,&bottom,&left,&right);
                assert(bottom>=0);
                // 任何档位、任何样式都必须留在带内：上边界是带的顶边距，下边界是带底。
                // Any level and any style has to stay inside the band: the top margin above, the
                // band's bottom edge below.
                if(top<band.y||bottom>band.y+band.height||
                   left<band.x||right>band.x+band.width){
                    fprintf(stderr,"size %d style %d case %u out of band: y %d..%d x %d..%d\n",
                            size,style,c,top,bottom,left,right);
                    assert(0);
                }
                // 字号真的作用到画面上：同一文案的墨迹随档位单调变高。
                // The size really reaches the pixels: the same input's ink grows with the level.
                if(size>APP_BOOK_TITLE_SIZE_MIN&&bottom-top<previous[style]){
                    fprintf(stderr,"size %d style %d shrank: %d < %d\n",size,style,bottom-top,previous[style]);
                    assert(0);
                }
                previous[style]=bottom-top;
                ++combos;
            }
        }
        // 默认档的静页余白 = 历史渲染整块下移到带心，字形与家具一个像素都不差。
        // 一行放不下的章名现在会折行，所以只在单行放得下时比对历史画面。
        // The plain style at the default level is the historical rendering moved to the band's
        // centre, with every glyph and piece of furniture unchanged. A title that no longer fits one
        // line now wraps, so the historical comparison applies only while one line still holds it.
        char fits[sizeof(s_chapter_heading_title)];
        copy_text(fits,sizeof(fits),s_chapter_heading_title);
        int fits_px=ui_text_title_fit(fits,APP_BOOK_TITLE_SIZE_DEFAULT,
                                      ui_content_width()-READER_HEADING_BOLD_PX,s_chapter_heading_title);
        if(fits_px==APP_BOOK_TITLE_SIZE_DEFAULT&&strlen(fits)==strlen(s_chapter_heading_title)){
            memset(styled,0xff,FB_BYTES);
            draw_reader_title_style(styled,band,APP_BOOK_TITLE_STYLE_PLAIN,APP_BOOK_TITLE_SIZE_DEFAULT,
                                    s_chapter_heading_label,s_chapter_heading_title);
            memset(plain,0xff,FB_BYTES);
            historical_plain(plain,band);
            if(!equals_shifted(styled,plain,CENTER_SHIFT)){
                fprintf(stderr,"case %u: the default level is not the historical block shifted by %d\n",c,CENTER_SHIFT);
                assert(0);
            }
        }
    }
    // 合成加粗（真实 TTF 路径）：覆盖率膨胀，单次抗锯齿；墨量至少多三成，四周各向扩展，
    // 画完恢复原来的阅读字重，不留下全局笔画状态。
    // Synthetic bold on the real TTF path: a coverage dilation rasterized once. Ink grows by at
    // least thirty percent, the bounds grow on every side, and the reader's stroke setting is
    // restored afterwards so no global state leaks.
    {
        const char *sample="远方来信";
        s_active_face=true;
        ttf_set_stroke_adjust_fine(0);
        memset(plain,0xff,FB_BYTES);
        ui_text_title_vc(plain,UI_LOCK_WIDTH/2,400,48,sample,sample,EPD_DRAW_ALIGN_CENTER);
        memset(styled,0xff,FB_BYTES);
        ui_text_title_bold_vc(styled,UI_LOCK_WIDTH/2,400,48,sample,sample,EPD_DRAW_ALIGN_CENTER);
        assert(ttf_get_stroke_adjust_fine()==0);
        int rt,rb,rl,rr,bt,bb,bl,br;
        ink_bounds_at(plain,FRAME_PAPER,&rt,&rb,&rl,&rr);
        ink_bounds_at(styled,FRAME_PAPER,&bt,&bb,&bl,&br);
        assert(rt>=0&&bt>=0);
        if(!(bl<=rl&&br>=rr&&bt<=rt&&bb>=rb)){
            fprintf(stderr,"bold lost an edge: %d..%d x %d..%d vs %d..%d x %d..%d\n",
                    bt,bb,bl,br,rt,rb,rl,rr);
            assert(0);
        }
        long regular_ink=0,bold_ink=0;
        for(int y=0;y<UI_LOCK_HEIGHT;++y)for(int x=0;x<UI_LOCK_WIDTH;++x){
            regular_ink+=FRAME_PAPER-pixel_at(plain,x,y);
            bold_ink+=FRAME_PAPER-pixel_at(styled,x,y);
        }
        // 至少 +28%：用户要求肉眼可辨，实测值见下方输出。
        // At least +28%: the user asked for a visible difference; the measured ratio is printed below.
        if(bold_ink*100<regular_ink*128){
            fprintf(stderr,"bold coverage %ld vs regular %ld (%ld%%)\n",
                    bold_ink,regular_ink,bold_ink*100/regular_ink);
            assert(0);
        }
        fprintf(stderr,"bold coverage +%ld%%\n",bold_ink*100/regular_ink-100);
        s_active_face=false;
    }
    // 含补充位图字面的行整行取补充基准（档位/2），所以编号永远不会比章名大。
    // A line carrying a bitmap supplement takes the supplement base (level/2), so the label can never
    // overtake the title.
    s_font_has_supplement=true;
    for(int size=APP_BOOK_TITLE_SIZE_MIN;size<=APP_BOOK_TITLE_SIZE_MAX;size+=APP_BOOK_TITLE_SIZE_STEP){
        char fit[sizeof(s_chapter_heading_title)];
        int title_px=reader_heading_fit(fit,sizeof(fit),"远方来信",size,ui_content_width(),size/2);
        int label_px=reader_heading_fit(fit,sizeof(fit),"第十二章",size/2,ui_content_width(),size/2);
        if(title_px!=size/2||label_px!=size/2){
            fprintf(stderr,"size %d: supplement lines came out %d/%d, expected %d\n",
                    size,title_px,label_px,size/2);
            assert(0);
        }
    }
    s_font_has_supplement=false;
    // 长章名折行：断点把整串分成两段非空文本，落在 UTF-8 字符边界上；短章名不折。
    // A long title's break splits the string into two non-empty parts on a character boundary; a
    // short title keeps one line.
    {
        const char *long_title="一个相当长的章节名称需要在带宽内截短之后再画出来";
        int width=ui_content_width()-READER_HEADING_BOLD_PX;
        int split=heading_wrap_break(long_title,72,width,long_title);
        assert(split>0&&split<(int)strlen(long_title));
        assert(((unsigned char)long_title[split]&0xc0)!=0x80);
        const char *rest=long_title+split;
        while(*rest==' ')++rest;
        assert(*rest);
        assert(heading_wrap_break("远方来信",48,width,"远方来信")==0);
        // 英文优先在空格处断开，第二行不从空格开始。
        // Latin text prefers a space, and the second line never starts with one.
        const char *latin="The Long Way Home";
        int en=heading_wrap_break(latin,48,180,latin);
        assert(en>0&&latin[en]==' '&&latin[en+1]!=' ');
    }
    printf("PASS: %u heading combinations (%u inputs x %d styles x %d sizes) inside the %d px band; the default %d px is the historical block shifted %d px to the band's centre, the title is synthetically bold, long titles wrap into two lines, supplement lines stay at level/2\n",
           combos,(unsigned)(sizeof(CASES)/sizeof(CASES[0])),APP_BOOK_TITLE_STYLE_COUNT,
           (APP_BOOK_TITLE_SIZE_MAX-APP_BOOK_TITLE_SIZE_MIN)/APP_BOOK_TITLE_SIZE_STEP+1,
           READER_HEADING_BAND_HEIGHT,APP_BOOK_TITLE_SIZE_DEFAULT,CENTER_SHIFT);
    free(styled);free(plain);
    return 0;
}
'''
    (p / 'test.c').write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-I' + str(p), '-I' + str(ROOT / 'main/ui'), '-I' + str(ROOT / 'main/font'),
             '-I' + str(ROOT / 'main'), "-I" + str(ROOT / "main/diagnostics"), "-I" + str(ROOT / "main/settings")]
    subprocess.run(flags + ['-c', str(ROOT / 'main/font/ttf_font.c'), '-o', str(p / 'ttf.o')], check=True)
    subprocess.run(flags + [str(p / 'test.c'), str(p / 'assets.c'), str(p / 'ttf.o'), str(ROOT / 'main/font/ttf_font_source.c'), str(ROOT / 'main/font/ttf_block_cache.c'), str(ROOT / 'main/font/ttf_glyph_cache.c'),
                            '-Wl,-dead_strip', '-lm', '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test'), str((ROOT / 'main/assets/builtin.ttf').resolve())], check=True)
