/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：主循环的同步触摸摄取与手势分发；不读取硬件、不持有采样队列或决定页面切换。
 * English: Synchronous loop touch ingestion and gesture dispatch; no hardware reads, sampler-queue ownership or navigation decisions.
 */
#pragma once
#include "app.h"
#include "touch_sampler.h"
#include "ui_gesture.h"
#include "sdkconfig.h"

/// 主循环提供原有呈现入口，仍在手势回调之后同步执行。
/// The loop provides its existing presenter, called synchronously after the gesture callback.
typedef void (*app_input_present_t)(app_ctx_t *ctx, const app_desc_t *app,
                                    ui_gesture_t *gesture, app_redraw_t redraw);

/// 重建采样帧的坐标、触点数和按下状态；不回放多点载荷或原始寄存器。
/// Rebuild frame coordinates, contact count and contact state without replaying multipoint payloads or raw registers.
void app_input_frame_to_touch(const touch_frame_t *frame, app_touch_t *touch);
/// 先判边沿、记诊断，再去抖并更新 ctx；was_touched 仍由调用方在原分发位置提交。
/// Detect edges and record diagnostics before debounce/context updates; caller still commits was_touched at its existing dispatch point.
/// frame_ms 使用原采样或直读时间，last_input_ms 只在成功读取的接触或释放帧更新。
/// frame_ms retains sampler/direct-read time; only successfully read contact/release frames update last_input_ms.
void app_input_ingest_touch_frame(app_ctx_t *ctx, app_touch_t *latest,
                                  const app_touch_t *touch, esp_err_t err,
                                  bool *was_touched, int64_t frame_ms,
                                  int64_t *last_input_ms, bool *pressed, bool *released);
/// 浮层、菜单或读错时不分发；回调、呈现、consumed 的先后顺序保持不变。
/// Do not dispatch while an overlay/menu owns input or reads fail; retain callback, presentation and consumed ordering.
void app_input_dispatch_gesture_frame(app_ctx_t *ctx, const app_desc_t *app,
                                      ui_gesture_t *gesture, esp_err_t err,
                                      bool owned_by_other, app_input_present_t present);
/// 取消页内装饰时保护已有导航请求；先重置识别器再用公共呈现入口刷新。
/// Protect pending navigation during decoration cancellation; reset recognition before using the public presenter.
void app_input_cancel_gesture(app_ctx_t *ctx, const app_desc_t *app, ui_gesture_t *gesture);

#if CONFIG_TOUCH_DIAG
typedef struct {
    unsigned press; ///< 按下边沿 / Press edges
    unsigned tap; ///< 点击 / Taps
    unsigned swipe; ///< 滑动 / Swipes
    unsigned longp; ///< 长按 / Long presses
    unsigned cancel; ///< 识别取消 / Recognition cancels
    unsigned multi; ///< 多点序列 / Multipoint sequences
} app_input_diag_counts_t;
/// 原统计日志按原时机读取，不清零累计计数。
/// Read cumulative counters at the existing statistics-log point without resetting them.
app_input_diag_counts_t app_input_diag_counts(void);
#endif
