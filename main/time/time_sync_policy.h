/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：自动对时的纯判定逻辑（何时该联网校时），无硬件依赖，供 main/time_sync.c 与 host 测试共用。
 *
 * English: Pure decision logic for auto time sync (when a network sync is due). It has no hardware
 * dependency and is shared by main/time_sync.c and the host test.
 *
 * 冻结：只做判定，不碰时钟、网络、NVS；阈值改动必须同步 docs/ONBOARDING.md。
 * Frozen: decisions only. It never touches the clock, network or NVS, and any threshold change must
 * update docs/ONBOARDING.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// 距上次成功对时超过 6 小时才再联网；开机/唤醒后通常即命中。
/// Resync only when the last success is 6 hours old, which a wake-up after a sleep usually hits.
#define APP_TIME_SYNC_INTERVAL_SEC (6u * 3600u)
/// 失败冷却：同一次开机内不会连续联网重试。
/// Failure cooldown: no back-to-back network retries inside one power cycle.
#define APP_TIME_SYNC_RETRY_SEC (30u * 60u)
/// 单次开机最多尝试次数，避免长期连不上时反复开关无线。
/// Attempts per power cycle, so an unreachable network cannot keep toggling the radio.
#define APP_TIME_SYNC_ATTEMPTS_PER_BOOT 3u
/// 有效时间下界，与全仓一致：2024-01-01 UTC。/ Clock floor used across the tree: 2024-01-01 UTC.
#define APP_TIME_SYNC_CLOCK_FLOOR 1704067200u
/// 记录比当前时间超前超过一天视为时钟被调回，忽略旧记录重新对时。
/// Records more than a day ahead of the clock mean the clock moved back; drop them and sync again.
#define APP_TIME_SYNC_BACKWARD_TOLERANCE_SEC (24u * 3600u)

/// 时钟是否有效（不是"从没对过时"的 1970 附近）。/ Whether the wall clock is plausible.
static inline bool app_time_sync_clock_valid(uint32_t now) {
    return now >= APP_TIME_SYNC_CLOCK_FLOOR;
}

/// 记录是否与当前时钟同处一个时间线：超前不超过容差才算可信；超前太多说明时钟被调回。
/// Whether a record belongs to the current timeline: at most the tolerance ahead counts; more than
/// that means the clock was moved back.
static inline bool app_time_sync_record_usable(uint32_t now, uint32_t stamp) {
    return stamp == 0 || now + APP_TIME_SYNC_BACKWARD_TOLERANCE_SEC >= stamp;
}

/// 现在是否该联网校时。last_ok/last_try 为 0 表示从未成功 / 从未尝试；记录超前（时钟刚被调回）
/// 按"刚校过"处理，避免追赶式重复联网。
/// Whether a network sync is due now; zero last_ok/last_try mean never succeeded / never tried. A
/// record ahead of the clock is treated as fresh so a backwards clock cannot cause a retry storm.
static inline bool app_time_sync_due(uint32_t now, uint32_t last_ok, uint32_t last_try) {
    if (!app_time_sync_clock_valid(now)) return false;
    if (last_ok && app_time_sync_record_usable(now, last_ok) &&
        (now < last_ok || now - last_ok < APP_TIME_SYNC_INTERVAL_SEC)) return false;
    if (last_try && app_time_sync_record_usable(now, last_try) &&
        (now < last_try || now - last_try < APP_TIME_SYNC_RETRY_SEC)) return false;
    return true;
}
