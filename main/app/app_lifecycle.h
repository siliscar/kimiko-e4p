/*
 * 中文：单一UI调度任务的锁屏检查点与睡眠前置交还；不绘制或操作外设电源。
 * English: Lock checkpoints and pre-sleep handoff on the single UI scheduling task; no painting or peripheral power operations.
 */
#pragma once
#include "app.h"

/// 页面先保存并收齐阅读借用，再按原格式保存主页面或阅读恢复点；保留原写入失败处理。
/// Let the page save and join reader borrows before saving the original tab/reader checkpoint; retain existing write-failure handling.
void app_lifecycle_checkpoint(app_ctx_t *ctx, const app_desc_t *current);
/// 先取消后台对时，再暂停传输；失败由调用方保留原页及处理恢复点。
/// Cancel background time sync before pausing transfers; callers retain the page and handle checkpoints on failure.
bool app_lifecycle_pause_for_lock(void);
/// 先取消对时，再依次检查升级、封面/头像和传输；失败立即停止，不动外设或阅读内存。
/// Cancel time sync, then gate OTA, cover/avatar jobs and transfers in order; stop on failure without touching peripherals or reader memory.
/// 全部通过后沿用关机入口的尽力停止蓝牙，结果不改变原下电流程。
/// On admission retain the shutdown entry's best-effort Bluetooth stop, whose result does not alter the existing power-down flow.
bool app_lifecycle_prepare_poweroff(void);

typedef struct {
    bool jobs_ready; ///< 文件任务交还失败时调用方立即返回 / Caller returns immediately on file-job handoff failure
    bool ble_stopped; ///< 为false时调用方继续唤醒轮询，不进入浅睡 / False keeps caller polling awake instead of entering light sleep
} app_lifecycle_light_t;

/// 每项保持原2000ms预算；区分文件交还失败与蓝牙失败后的唤醒轮询。
/// Retain each original 2000ms budget; distinguish file handoff failure from awake polling after Bluetooth failure.
app_lifecycle_light_t app_lifecycle_prepare_light(void);
