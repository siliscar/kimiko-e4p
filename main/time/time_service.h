/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/// 空闲时启动有界后台对时；主循环独占 PMU 与 NVS 的交接。
/// Start bounded background sync while idle; the main loop alone hands time to PMU/NVS.
void app_time_service_poll(bool idle);
/// 取消在途后台校准（组件内 100 ms 分片生效）；没有在途任务时无副作用。
/// Cancel an in-flight background calibration (takes effect in the component's 100 ms slices); a
/// no-op when nothing is in flight.
void app_time_service_cancel(void);
/// 手动（设置页）或升级页对时成功后登记：持久化时间线并清开机重试预算。
/// Register a manual or updater sync success: persist the timeline and clear the per-boot budget.
void app_time_service_note_success(uint32_t utc_seconds);

/// 手动校准的网络与内部时钟交接结果。/ Network and internal-clock handoff result of a manual sync.
typedef struct {
    /// 完整交接的返回值。/ Result of the complete handoff.
    esp_err_t error;
    /// 网络成功但内部时钟可能失败。/ Network succeeded; the internal clock may still fail.
    bool network_time_received;
} app_time_sync_result_t;

/// 仅 UI 调度任务调用：组件内抢占后台对时，成功后写入并回读内部时钟，再登记成功。
/// UI scheduling task only: preempt background sync inside the component, write/read back the internal clock, then record success.
app_time_sync_result_t app_time_service_sync_manual(void);
