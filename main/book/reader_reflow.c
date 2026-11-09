/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从阅读页面提取设置和全屏重排事务；保留校验、回滚和持久化顺序。
 * English: Setting and full-screen reflow transactions extracted from the reader page, preserving validation, rollback and persistence order.
 */
#include "reader_reflow.h"
#include <string.h>
#include "book_layout.h"
#include "settings.h"

bool reader_reflow_layout(const reader_reflow_t *reflow, int next_px,
                          const int next_margins[READER_REFLOW_MARGINS],
                          uint8_t next_line, uint8_t next_paragraph) {
    if (next_px < BOOK_READER_PX_MIN || next_px > BOOK_READER_PX_MAX ||
        next_line < 110 || next_line > 150 ||
        app_settings_book_paragraph_value(app_settings_book_paragraph_level(next_paragraph)) != next_paragraph)
        return false;
    bool margins_changed = false;
    for (int i = 0; i < READER_REFLOW_MARGINS; ++i) {
        if (next_margins[i] < READER_REFLOW_MARGIN_MIN || next_margins[i] > READER_REFLOW_MARGIN_MAX)
            return false;
        if (next_margins[i] != reflow->margins[i]) margins_changed = true;
    }
    if (next_px == *reflow->px && !margins_changed && next_line == app_settings_book_line_spacing() &&
        next_paragraph == app_settings_book_paragraph_spacing()) return false;
    size_t offset = book_layout_page_start_offset(reflow->position->page);
    int old_px = *reflow->px, old_margins[READER_REFLOW_MARGINS];
    memcpy(old_margins, reflow->margins, sizeof(old_margins));
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_paragraph = app_settings_book_paragraph_spacing();
    reflow->begin();
    *reflow->px = next_px;
    memcpy(reflow->margins, next_margins, sizeof(old_margins));
    *reflow->line_spacing = next_line;
    book_layout_set_spacing(next_line, next_paragraph);
    // 正文带变动后，题头预留和悬挂标点余量跟随当前边距重算。
    // After the body band changes, recompute heading reservation and hanging slack from current margins.
    book_layout_set_right_slack(reflow->margins[READER_REFLOW_RIGHT]);
    reflow->refresh_lead();
    bool ok = reflow->build();
    if (ok) reflow->position->page = book_layout_page_for_offset(offset);
    else {
        *reflow->px = old_px;
        memcpy(reflow->margins, old_margins, sizeof(old_margins));
        *reflow->line_spacing = old_line;
        book_layout_set_spacing(old_line, old_paragraph);
        book_layout_set_right_slack(reflow->margins[READER_REFLOW_RIGHT]);
        reflow->refresh_lead();
        (void)reflow->build();
    }
    reflow->end();
    if (!ok) return false;
    app_settings_set_book_px((uint8_t)*reflow->px);
    app_settings_set_book_margin_left((uint8_t)reflow->margins[READER_REFLOW_LEFT]);
    app_settings_set_book_margin_right((uint8_t)reflow->margins[READER_REFLOW_RIGHT]);
    app_settings_set_book_margin_top((uint8_t)reflow->margins[READER_REFLOW_TOP]);
    app_settings_set_book_margin_bottom((uint8_t)reflow->margins[READER_REFLOW_BOTTOM]);
    app_settings_set_book_line_spacing(next_line);
    app_settings_set_book_paragraph_spacing(next_paragraph);
    reflow->save();
    return true;
}

bool reader_reflow_typography(const reader_reflow_t *reflow, int tracking_index) {
    if (tracking_index < 0 || tracking_index >= APP_BOOK_TRACK_COUNT) return false;
    int old_index = app_settings_book_tracking();
    if (tracking_index == old_index) return false;
    size_t offset = book_layout_page_start_offset(reflow->position->page);
    reflow->begin();
    book_layout_set_typography(tracking_index + APP_BOOK_TRACK_MIN);
    bool ok = reflow->build();
    if (ok) reflow->position->page = book_layout_page_for_offset(offset);
    else {
        book_layout_set_typography(old_index + APP_BOOK_TRACK_MIN);
        (void)reflow->build();
    }
    reflow->end();
    if (!ok) return false;
    app_settings_set_book_tracking((uint8_t)tracking_index);
    reflow->save();
    return true;
}

bool reader_reflow_indent(const reader_reflow_t *reflow, int em, int adjustment) {
    if (em < 0 || em > 3 || adjustment < -100 || adjustment > 100 ||
        (em == app_settings_book_indent() && adjustment == app_settings_book_indent_adjust()))
        return false;
    size_t offset = book_layout_page_start_offset(reflow->position->page);
    uint8_t old_em = app_settings_book_indent();
    int old_adjustment = app_settings_book_indent_adjust();
    reflow->begin();
    book_layout_set_first_line_indent((unsigned)em);
    book_layout_set_first_line_indent_adjust(adjustment);
    bool ok = reflow->build();
    if (ok) reflow->position->page = book_layout_page_for_offset(offset);
    else {
        book_layout_set_first_line_indent(old_em);
        book_layout_set_first_line_indent_adjust(old_adjustment);
        (void)reflow->build();
    }
    reflow->end();
    if (!ok) return false;
    app_settings_set_book_indent((uint8_t)em);
    app_settings_set_book_indent_adjust((int8_t)adjustment);
    reflow->save();
    return true;
}

static void reader_reflow_set_option(int kind, bool value) {
    if (kind == 0) app_settings_set_reader_statusbar(value);
    else if (kind == 1) {
        app_settings_set_reader_hide_images(value);
        book_layout_set_images_visible(!value);
    } else {
        app_settings_set_book_vertical(value);
        book_layout_set_vertical(value);
    }
}

bool reader_reflow_option_locked(const reader_reflow_t *reflow, int kind, bool previous, size_t offset) {
    reader_reflow_set_option(kind, !previous);
    // 模式改变正文区域或题头轴向，先重算题头再构建布局。
    // A mode changes the body area or heading axis, so refresh the heading before building.
    reflow->refresh_lead();
    bool ok = reflow->build();
    if (ok) reflow->position->page = book_layout_page_for_offset(offset);
    else {
        reader_reflow_set_option(kind, previous);
        reflow->refresh_lead();
        (void)reflow->build();
    }
    return ok;
}

reader_reflow_result_t reader_reflow_fullscreen(const reader_reflow_t *reflow, bool *fullscreen) {
    size_t offset = book_layout_page_start_offset(reflow->position->page);
    reflow->begin();
    *fullscreen = !*fullscreen;
    reflow->refresh_lead();
    bool ok = reflow->build();
    bool restored = true;
    if (ok) reflow->position->page = book_layout_page_for_offset(offset);
    else {
        *fullscreen = !*fullscreen;
        reflow->refresh_lead();
        restored = reflow->build();
    }
    reflow->end();
    return ok ? READER_REFLOW_APPLIED : restored ? READER_REFLOW_RESTORED : READER_REFLOW_LOST_LAYOUT;
}
