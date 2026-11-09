/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：提取 USB、文件页与睡眠共用的封面和头像交还顺序；不改变等待预算或外围门控。
 * English: Extract the cover/avatar handoff shared by USB, files and sleep without changing budgets or caller guards.
 */
#include "storage_jobs.h"
#include "book_cover_jobs.h"
#include "ui_avatar.h"

bool storage_jobs_quiesce(unsigned timeout_ms) {
    return book_cover_jobs_quiesce(timeout_ms) && ui_avatar_quiesce(timeout_ms);
}
