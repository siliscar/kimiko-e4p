/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：横排章节题头的样式与长标题折行；所有文字和几何由调用方传入，不持有页面状态。
 * English: Horizontal chapter styles and long-title wrapping; callers supply text and geometry, with no page state retained.
 */
#include "ui_reader_heading.h"
#include "ui_kit.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

EpdRect reader_heading_band(int top) {
    return (EpdRect){36, top, UI_LOCK_WIDTH - 72, READER_HEADING_BAND_HEIGHT};
}

// 长度类几何（高度、宽度、内边距）按字号缩放。
// Lengths -- heights, widths, padding -- scale with the size.
static int heading_len(int length, int scale_px) {
    return (length * scale_px + APP_BOOK_TITLE_SIZE_DEFAULT / 2) / APP_BOOK_TITLE_SIZE_DEFAULT;
}

// 块内坐标换成带内坐标：块高随档缩放，整块在带里垂直居中，所以放大是绕着带心放大的。
// offset 是基准档写在块内的坐标（4..180 是基准块）。
// A block coordinate becomes a band coordinate: the block scales with the size and stays centred in
// the band, so a bigger level grows around the band's centre. `offset` is the reference-level
// coordinate written inside the block (4..180 is the reference block).
static int heading_line(EpdRect band, int offset, int scale_px) {
    return band.y + (band.height - heading_len(READER_HEADING_BLOCK_HEIGHT, scale_px)) / 2 +
           heading_len(offset - READER_HEADING_BLOCK_TOP, scale_px);
}

// 题头文字按目标字号截短到给定宽度，返回实际字号；样本用原串，截短只影响画出来的那份。
// supplement_px 是补充位图字面的基准（档位/2）：含补充字面的行整行按它取整，免得编号比章名大。
// Fit a heading line into the given width, returning the size used; the sample stays the original
// string so only the painted copy is shortened. supplement_px is the bitmap supplement's base size
// (level/2): a line carrying such a glyph takes it, so the label can never overtake the title.
static int reader_heading_fit(char *out, size_t cap, const char *sample, int px, int width,
                              int supplement_px) {
    copy_text(out, cap, sample ? sample : "");
    return ui_text_title_fit_sized(out, px, width, sample ? sample : "", supplement_px);
}

// 静页余白：编号在上、章名在下、细线收尾，横排题头的历史画法。
// Plain: label above, title below, a hairline to close the block, the historical horizontal heading.
static void draw_heading_plain(uint8_t *fb, EpdRect band, int text_x, int title_px, int label_px,
                               int scale_px, const char *title, const char *title_sample,
                               const char *label, const char *label_sample) {
    ui_text_title_vc(fb, text_x, heading_line(band, 29, scale_px), label_px, label, label_sample,
                     EPD_DRAW_ALIGN_CENTER);
    ui_text_title_bold_vc(fb, text_x, heading_line(band, 112, scale_px), title_px, title, title_sample,
                          EPD_DRAW_ALIGN_CENTER);
    const int line_w = heading_len(264, scale_px);
    ui_hairline(fb, heading_line(band, 174, scale_px), band.x + (band.width - line_w) / 2, line_w,
                UI_GRAY_LIGHT);
}

// 中央章徽：编号落在实心胶囊里居中悬于章名上方；没有编号时只剩居中的章名。
// Emblem: the label sits in a solid pill above the centred title, and a book without a label simply
// keeps the centred title.
static void draw_heading_emblem(uint8_t *fb, EpdRect band, int text_x, int title_px, int scale_px,
                                const char *title, const char *title_sample,
                                const char *label, const char *label_sample) {
    const bool has_label = label && label[0];
    // 胶囊要按字宽包住编号，所以这里重新量一次：宽度和字号出自同一次测量，底和字不会错开。
    // The pill has to wrap the label by its width, so measure here: width and size come from one
    // measurement, and the pill cannot drift from the text inside it.
    if (has_label) {
        int pill_px = 0;
        const int text_w = ui_text_title_measure_sized(scale_px / 2, label, label_sample,
                                                       scale_px / 2, &pill_px);
        const int pill_h = heading_len(54, scale_px);
        const int pill_w = text_w + heading_len(44, scale_px);
        const int pill_cy = heading_line(band, 52, scale_px);
        ui_fill_round_rect(fb, (EpdRect){text_x - pill_w / 2, pill_cy - pill_h / 2, pill_w, pill_h},
                           pill_h / 2, UI_GRAY_BLACK);
        ui_text_title_ink_vc(fb, text_x, pill_cy, pill_px, label, label_sample,
                             EPD_DRAW_ALIGN_CENTER, UI_INK_WHITE, UI_INK_BLACK);
    }
    ui_text_title_bold_vc(fb, text_x, heading_line(band, has_label ? 140 : 107, scale_px), title_px,
                          title, title_sample, EPD_DRAW_ALIGN_CENTER);
}

// 边框索引：细框加左侧粗边，编号与章名都在框内左对齐。
// Frame: a hairline box with a heavy left edge, label and title both left aligned inside it.
static void draw_heading_frame(uint8_t *fb, EpdRect band, int text_x, int title_px, int label_px,
                               int scale_px, const char *title, const char *title_sample,
                               const char *label, const char *label_sample) {
    const bool has_label = label && label[0];
    EpdRect box = {band.x + 24, heading_line(band, 10, scale_px), band.width - 48,
                   heading_len(162, scale_px)};
    ui_fill_round_rect(fb, box, 14, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, box, 14, UI_GRAY_BLACK);
    ui_draw_control_frame(fb, (EpdRect){box.x + 1, box.y + 1, box.width - 2, box.height - 2}, 13, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){box.x + 2, box.y + 2, 14, box.height - 4}, 7, UI_GRAY_BLACK);
    if (has_label)
        ui_text_title_vc(fb, text_x, heading_line(band, 52, scale_px), label_px, label, label_sample,
                         EPD_DRAW_ALIGN_LEFT);
    ui_text_title_bold_vc(fb, text_x,
                          has_label ? heading_line(band, 140, scale_px) : box.y + box.height / 2,
                          title_px, title, title_sample, EPD_DRAW_ALIGN_LEFT);
}

// 左锋题签：左侧一道竖线，编号与章名左对齐紧随其后。
// 竖线是直角矩形，且比文字块更长，下端垂到章名下面；下沿与边框索引的框底同在 y+172。
// Mark: a square-ended vertical bar on the left with the label and title left aligned beside it.
// The bar is longer than the text block and hangs below the title; its foot matches the frame
// style's box bottom at y+172.
static void draw_heading_mark(uint8_t *fb, EpdRect band, int text_x, int title_px, int label_px,
                              int scale_px, const char *title, const char *title_sample,
                              const char *label, const char *label_sample) {
    const bool has_label = label && label[0];
    const int bar_h = heading_len(has_label ? 146 : 96, scale_px);
    const int bar_y = heading_line(band, has_label ? 26 : 59, scale_px);
    ui_fill_rect_fast((EpdRect){band.x + 16, bar_y, 8, bar_h}, UI_GRAY_BLACK, fb);
    if (has_label)
        ui_text_title_vc(fb, text_x, heading_line(band, 50, scale_px), label_px, label, label_sample,
                         EPD_DRAW_ALIGN_LEFT);
    ui_text_title_bold_vc(fb, text_x, heading_line(band, has_label ? 136 : 107, scale_px), title_px,
                          title, title_sample, EPD_DRAW_ALIGN_LEFT);
}

// 题头带里画一次：左右对齐的样式用 text_x 当左边缘，居中样式当中心；字号与宽度在这里一次算准，
// 绘制与截短不会各算一套。scale_px 是当前字号档（基准 48），几何按它缩放；编号字号与补充字面
// 基准都取它的一半。编号或章名缺失时各样式自己收窄。
// Paint one heading inside the band: text_x is the left edge for the aligned styles and the centre
// for the centred ones. Size and width are decided here once so fitting and painting cannot drift;
// scale_px is the current size level (reference 48) and the geometry follows it. Each style narrows
// itself when the label or the title is missing.
static void draw_heading_single(uint8_t *fb, EpdRect band, int style, int text_x, int title_px,
                                int label_px, int scale_px, const char *title,
                                const char *title_sample, const char *label,
                                const char *label_sample) {
    switch (style) {
        case APP_BOOK_TITLE_STYLE_EMBLEM:
            draw_heading_emblem(fb, band, text_x, title_px, scale_px, title, title_sample, label, label_sample);
            break;
        case APP_BOOK_TITLE_STYLE_FRAME:
            draw_heading_frame(fb, band, text_x, title_px, label_px, scale_px, title, title_sample, label,
                               label_sample);
            break;
        case APP_BOOK_TITLE_STYLE_MARK:
            draw_heading_mark(fb, band, text_x, title_px, label_px, scale_px, title, title_sample, label,
                              label_sample);
            break;
        default:
            draw_heading_plain(fb, band, text_x, title_px, label_px, scale_px, title, title_sample, label,
                               label_sample);
            break;
    }
}

/* ---- 长章名折行 / Wrapped long titles ---- */
// 一行放不下时折成两行：编号留在块顶，两行章名在其余空间里居中。两行仍放不下就逐档缩字号，
// 最低到 28px；更长的章名才截断第二行。带高与块高都不变，所以折行不触发正文重排。
// A title that cannot fit one line breaks into two: the label stays at the block's top and the two
// lines fill the rest. If two lines still do not fit, the size steps down to 28 px; only longer
// titles truncate the second line. The band and block heights are unchanged, so wrapping never
// re-typesets the chapter.
#define READER_HEADING_WRAP_MIN_PX 28
#define READER_HEADING_WRAP_GAP 6

// 量一个字节前缀的宽度：折行逐字前进，前缀先落进临时串。
// Width of a byte prefix: wrapping walks character by character, so the prefix is copied first.
static int heading_prefix_width(const char *text, size_t bytes, int px, const char *sample) {
    char scratch[READER_HEADING_TITLE_BYTES];
    if (bytes >= sizeof(scratch)) bytes = sizeof(scratch) - 1;
    memcpy(scratch, text, bytes);
    scratch[bytes] = 0;
    return ui_text_title_width_px(px, scratch, sample);
}

// 第一行结束的字节位置：0 表示整串一行放得下，-1 表示一个字都放不下。
// 英文优先在最后一个空格处断开，第一行至少占已容纳宽度的三分之一。
// End byte of the first line: zero when the whole string fits one line, -1 when not even one
// character does. Latin text prefers the last space while the first line keeps at least a third of
// what fit, so a single word is not stranded.
static int heading_wrap_break(const char *text, int px, int width, const char *sample) {
    size_t best = 0;
    for (size_t i = 0; text[i];) {
        size_t next = i + 1;
        while (text[next] && ((unsigned char)text[next] & 0xc0) == 0x80) ++next;
        if (heading_prefix_width(text, next, px, sample) > width) break;
        best = next;
        i = next;
    }
    if (!best) return -1;
    if (!text[best]) return 0;
    size_t space = 0;
    for (size_t i = 1; i < best; ++i)
        if (text[i] == ' ' && i >= best / 3) space = i;
    return (int)(space ? space : best);
}

// 已确定字号的截短：折行缩到下限后的兜底，规则与 ui_text_title_fit_sized 相同但不做字号换算。
// Truncation at a resolved size: the last resort after wrapping bottoms out, using the same rule as
// ui_text_title_fit_sized without converting the size again.
static void heading_truncate_px(char *text, int px, int width, const char *sample) {
    while (*text && ui_text_title_width_px(px, text, sample) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

// 折行版：编号在块顶，两行章名在其余空间居中；缩到下限仍放不下时截断第二行。
// 缩字号过程中整串重新变成一行放得下时，回到单行画法，不再强行折行。
// Wrapped layout: the label at the block's top, two title lines centred in the rest; anything that
// still does not fit at the floor truncates the second line. If a reduced size makes the whole
// title fit on one line again, it returns to the single-line layout instead of forcing a break.
static void draw_heading_wrapped(uint8_t *fb, EpdRect band, int style, int text_x, int text_w,
                                 int scale_px, int start_px, int label_px,
                                 const char *title_sample, const char *label,
                                 const char *label_sample) {
    const int width = text_w - READER_HEADING_BOLD_PX;
    const bool has_label = label && label[0];
    const int block_top = band.y +
        (band.height - heading_len(READER_HEADING_BLOCK_HEIGHT, scale_px)) / 2;
    const int area_top = block_top + heading_len(has_label ? 52 : 20, scale_px);
    const int area_bottom = block_top + heading_len(168, scale_px);
    const int area_h = area_bottom - area_top;
    const int gap = heading_len(READER_HEADING_WRAP_GAP, scale_px) < 2
                        ? 2 : heading_len(READER_HEADING_WRAP_GAP, scale_px);
    char first[READER_HEADING_TITLE_BYTES], second[READER_HEADING_TITLE_BYTES];
    int px = start_px;
    for (;;) {
        int split = heading_wrap_break(title_sample, px, width, title_sample);
        if (split == 0) {
            int fit_label = px / 2 < 16 ? 16 : px / 2;
            if (fit_label > label_px) fit_label = label_px;
            draw_heading_single(fb, band, style, text_x, px, fit_label, scale_px,
                                title_sample, title_sample, label, label_sample);
            return;
        }
        if (split > 0) {
            copy_text(first, sizeof(first), title_sample);
            first[split] = 0;
            const char *rest = title_sample + split;
            while (*rest == ' ') ++rest;
            copy_text(second, sizeof(second), rest);
            int line_h = ui_text_title_line_height_px(px, first, title_sample);
            int line_h2 = second[0] ? ui_text_title_line_height_px(px, second, title_sample) : 0;
            if (line_h2 > line_h) line_h = line_h2;
            int widest = ui_text_title_width_px(px, first, title_sample);
            int second_w = ui_text_title_width_px(px, second, title_sample);
            if (second_w > widest) widest = second_w;
            if (widest <= width && line_h * 2 + gap <= area_h) break;
            if (px > READER_HEADING_WRAP_MIN_PX) {
                // 两行总宽才是折行能否成立的判据：先按它一次缩到位，再逐 2px 收敛。
                // 只有整串总宽已经够两行、却有单词仍超宽时才按最长一行缩。
                // The two-line total is what decides whether wrapping can work: jump to the size it
                // implies, then converge by 2 px. Only when the total already fits two lines but a
                // single unbreakable word does not does the widest line drive the step.
                int next = px - 2;
                int total_w = ui_text_title_width_px(px, title_sample, title_sample);
                if (total_w > width * 2) {
                    int scaled = px * (width * 2) / total_w;
                    if (scaled < next) next = scaled;
                } else if (widest > width) {
                    int scaled = px * width / widest;
                    if (scaled < next) next = scaled;
                }
                if (next < READER_HEADING_WRAP_MIN_PX) next = READER_HEADING_WRAP_MIN_PX;
                if (next >= px) next = px - 2;
                px = next;
                continue;
            }
        } else if (px > READER_HEADING_WRAP_MIN_PX) {
            // 一个字都放不下：同样逐档缩，直到第一行至少能落下一个字。
            // Not even one character fits: step down until the first line can hold one.
            px -= 2;
            continue;
        }
        // 缩到下限：第一行与第二行都按宽度截断，保证不越出题头带。
        // At the floor: truncate both lines to the width so nothing can leave the band.
        if (split > 0) {
            copy_text(first, sizeof(first), title_sample);
            first[split] = 0;
            const char *rest = title_sample + split;
            while (*rest == ' ') ++rest;
            copy_text(second, sizeof(second), rest);
        } else {
            copy_text(first, sizeof(first), title_sample);
            second[0] = 0;
        }
        heading_truncate_px(first, px, width, title_sample);
        heading_truncate_px(second, px, width, title_sample);
        break;
    }
    const int lines = second[0] ? 2 : 1;
    int line_h = ui_text_title_line_height_px(px, first, title_sample);
    int line_h2 = lines == 2 ? ui_text_title_line_height_px(px, second, title_sample) : 0;
    if (line_h2 > line_h) line_h = line_h2;
    const int pitch = line_h + gap;
    const int center = area_top + area_h / 2;
    const int y1 = lines == 2 ? center - pitch / 2 : center;
    const int y2 = center + pitch / 2;
    int wrapped_label_px = px / 2 < 16 ? 16 : px / 2;
    if (wrapped_label_px > label_px) wrapped_label_px = label_px;
    const int label_cy = block_top + heading_len(26, scale_px);
    const int cx = band.x + band.width / 2;
    switch (style) {
        case APP_BOOK_TITLE_STYLE_EMBLEM:
            if (has_label) {
                const int pill_h = heading_len(40, scale_px);
                const int pill_w = ui_text_title_width_px(wrapped_label_px, label, label_sample) +
                                   heading_len(36, scale_px);
                ui_fill_round_rect(fb, (EpdRect){cx - pill_w / 2, label_cy - pill_h / 2, pill_w, pill_h},
                                   pill_h / 2, UI_GRAY_BLACK);
                ui_text_title_ink_vc(fb, cx, label_cy, wrapped_label_px, label, label_sample,
                                     EPD_DRAW_ALIGN_CENTER, UI_INK_WHITE, UI_INK_BLACK);
            }
            ui_text_title_bold_vc(fb, cx, y1, px, first, title_sample, EPD_DRAW_ALIGN_CENTER);
            if (lines == 2)
                ui_text_title_bold_vc(fb, cx, y2, px, second, title_sample, EPD_DRAW_ALIGN_CENTER);
            break;
        case APP_BOOK_TITLE_STYLE_FRAME: {
            EpdRect box = {band.x + 24, block_top + heading_len(6, scale_px), band.width - 48,
                           heading_len(162, scale_px)};
            ui_fill_round_rect(fb, box, 14, UI_GRAY_WHITE);
            ui_draw_control_frame(fb, box, 14, UI_GRAY_BLACK);
            ui_draw_control_frame(fb, (EpdRect){box.x + 1, box.y + 1, box.width - 2, box.height - 2}, 13,
                                  UI_GRAY_BLACK);
            ui_fill_round_rect(fb, (EpdRect){box.x + 2, box.y + 2, 14, box.height - 4}, 7, UI_GRAY_BLACK);
            if (has_label)
                ui_text_title_vc(fb, text_x, label_cy, wrapped_label_px, label, label_sample, EPD_DRAW_ALIGN_LEFT);
            ui_text_title_bold_vc(fb, text_x, y1, px, first, title_sample, EPD_DRAW_ALIGN_LEFT);
            if (lines == 2)
                ui_text_title_bold_vc(fb, text_x, y2, px, second, title_sample, EPD_DRAW_ALIGN_LEFT);
            break;
        }
        case APP_BOOK_TITLE_STYLE_MARK: {
            const int bar_y = block_top + heading_len(has_label ? 22 : 20, scale_px);
            ui_fill_rect_fast((EpdRect){band.x + 16, bar_y, 8, heading_len(146, scale_px)},
                              UI_GRAY_BLACK, fb);
            if (has_label)
                ui_text_title_vc(fb, text_x, label_cy, wrapped_label_px, label, label_sample, EPD_DRAW_ALIGN_LEFT);
            ui_text_title_bold_vc(fb, text_x, y1, px, first, title_sample, EPD_DRAW_ALIGN_LEFT);
            if (lines == 2)
                ui_text_title_bold_vc(fb, text_x, y2, px, second, title_sample, EPD_DRAW_ALIGN_LEFT);
            break;
        }
        default:
            if (has_label)
                ui_text_title_vc(fb, cx, label_cy, wrapped_label_px, label, label_sample, EPD_DRAW_ALIGN_CENTER);
            ui_text_title_bold_vc(fb, cx, y1, px, first, title_sample, EPD_DRAW_ALIGN_CENTER);
            if (lines == 2)
                ui_text_title_bold_vc(fb, cx, y2, px, second, title_sample, EPD_DRAW_ALIGN_CENTER);
            ui_hairline(fb, block_top + heading_len(170, scale_px),
                        band.x + (band.width - heading_len(264, scale_px)) / 2,
                        heading_len(264, scale_px), UI_GRAY_LIGHT);
            break;
    }
}

void draw_reader_title_style(uint8_t *fb, EpdRect band, int style, int scale_px,
                                    const char *label_sample, const char *title_sample) {
    if (!fb || !title_sample || !title_sample[0] || band.width <= 0) return;
    if (style < 0 || style >= APP_BOOK_TITLE_STYLE_COUNT) style = APP_BOOK_TITLE_STYLE_PLAIN;
    // 越界档位回落到基准：几何按它换算，负值或零会让所有纵向坐标塌到带顶。
    // An out-of-range level falls back to the reference: the geometry is derived from it, and a
    // zero or negative level would collapse every vertical coordinate onto the band's top edge.
    if (scale_px < APP_BOOK_TITLE_SIZE_MIN || scale_px > APP_BOOK_TITLE_SIZE_MAX)
        scale_px = APP_BOOK_TITLE_SIZE_DEFAULT;
    int text_x = band.x + band.width / 2, text_w = ui_content_width();
    if (style == APP_BOOK_TITLE_STYLE_FRAME) {
        text_x = band.x + 58;
        text_w = band.width - 102;
    } else if (style == APP_BOOK_TITLE_STYLE_MARK) {
        text_x = band.x + 44;
        text_w = band.width - 68;
    }
    char title[READER_HEADING_TITLE_BYTES], label[READER_HEADING_LABEL_BYTES];
    int title_px = reader_heading_fit(title, sizeof(title), title_sample, scale_px,
                                      text_w - READER_HEADING_BOLD_PX, scale_px / 2);
    int label_px = reader_heading_fit(label, sizeof(label), label_sample, scale_px / 2, text_w,
                                      scale_px / 2);
    // 一行放不下才折行；补充位图字面把字号换算过时沿用单行截短，避免换算后的字号再被换算一次。
    // Wrap only when one line cannot hold the title; a bitmap-supplement line whose size was
    // already converted keeps the single-line truncation so it is never converted twice.
    if (title_px == scale_px && strlen(title) < strlen(title_sample)) {
        draw_heading_wrapped(fb, band, style, text_x, text_w, scale_px, title_px, label_px,
                             title_sample, label, label_sample);
        return;
    }
    draw_heading_single(fb, band, style, text_x, title_px, label_px, scale_px, title, title_sample,
                        label, label_sample);
}
