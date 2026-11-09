/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：阅读手账纯绘制及几何命中。/ English: Pure journal drawing and geometric hit testing.
 */
#pragma once
#include "epdiy.h"
#include "reading_journal_model.h"

EpdRect ui_reading_journal_art(EpdRect region);
void ui_reading_journal_widget(uint8_t *fb, EpdRect region, const reading_journal_model_t *model,
                               const reading_journal_date_t *today, const char *notice);
void ui_reading_journal_detail(uint8_t *fb, const reading_journal_model_t *model,
                               const reading_journal_date_t *today, unsigned selected, unsigned page, const char *notice);
/// 仅返回本月非未来日期，日历以周一开头。/ Return only nonfuture dates in this month, Monday first.
unsigned ui_reading_journal_date_hit(const reading_journal_date_t *today, int x, int y);
