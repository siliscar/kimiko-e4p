/*
 * 中文：顶部快捷层的同步控制接口；只由单 UI 调度任务调用，内部状态不向页面公开。
 * English: Synchronous top quick-layer control; called only by the UI task, with internal state hidden from pages.
 */
#pragma once
#include "app.h"

/// 启动时重置层并装入前光设置；不重新写前光硬件。
/// Reset the layer and load frontlight settings at startup without reapplying hardware.
void app_quick_layer_init(void);
/// 先持久化当前预览，再丢弃接触、待呈现行和层高度；不推屏。
/// Persist previews, then discard contact, pending rows and layer height without presenting.
void app_quick_layer_reset(void);
/// 当前是否展开；主循环据此冻结页内事件和轮询。
/// Whether open; the loop uses this to suspend page events and ticks.
bool app_quick_layer_is_open(void);
/// 识别并同步执行一帧，返回是否接管输入；coalesce 只合并前光呈现。
/// Recognize and synchronously execute a frame, returning input ownership; coalesce merges only frontlight presentation.
bool app_quick_layer_feed_frame(app_ctx_t *ctx, const app_desc_t *current,
                                bool *lock_requested, bool coalesce);
/// 原层开关、归一及强刷路径；失败时重置层并请求当前页全刷。
/// Existing layer open/close, normalization and full refresh; reset and fully present the page on failure.
void app_quick_layer_present(app_ctx_t *ctx, const app_desc_t *current, bool full);
/// 只同步圆键按压和必要状态，不重新合成底页。
/// Synchronize button presses and required status without recomposing the underlay.
void app_quick_layer_present_buttons(app_ctx_t *ctx, const app_desc_t *current);
/// 网络或蓝牙状态变化后按原时点更新圆键。
/// Update buttons at the original polling point when network or Bluetooth status changes.
void app_quick_layer_sync_status(app_ctx_t *ctx, const app_desc_t *current);
/// 回放排空后只呈现最后一条前光行，保留松手提交标记。
/// Present the final frontlight row after replay drains, retaining the release commit flag.
void app_quick_layer_flush_frontlight(app_ctx_t *ctx, const app_desc_t *current);
/// 切页请求产生后丢弃积压行；不撤销硬件跟随或已经持久化的设置。
/// Drop queued rows on a page request without undoing hardware updates or persisted settings.
void app_quick_layer_discard_frontlight(void);
/// 读点失败时取消接触并落盘预览，返回层是否仍展开。
/// Cancel contact and persist previews on a read error, returning whether the layer remains open.
bool app_quick_layer_cancel_input(void);
