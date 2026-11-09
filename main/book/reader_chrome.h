/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：阅读页页眉与页脚的三段槽位绘制。合成槽位文本、确定性排布与截短都在这里；
 * 页眉页脚共用阅读字体面。页脚进度条、全屏进度线与分隔线
 * 不属于本文件。
 * English: three-slot painting for the reader header and footer. Slot text composition,
 * deterministic layout and trimming live here; both bars share the reading face.
 * The footer progress bar, full-screen strip and rules stay outside.
 *
 * 冻结：槽位只读设置与传入数据，不写设置、不读卡、不推屏、不改全局字体状态
 * （页脚系统槽选择在绘制内自恢复）。
 * Frozen: slots only read settings and the passed data — never write settings, touch the card,
 * present, or leave global font state changed (the footer's system-slot select restores itself).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

typedef struct {
    const char *title;   ///< 书名样本（空时调用方已回退章节名）/ Title sample (caller already fell back)
    const char *chapter; ///< 章节名，含「未保存 ·」前缀 / Chapter name incl. the unsaved prefix
    unsigned page;       ///< 本章 0 基页码 / Zero-based chapter page
    unsigned page_count; ///< 本章总页数 / Chapter page count
    unsigned percent;    ///< 全书字节百分比 / Whole-book byte percentage
    const char *clock;   ///< "HH:MM"，未对时为 "--:--" / "HH:MM", "--:--" when unsynced
    int battery;         ///< 电量百分比，负值未知 / Battery percent, negative when unknown
    bool charging;       ///< 充电中 / Charging flag
    const char *date;    ///< "MM-DD 周X"，未对时为空 / "MM-DD 周X", empty when unsynced
} reader_chrome_data_t;

/// 槽位内容项的选择器文案。/ Picker label for a slot item.
const char *reader_chrome_item_label(app_reader_slot_item_t item);
/// 画页眉三槽；cy 为三格共用视觉中心线，行区固定 36..648。/ Paint the header slots; cy is the shared centerline across 36..648.
void reader_chrome_draw_header(uint8_t *fb, const reader_chrome_data_t *data, int cy);
/// 画页脚文字行三槽；top 为行上沿，字号与左右边界由调用方给出（与进度条矩形同源）。
/// Paint the footer text row; the caller supplies the row top, type size and edges (from the bar rect).
void reader_chrome_draw_footer_row(uint8_t *fb, const reader_chrome_data_t *data,
                                   int top, int px, int left_x, int right_x);
/// 页眉三格合成文本的 FNV-1a 指纹（含电量图标状态），供翻页时判断顶部差分。
/// FNV-1a over the composed header texts (battery-icon state included), gating top-band diffs on turns.
uint32_t reader_chrome_header_fingerprint(const reader_chrome_data_t *data);
