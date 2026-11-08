/* 中文：记录真实阅读设置模块的有序绘制调用，检查页面适配和设置快照。
 * English: Record ordered drawing calls from the real reader-settings module and check page adapters and snapshots.
 */
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ui_reader_settings.h"
#include "ui_kit.h"

static unsigned scale, pages, flags;
static uint8_t framebuffer[1];
static unsigned setting_reads, draws;
static bool recording = true;

static void record(const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (recording) vprintf(format, args);
    va_end(args);
    ++draws;
}
static void rect(const char *kind, uint8_t *fb, EpdRect r, int radius, uint8_t gray) {
    assert(fb == framebuffer && r.width > 0 && r.height > 0);
    record("%s %d %d %d %d %d %u\n", kind, r.x, r.y, r.width, r.height, radius, gray);
}
void ui_fill_round_rect(uint8_t *fb, EpdRect r, int radius, uint8_t gray) {
    rect("round", fb, r, radius, gray);
}
void ui_draw_control_frame(uint8_t *fb, EpdRect r, int radius, uint8_t gray) {
    rect("frame", fb, r, radius, gray);
}
void ui_fill_rect_fast(EpdRect r, uint8_t gray, uint8_t *fb) {
    rect("rect", fb, r, 0, gray);
}
void ui_text_box(uint8_t *fb, EpdRect r, int px, const char *text, enum EpdFontFlags align, bool inverted) {
    assert(fb == framebuffer && r.width > 0 && r.height > 0);
    record("text %d %d %d %d %d %d %d %s\n", r.x, r.y, r.width, r.height, px, align, inverted, text);
}
void ui_draw_icon(uint8_t *fb, int x, int y, int px, ui_icon_t icon, uint8_t gray) {
    assert(fb == framebuffer);
    record("icon %d %d %d %d %u\n", x, y, px, icon, gray);
}
void ui_hairline(uint8_t *fb, int y, int x, int width, uint8_t gray) {
    assert(fb == framebuffer);
    record("hairline %d %d %d %u\n", y, x, width, gray);
}
void epd_fill_circle(int x, int y, int r, uint8_t gray, uint8_t *fb) {
    assert(fb == framebuffer);
    record("fill_circle %d %d %d %u\n", x, y, r, gray);
}
void epd_draw_circle(int x, int y, int r, uint8_t gray, uint8_t *fb) {
    assert(fb == framebuffer);
    record("circle %d %d %d %u\n", x, y, r, gray);
}
static unsigned app_settings_system_font_size(void) { ++setting_reads; return scale; }
static unsigned app_settings_reader_full_pages(void) { ++setting_reads; return pages; }
static bool app_settings_reader_turn_effect(void) { ++setting_reads; return (flags & 1) != 0; }
static bool app_settings_reader_vertical_turn(void) { ++setting_reads; return (flags & 2) != 0; }
static bool app_settings_reader_shake_turn(void) { ++setting_reads; return (flags & 32) != 0; }
static bool app_settings_reader_statusbar(void) { ++setting_reads; return (flags & 4) != 0; }
static bool app_settings_reader_hide_images(void) { ++setting_reads; return (flags & 8) != 0; }
static bool app_settings_book_vertical(void) { ++setting_reads; return (flags & 16) != 0; }
#ifndef LEGACY
static bool app_settings_book_styles(void) { ++setting_reads; return (flags & 64) != 0; }
#endif
static void draw_sheet(uint8_t *fb, int top, const char *title) {
    assert(fb == framebuffer);
    record("sheet %d %s\n", top, title);
}
static void draw_sheet_back(uint8_t *fb, int top) {
    assert(fb == framebuffer);
    record("back %d\n", top);
}
#ifdef LEGACY
static void copy_text(char *to, size_t cap, const char *from) { snprintf(to, cap, "%s", from); }
#endif
#include "page_adapter.inc"

static bool same_rect(EpdRect a, EpdRect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int main(void) {
    const unsigned scales[] = {0, 80, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 240};
    const unsigned intervals[] = {0, 5, 10, 15, 30, 77, UINT_MAX};
#ifdef LEGACY
    const int setting_count=7;
    const unsigned flag_count=32;
#else
    const int setting_count=9;
    const unsigned flag_count=128;
#endif
    for (unsigned si = 0; si < sizeof(scales) / sizeof(*scales); ++si) {
        scale = scales[si];
        int top = reading_settings_top();
        record("top %u %d\n", scale, top);
        for (int i = -1; i <= setting_count; ++i) {
            EpdRect r = reading_setting_rect(i);
            record("cell %d %d %d %d %d\n", i, r.x, r.y, r.width, r.height);
#ifndef LEGACY
            assert(same_rect(r, ui_reader_settings_rect(scale, i)));
#endif
            if (i < 0 || i >= setting_count) assert(!r.width && !r.height);
            else assert(r.y >= top + 100 && r.y + r.height < 1141);
        }
        for (unsigned pi = 0; pi < sizeof(intervals) / sizeof(*intervals); ++pi) {
            pages = intervals[pi];
            for (flags = 0; flags < flag_count; ++flags) {
                record("case %u %u %u\n", scale, pages, flags);
                draw_reading_settings(framebuffer);
                draw_turn_settings(framebuffer);
            }
        }
    }
    // 其他面板的开关仍经页面包装转接。/ Other sheets still reach switch painting through the page wrapper.
    draw_reading_switch(framebuffer, (EpdRect){556, 660, 69, 39}, false);
    draw_reading_switch(framebuffer, (EpdRect){556, 660, 69, 39}, true);
#ifndef LEGACY
    const ui_reader_settings_values_t values = {150, 30, true, false, true, false, true, false};
    const ui_reader_settings_values_t before = values;
    unsigned reads_before = setting_reads;
    recording = false;
    ui_reader_settings_draw(framebuffer, &values);
    ui_reader_turn_settings_draw(framebuffer, true, false);
    assert(setting_reads == reads_before && !memcmp(&values, &before, sizeof(values)));
    // 快照直接调用的额外绘制不属于旧实现对照。/ Direct snapshot calls above are checked outside the legacy comparison.
#else
    (void)same_rect;
#endif
    assert(draws > 200000);
    return 0;
}
