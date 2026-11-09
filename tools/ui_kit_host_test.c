/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用主机像素缓冲验证按压背景和刷新区域。
 * English: Verify pressed backgrounds and refresh regions using a host pixel buffer.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "ui_kit.h"
#include "ui_font.h"
#include "ttf_font.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define W 100
#define H 100
static enum EpdRotation rotation = EPD_ROT_LANDSCAPE;
static bool painted_ttf;
static bool active_ready = true, active_builtin;
static int active_face = 1, painted_face;
bool ttf_font_ready(void) { return active_ready; }
bool ttf_font_is_builtin(void) { return active_builtin; }
bool ttf_font_has_text(const char *text) { return active_ready && text && !strchr(text, 'Z'); }
static int painted_baseline;
static int painted_px;
static int characters(const char *s) {
    int n = 0;
    for (; *s; ++s) if (((unsigned char)*s & 0xc0) != 0x80) ++n;
    return n;
}
bool ui_font_has_text(const char *text) {
    for (; *text; ++text) if ((unsigned char)*text >= 0x80) return false;
    return true;
}
int ui_font_text_width_px(int px, const char *text) { return characters(text) * px / 2; }
int ui_font_title_px(int px, const char *text) { return strchr(text, 'Z') ? 24 : px; }
bool ui_font_title_uses_supplement(const char *text) { return strchr(text, 'Z') != NULL; }
int ttf_text_width_px(int px, const char *text) { return characters(text) * px * 3 / 4; }
void ui_font_measure_line_px(int px, const char *text, int *above, int *below) {
    (void)text; *above = px * 3 / 4; *below = px / 4;
}
void ttf_measure_line_px(int px, const char *text, int *above, int *below) {
    (void)text; *above = px * 2 / 3; *below = px / 3;
}
// 与真实栅格一致，抗锯齿默认为开；ui_kit 绘制期间临时改写再还原。
// Default on like the real raster; ui_kit rewrites it for a draw and restores it.
static bool antialias = true;
void ttf_set_antialias(bool enabled) { antialias = enabled; }
bool ttf_get_antialias(void) { return antialias; }
// 合成加粗用的临时笔画：这里只需记录读写，字模由各用例自己的桩决定。
// Scoped stroke for synthetic bold: record the value; the glyph stubs stay per-case.
static int stroke_adjust;
int ttf_get_stroke_adjust_fine(void) { return stroke_adjust; }
void ttf_set_stroke_adjust_fine_scoped(int steps) { stroke_adjust = steps; }
void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                          enum EpdFontFlags align, uint8_t fg, uint8_t bg, bool bw) {
    (void)fb; (void)x; (void)text; (void)align; (void)fg; (void)bg; (void)bw;
    painted_ttf = false; painted_baseline = baseline; painted_px = px;
}
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)x; (void)text; (void)align; (void)fg; (void)bg;
    painted_ttf = true; painted_baseline = baseline;
    painted_px = px;
    painted_face = active_face;
}
void ui_font_draw_title_px(uint8_t *fb, int x, int baseline, int px,
                           const char *text, enum EpdFontFlags align) {
    ui_font_draw_text_px(fb, x, baseline, px, text, align, 0, 15, true); painted_px = px;
}
void ttf_draw_text_px_bw(uint8_t *fb, int x, int baseline, int px, const char *text,
                        enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    assert(fg == 0 && bg == 15);
    ttf_draw_text_px(fb, x, baseline, px, text, align, fg, bg); painted_px = px;
}
// ASCII 前缀也须采用整条输入实际使用的回退字体。/ ASCII prefixes must use the full input's fallback face.
static void test_font_context(void) {
    uint8_t fb = 0;
    ui_text_set_system_font(false);
    assert(ui_text_fixed_context_width_px(24, "AB", "AB你好") == 36);
    assert(ui_text_fixed_context_width_px(24, "AB", "ABC") == 24);
    assert(ui_text_fixed_context_width_px(24, "", "") == 0);
    assert(ui_text_fixed_width_px(24, "AB") == 24);
    ui_text_fixed_context_vc(&fb, 0, 50, 24, "AB", "AB你好", EPD_DRAW_ALIGN_LEFT, false);
    assert(painted_ttf && painted_baseline == 54);
    ui_text_fixed_context_vc(&fb, 0, 50, 24, "AB", "ABC", EPD_DRAW_ALIGN_LEFT, false);
    assert(!painted_ttf && painted_baseline == 56);
    ui_text_set_system_font(true);
    assert(ui_text_fixed_context_width_px(24, "AB", "ABC") == 36);
    ui_text_fixed_vc(&fb, 0, 50, 24, "AB", EPD_DRAW_ALIGN_LEFT, false);
    assert(painted_ttf);
    ui_text_set_system_font(false);
}
static void test_native_titles(void) {
    uint8_t fb = 0;
    ui_text_set_system_font(false);
    // 阅读字体已有轮廓，哪怕内建UI有这些字，也应直接用阅读字体和原字号。
    // Use the existing reader outline and original size even when built-in UI glyphs cover the text.
    active_face = 1;
    char title[64] = "ABZ";
    int px = ui_text_title_fit(title, 32, 48, "ABZ");
    assert(px == 32 && !strcmp(title, "AB"));
    ui_text_title_vc(&fb, 0, 50, px, title, "ABZ", EPD_DRAW_ALIGN_LEFT);
    assert(painted_ttf && painted_face == 1 && painted_px == 32 && painted_baseline == 55);
    strcpy(title, "AB你好");
    px = ui_text_title_fit(title, 48, 72, "AB你好");
    assert(px == 48 && !strcmp(title, "AB"));
    ui_text_title_vc(&fb, 0, 50, px, title, "AB你好", EPD_DRAW_ALIGN_LEFT);
    assert(painted_ttf && painted_face == 1 && painted_px == 48);
    ui_text_set_system_scale(false);
    ui_text_set_system_font(true);
    active_face = 2;
    strcpy(title, "ABZ");
    px = ui_text_title_fit(title, 40, 120, "ABZ");
    assert(px == 40 && !strcmp(title, "ABZ"));
    ui_text_title_vc(&fb, 0, 50, px, title, "ABZ", EPD_DRAW_ALIGN_LEFT);
    assert(painted_ttf && painted_face == 2 && painted_px == 40);
    ui_text_set_system_scale(true);
    ui_text_title_vc(&fb, 0, 50, px, title, "ABZ", EPD_DRAW_ALIGN_LEFT);
    assert(painted_face == 2 && painted_px == 40);
    active_builtin = true;
    strcpy(title, "ABZ");
    px = ui_text_title_fit(title, 40, 24, "ABZ");
    assert(px == 24 && !strcmp(title, "AB"));
    ui_text_title_vc(&fb, 0, 50, px, title, "ABZ", EPD_DRAW_ALIGN_LEFT);
    assert(!painted_ttf && painted_px == 24);
    active_ready = false;
    assert(ui_text_title_fit(title, 40, 24, "ABZ") == 24);
    active_ready = true; active_builtin = false;
    ui_text_set_system_font(false);
}
int epd_width(void) { return W; }
int epd_height(void) { return H; }
// 测试缓冲为正方形，旋转不改变可用尺寸。/ The test buffer is square, so rotation never changes the usable size.
int epd_rotated_display_width(void) { return W; }
int epd_rotated_display_height(void) { return H; }
enum EpdRotation epd_get_rotation(void) { return rotation; }
static void physical(int *x, int *y) {
    int ox = *x, oy = *y;
    if (rotation == EPD_ROT_PORTRAIT) { *x = W - oy - 1; *y = ox; }
    else if (rotation == EPD_ROT_INVERTED_LANDSCAPE) { *x = W - ox - 1; *y = H - oy - 1; }
    else if (rotation == EPD_ROT_INVERTED_PORTRAIT) { *x = oy; *y = H - ox - 1; }
}
// 面板帧缓冲按 4 位打包：偶数 x 存低半字节，奇数 x 存高半字节，与 epdiy 相同。
// The panel framebuffer packs 4bpp: even x in the low nibble, odd x in the high nibble, as in epdiy.
#define FB_STRIDE (W / 2)
// 读回 4 位灰阶的 8 位字节值（n*17），使原断言的量级保持可读。
// Read a 4-bit level back as its 8-bit byte value (n*17) so the original assertions stay readable.
static uint8_t fb_level(const uint8_t *fb, int x, int y) {
    const uint8_t packed = fb[(size_t)y * FB_STRIDE + x / 2];
    const uint8_t nibble = (x % 2) ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0f);
    return (uint8_t)(nibble * 17);
}
uint8_t epd_get_pixel(int x, int y, int width, int height, const uint8_t *fb) {
    assert(width == W && height == H && x >= 0 && x < W && y >= 0 && y < H);
    const uint8_t packed = fb[(size_t)y * FB_STRIDE + x / 2];
    return (x % 2) ? (uint8_t)(packed & 0xf0) : (uint8_t)((packed & 0x0f) << 4);
}
uint8_t app_settings_system_contrast(void) { return 100; }
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    physical(&x, &y);
    assert(x >= 0 && x < W && y >= 0 && y < H);
    uint8_t *cell = &fb[(size_t)y * FB_STRIDE + x / 2];
    *cell = (x % 2) ? (uint8_t)((*cell & 0x0f) | (color & 0xf0))
                    : (uint8_t)((*cell & 0xf0) | (color >> 4));
}
void epd_fill_rect(EpdRect r, uint8_t color, uint8_t *fb) {
    for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x) epd_draw_pixel(x, y, color, fb);
}
void epd_fill_circle_helper(int x, int y, int radius, int corners, int delta, uint8_t color, uint8_t *fb) {
    (void)x; (void)y; (void)radius; (void)corners; (void)delta; (void)color; (void)fb;
}
static void rect_eq(EpdRect r, int x, int y, int w, int h) {
    assert(r.x == x && r.y == y && r.width == w && r.height == h);
}
// 检查实际尺寸、底色合成与四种旋转下的像素一致性。/ Check draw sizes, blending and all rotations.
static void test_icons(void) {
    uint8_t fb[W * H], reference[W * H];
    const int sizes[] = {26, 30, 32, 34, 38, 40, 44, 46, 48, 72};
    for (int icon = 0; icon < UI_ICON_COUNT; ++icon) {
        for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
            int size = sizes[s], left = 50 - size / 2;
            for (int light = 0; light < 2; ++light) {
                uint8_t background = light ? 0x66 : 0xee;
                rotation = EPD_ROT_LANDSCAPE;
                memset(reference, background, sizeof(reference));
                ui_draw_icon(reference, 50, 50, size, (ui_icon_t)icon, light ? 255 : 0);
                unsigned changes = 0;
                for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                    uint8_t value = fb_level(reference, x, y);
                    changes += value != background;
                    assert(light ? value >= background : value <= background);
                    if (x < left || y < left || x >= left + size || y >= left + size)
                        assert(value == background);
                }
                assert(changes > 0);
                for (int r = EPD_ROT_PORTRAIT; r <= EPD_ROT_INVERTED_PORTRAIT; ++r) {
                    rotation = (enum EpdRotation)r;
                    memset(fb, background, sizeof(fb));
                    ui_draw_icon(fb, 50, 50, size, (ui_icon_t)icon, light ? 255 : 0);
                    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                        int px = x, py = y; physical(&px, &py);
                        assert(fb_level(fb, px, py) == fb_level(reference, x, y));
                    }
                }
            }
        }
    }
    rotation = EPD_ROT_LANDSCAPE;
    memset(fb, 0xe0, sizeof(fb)); memcpy(reference, fb, sizeof(fb));
    ui_draw_icon(fb, 50, 50, 40, UI_ICON_COUNT, 0);
    ui_draw_icon(fb, 50, 50, 0, UI_ICON_WIFI, 0);
    ui_draw_icon(NULL, 50, 50, 40, UI_ICON_WIFI, 0);
    assert(!memcmp(fb, reference, sizeof(fb)));
    ui_draw_icon(fb, 50, 50, 40, UI_ICON_BOOKMARK, 0);
    ui_draw_icon(reference, 50, 50, 40, UI_ICON_BOOKMARK_CHECK, 0);
    assert(memcmp(fb, reference, sizeof(fb)) != 0);
}
int main(void) {
    rect_eq(ui_rect_union((EpdRect){0}, (EpdRect){10,20,30,40}), 10,20,30,40);
    rect_eq(ui_rect_union((EpdRect){10,20,30,40}, (EpdRect){5,30,50,10}), 5,20,50,40);
    rect_eq(ui_rect_union((EpdRect){-10,-20,30,40}, (EpdRect){0}), 0,0,20,20);
    rect_eq(ui_rect_union((EpdRect){680,1210,INT_MAX,INT_MAX}, (EpdRect){0}), 680,1210,4,6);
    rect_eq(ui_rect_union((EpdRect){INT_MIN,INT_MIN,10,10}, (EpdRect){0}), 0,0,0,0);
    uint8_t fb[W * H]; memset(fb, UI_GRAY_WHITE, sizeof(fb));
    ui_draw_control_frame(fb,(EpdRect){10,10,60,40},0,0xa0);
    assert(fb_level(fb,30,10)<128&&fb_level(fb,30,11)<128&&fb_level(fb,30,12)==255);
    ui_draw_separator(fb,70,10,60,0xa0);
    assert(fb_level(fb,30,70)<128&&fb_level(fb,30,71)<128&&fb_level(fb,30,72)==255);
    ui_draw_round_rect(fb,(EpdRect){80,10,10,40},0,0xa0);assert(fb_level(fb,85,10)>=128);
    memset(fb,UI_GRAY_WHITE,sizeof(fb));
    ui_draw_pressed_round_rect(fb, (EpdRect){10,10,60,40}, 0);
    assert(fb_level(fb,40,30) == (uint8_t)((UI_GRAY_LIGHT >> 4) * 17));
    assert(fb_level(fb,14,30) == UI_GRAY_BLACK && fb_level(fb,16,30) == UI_GRAY_BLACK);
    assert(fb_level(fb,9,30) == UI_GRAY_WHITE);
    // 按压底图之后绘制黑字像素，文字不反色。/ Draw black text pixels after the pressed background, without inversion.
    epd_draw_pixel(40,30,UI_GRAY_BLACK,fb); assert(fb_level(fb,40,30) == UI_GRAY_BLACK);
    // 控件使用三色，原始绘图仍保留每档灰阶。/ Controls use three targets while raw drawing retains every gray level.
    for (unsigned tone=0;tone<256;++tone) {
        const uint8_t expected=tone<128?0:tone>=240?255:144;
        assert(ui_control_gray((uint8_t)tone)==expected);
        ui_fill_round_rect(fb,(EpdRect){10,10,60,40},8,(uint8_t)tone);
        assert(fb_level(fb,40,30)==(uint8_t)((expected>>4)*17));
        ui_fill_round_rect_raw(fb,(EpdRect){10,10,60,40},8,(uint8_t)tone);
        assert(fb_level(fb,40,30)==(uint8_t)((tone>>4)*17));
    }
    test_icons();
    test_font_context();
    test_native_titles();
    puts("ui kit host tests passed");
}
