/*
 * 中文：介质失效与字体重试的同步服务；仅由单一 UI 调度任务调用。
 * English: Synchronous media-loss and font-retry service, called only by the UI scheduling task.
 */
#pragma once
#include "app.h"

/// 启动时从挂载状态和现用字体识别已有文件消费者，不发起挂载。
/// Detect existing consumers from mount status and the active face at startup without mounting.
bool app_media_initial_mounted(void);
/// 重试已保存字体，allow_probe 由主循环根据失效锁存决定；不绘制或呈现。
/// Retry the saved face with probing permitted by the loop's invalidation latch; never paint or present.
bool app_media_retry_font(bool allow_probe);
/// mounted 与 invalidated 由主循环持有，服务只在本次同步调用内借用；重挂节流归本模块。
/// The loop owns mounted and invalidated, borrowed only during this call; this module owns remount throttling.
/// 保留缓存失效、停止封面工作、页面通知、字体回退的顺序；返回 true 请求主循环取消旧输入并重绘。
/// Retain cache invalidation, cover quiescence, page notification and font fallback order; true requests input cancellation and repainting by the loop.
bool app_media_poll(app_ctx_t *ctx, const app_desc_t *current, bool *mounted, bool *invalidated);
