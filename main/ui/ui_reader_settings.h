/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从 app_book 提取的阅读设置绘制与共用几何；调用方持有设置和页面状态。
 * English: Reader-setting painting and shared geometry extracted from app_book; callers own settings and page state.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

typedef struct {
    unsigned system_font_size; ///< 界面字号百分比 / UI font percentage
    unsigned full_pages; ///< 自动全刷间隔，零为不强制 / Full-refresh interval, zero disables forcing
    bool turn_effect; ///< 水波纹效果 / Ripple effect
    bool vertical_turn; ///< 上下点击翻页 / Vertical tap zones
    bool statusbar; ///< 显示状态栏 / Visible status bar
    bool hide_images; ///< 跳过书内插图 / Skip inline images
    bool vertical_text; ///< 竖排正文 / Vertical body text
} ui_reader_settings_values_t;

/// 阅读设置顶部；与页面命中共用，字号限制为100至200。/ Shared sheet top for drawing and hit testing, clamped to 100–200 percent.
int ui_reader_settings_top(unsigned system_font_size);
/// 八个设置格；无效索引返回空矩形。/ Eight setting cells; invalid indices return an empty rectangle.
EpdRect ui_reader_settings_rect(unsigned system_font_size, int index);
/// 只画开关，不修改状态，供字体和按键面板复用。/ Paint a switch without state changes, shared by font and key sheets.
void ui_reader_settings_switch(uint8_t *fb, EpdRect track, bool on);
/// 绘制控件与完成键；调用方先画面板壳，不推屏、不读取或修改设置。/ Paint controls and Done after the caller paints the shell; no presentation or settings access.
void ui_reader_settings_draw(uint8_t *fb, const ui_reader_settings_values_t *values);
/// 绘制翻页示意、晃动开关与完成键；调用方先画面板壳和返回键。/ Paint turn diagrams, the shake toggle and Done after the caller paints the shell and Back.
void ui_reader_turn_settings_draw(uint8_t *fb, bool vertical, bool shake);
