/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：浅睡唤醒后的 CACHE_L1 配置恢复（S31 专用；见 cache_wake_restore.c）。
 *
 * English: CACHE_L1 configuration restore after a light-sleep wake (S31 only;
 * see cache_wake_restore.c).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// 清醒态抓一次 CACHE_L1 稳定配置快照；重复调用无副作用。
/// Snapshot the stable CACHE_L1 words once while awake; calling again is a no-op.
void cache_wake_restore_snapshot(void);

/// 每次浅睡返回后调用：把快照写回 CACHE_L1。
/// Call after every light-sleep return: write the snapshot back to CACHE_L1.
void cache_wake_restore_apply(void);

/// 快照是否已抓取（S31 上且已调用过 snapshot）。
/// Whether a snapshot exists (S31 and snapshot() has run).
bool cache_wake_restore_active(void);

#ifdef __cplusplus
}
#endif
