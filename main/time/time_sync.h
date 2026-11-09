/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：系统级自动对时服务。开机/唤醒后距上次成功对时超过阈值时，短暂联网做一次 SNTP 并写回 PMU。
 *
 * English: System-level auto time sync. When the last successful sync is older than the interval,
 * one short network session seeds both the host clock and the PMU.
 *
 * 冻结：会话所有权归调用它的界面循环，锁屏/关机前必须释放；PMU 写入只在界面循环所在任务里做。
 * Frozen: the UI loop owns the radio session and must release it before lock or power-off; the PMU
 * write happens on that same task only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// 每轮界面循环调用（内部自行限速）。blocked 为真表示当前状态不允许占用无线（如传书页）。
/// Called from the UI loop; the service throttles itself. A true `blocked` forbids the radio here.
void app_time_sync_poll(int64_t now_ms, bool blocked);

/// 把无线交回：取消在途对时并释放自动对时自己启动的会话。锁屏/关机、传书页进页、手动对时与
/// 联网升级开始前调用；别人的会话不动。
/// Hand the radio back: cancel any in-flight sync and release the session auto-sync owns. Called
/// before lock/power-off, before the transfer page, and before a manual or updater sync; a session
/// owned by anyone else is left alone.
void app_time_sync_yield_radio(void);

/// 手动（设置页）或升级页对时成功后登记，避免自动对时立刻重复联网。
/// Register a manual or updated-page sync so auto-sync does not immediately go online again.
void app_time_sync_note_success(uint32_t utc_seconds);
