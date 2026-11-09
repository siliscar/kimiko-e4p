/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：阅读背景的唯一缓存与绘制。解码后先做白点归一（≥240 归纯白），缓存按面板
 * 扫描序存放，混合用 256 项半字节查表，翻页热路径里不再有逐像素函数调用。
 *
 * English: The sole reading-background cache and painter. Decoded paper is white-point
 * clamped (>=240 becomes pure white) before the cache is packed in panel scan order;
 * blending uses a 256-entry nibble LUT, so the turn hot path makes no per-pixel calls.
 *
 * 冻结：仅一个选中背景，不保留两份图片；混合公式 (bg*ink+7)/15 不变。
 * Frozen: one selected wallpaper, never two image caches; the blend formula
 * (bg*ink+7)/15 never changes.
 */
#include "../diagnostics/reader_memory.h"
#include "ui_reader_background.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "image_decode.h"
#include "esp_heap_caps.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
static uint8_t *s_gray;
static unsigned s_width, s_height;
static int s_rotation = -1;
static char s_path[288];
static off_t s_size;
static time_t s_mtime;
// 白点归一：纸面统一成纯白，近白 JPEG 噪声不再被抖动成 14/15 灰点。
// White-point clamp: paper snaps to pure white so near-white JPEG noise cannot dither
// into a 14/15 speckle. 240 only touches the top 6% of the input range.
#define READER_BG_WHITE_CLAMP 240
static uint8_t s_white_lut[256];
static bool s_white_lut_ready;

static void white_lut_init(void) {
    for (unsigned g = 0; g < 256; ++g)
        s_white_lut[g] = (uint8_t)(g >= READER_BG_WHITE_CLAMP ? 255 : g);
    s_white_lut_ready = true;
}
// 查表混合：索引 (bg<<4)|ink，值 (bg*ink+7)/15；ink=0 得 0，与旧的「纯黑跳过」逐位一致。
// LUT blend: index (bg<<4)|ink, value (bg*ink+7)/15; ink=0 yields 0, bit-identical to the
// old skip-and-keep-black path.
static uint8_t s_blend_lut[256];
static bool s_blend_lut_ready;

static void blend_lut_init(void) {
    for (unsigned bg = 0; bg < 16; ++bg)
        for (unsigned ink = 0; ink < 16; ++ink)
            s_blend_lut[(bg << 4) | ink] = (uint8_t)((bg * ink + 7) / 15);
    s_blend_lut_ready = true;
}
void ui_reader_background_release(void) {
    free(s_gray); s_gray = NULL; s_path[0] = 0; s_width = s_height = 0; s_rotation = -1;
}
// 逻辑坐标 → 面板半字节序号；旋转映射与 epd_draw_pixel/_get_pixel 一致（偶数位为低半字节）。
// Logical coords to a panel nibble index; the rotation matches epd_draw_pixel/_get_pixel
// (even positions pack into the low nibble).
static size_t panel_nibble(int x, int y) {
    int px = x, py = y;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT: px = epd_width() - y - 1; py = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: px = epd_width() - x - 1; py = epd_height() - y - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: px = y; py = epd_height() - x - 1; break;
        default: break;
    }
    return (size_t)py * epd_width() + px;
}
bool ui_reader_background_prepare(const char *path) {
    struct stat st;
    if (!path || !path[0] || strncmp(path, "/sdcard/pictures/", 17) ||
        strnlen(path, sizeof(s_path)) >= sizeof(s_path) || stat(path, &st) ||
        !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) {
        ui_reader_background_release(); return false;
    }
    const unsigned w = epd_rotated_display_width(), h = epd_rotated_display_height();
    if (!w || !h || w > 1216 || h > 1216 || (uint64_t)w * h > 684u * 1216u) {
        ui_reader_background_release(); return false;
    }
    const int rotation = (int)epd_get_rotation();
    if (!strcmp(path, s_path) && s_gray && st.st_size == s_size && st.st_mtime == s_mtime &&
        s_width == w && s_height == h && s_rotation == rotation) return true;
    ui_reader_background_release(); // 仅一个选中背景，不保留两份图片。/ One selected wallpaper, never two image caches.
    const char *ext = strrchr(path, '.');
    bool png = ext && !strcasecmp(ext, ".png");
    unsigned source_w = 0, source_h = 0;
    if (!ext || (!png && strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg")) ||
        !book_image_file_dimensions(path, png, &source_w, &source_h) || !source_w || !source_h) return false;
    const size_t pixels = (size_t)w * h, packed = (pixels + 1) / 2;
    const unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    // 8 位临时面与面板序 4 位成品并存解码；常驻仍只有 packed 一份。
    // The 8-bit temp plane and the panel-order 4-bit result coexist during packing;
    // only the packed half stays resident afterwards.
    if (heap_caps_get_free_size(caps) < pixels + packed + 1024u * 1024u ||
        heap_caps_get_largest_free_block(caps) < pixels) return false;
    uint8_t *plane = heap_caps_malloc(pixels, caps);
    uint8_t *out = plane ? heap_caps_calloc(packed, 1, caps) : NULL;
    if (!plane || !out || !book_image_file_grayscale(path, png, w, h, plane)) {
        free(plane); free(out); ui_reader_background_release(); return false;
    }
    // 白点归一后再抖动：纸面落在纯白，装饰灰阶原样通过；抖动仍在逻辑坐标定形，半字节按面板序落位。
    // White-point clamp feeds the dither: paper lands on pure white while decorative grays
    // pass through; dithering keeps logical coordinates and nibbles land in panel order.
    if (!s_white_lut_ready) white_lut_init();
    for (size_t i = 0; i < pixels; ++i) {
        const int x = (int)(i % w), y = (int)(i / w);
        const unsigned nib = ui_image_dither_gray(s_white_lut[plane[i]], x, y) >> 4;
        const size_t at = panel_nibble(x, y);
        out[at >> 1] |= at & 1 ? (uint8_t)(nib << 4) : (uint8_t)nib;
    }
    free(plane);
    s_gray = out;
    s_width = w; s_height = h; s_rotation = rotation; s_size = st.st_size; s_mtime = st.st_mtime;
    strcpy(s_path, path); return true;
}

void ui_reader_background_paint(uint8_t *fb, EpdRect area, bool blend) {
    if (!s_gray || !fb || area.x < 0 || area.y < 0 || area.width <= 0 || area.height <= 0 ||
        area.x + area.width > (int)epd_rotated_display_width() ||
        area.y + area.height > (int)epd_rotated_display_height()) return;
    if (blend && (s_width != epd_rotated_display_width() ||
                  s_height != epd_rotated_display_height())) return;
    if (!blend) {
        // 缩略图只服务设置预览：按屏幕坐标采样，落位仍走通用绘制原语。
        // The thumbnail serves the settings preview only: sample in screen coordinates
        // and keep writing through the generic primitive.
        for (int y = 0; y < area.height; ++y) for (int x = 0; x < area.width; ++x) {
            const unsigned sx = (unsigned)((uint64_t)x * s_width / area.width);
            const unsigned sy = (unsigned)((uint64_t)y * s_height / area.height);
            const size_t at = panel_nibble((int)sx, (int)sy);
            const unsigned gray = (s_gray[at >> 1] >> ((at & 1) * 4)) & 15;
            epd_draw_pixel(area.x + x, area.y + y, gray << 4, fb);
        }
        return;
    }
    if (!s_blend_lut_ready) blend_lut_init();
    if (area.x == 0 && area.y == 0 &&
        area.width == (int)epd_rotated_display_width() &&
        area.height == (int)epd_rotated_display_height()) {
        // 全屏热路径：缓存与帧缓冲同面板序，按字节顺序扫一遍，每字节两次查表。
        // 纯黑位写 0 与旧的「跳过」同值；曾按逻辑行步进会让两个缓冲都跨缓存行，更慢。
        // Full-screen hot path: the cache shares the framebuffer's panel order, so one
        // sequential byte pass needs two lookups per byte. Writing 0 over pure black
        // matches the old skip exactly; walking logical rows instead strided both
        // buffers across cache lines and ran slower.
        const size_t bytes = ((size_t)epd_width() * epd_height() + 1) / 2;
        for (size_t i = 0; i < bytes; ++i) {
            const uint8_t f = fb[i], b = s_gray[i];
            fb[i] = (uint8_t)((s_blend_lut[(b & 0xF0) | (f >> 4)] << 4) |
                              s_blend_lut[((b << 4) & 0xF0) | (f & 15)]);
        }
        return;
    }
    // 局部区域（状态栏带等小面积）保持逻辑行步进遍历。
    // Partial areas (status bands and other small strips) keep the logical-row walk.
    const int pw = (int)epd_width(), ph = (int)epd_height();
    ptrdiff_t dx, dy;
    size_t nib;
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:
            dx = pw; dy = -1; nib = (size_t)area.x * pw + (pw - area.y - 1); break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            dx = -1; dy = -pw; nib = (size_t)(ph - area.y - 1) * pw + (pw - area.x - 1); break;
        case EPD_ROT_INVERTED_PORTRAIT:
            dx = -pw; dy = 1; nib = (size_t)(ph - area.x - 1) * pw + area.y; break;
        default:
            dx = 1; dy = pw; nib = (size_t)area.y * pw + area.x; break;
    }
    for (int y = 0; y < area.height; ++y, nib += (size_t)dy) {
        for (size_t at = nib, x = 0; x < area.width; ++x, at += (size_t)dx) {
            uint8_t *const byte = fb + (at >> 1);
            const unsigned sh = (at & 1) * 4;
            const unsigned ink = (*byte >> sh) & 15;
            if (!ink) continue; // 纯黑文字保持原样。/ Pure-black ink stays untouched.
            const unsigned bg = (s_gray[at >> 1] >> sh) & 15;
            *byte = (uint8_t)((*byte & (sh ? 0x0F : 0xF0)) |
                              ((unsigned)s_blend_lut[(bg << 4) | ink] << sh));
        }
    }
}


#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
void ui_reader_background_memory_stats(reader_memory_stats_t *stats) {
    stats->background = reader_memory_owned(s_gray);
}
#endif
