/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：UI任务独占的阅读手账服务，内置卷双快照与共享插画状态。
 * English: UI-task-owned reading journal with two internal-volume snapshots and shared scenes.
 */
#pragma once
#include "reading_journal_model.h"

/// 事件或轮询调用；日期/状态变化返回true，不从render调用。/ Events/polls only; return date/state changes, never call from render.
bool app_reading_journal_poll(void);
/// 成功开书/翻页及有效正文计时入口，毫秒仅归属对应民用日期。/ Successful open/turn and active-body time; assign milliseconds to civil dates.
void app_reading_journal_record(const char *path, const char *title, uint32_t file_size,
                                unsigned chapter, uint32_t milliseconds, uint32_t turns);
/// 锁屏/离页立即尝试保存；失败保留内存，下次重试。/ Flush on lock/exit; retain memory on failure for retry.
bool app_reading_journal_flush(void);
/// 分类改名/移书时保持当月书籍身份；失败由卡上路径日志重试。/ Retain monthly book identity during category moves; retry failures through the card's path journal.
bool app_reading_journal_relocate(const char *old_path, const char *new_path, uint32_t file_size);
bool app_reading_journal_next_scene(void);
/// 只读快照；未校时显示无日期状态，不把旧记录当成今天。/ Read-only views; invalid time never relabels old records as today.
const reading_journal_model_t *app_reading_journal_model(void);
const reading_journal_date_t *app_reading_journal_today(void);
const char *app_reading_journal_notice(void);
