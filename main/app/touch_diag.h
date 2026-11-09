/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：触摸丢点诊断（CONFIG_TOUCH_DIAG）。仅在诊断构建启用：事件单行日志 +
 * 可选的丢弃蜂鸣。全部日志走 "touchdiag" 标签，便于串口过滤。
 * English: Touch tap-drop diagnostics (CONFIG_TOUCH_DIAG). Diagnostic builds only:
 * one-line event logs plus an optional discarded-input beep, all under "touchdiag".
 *
 * 事件清单 / Event vocabulary:
 *   STALL gap=      相邻两次读触摸的间隔超过 50ms（刷新等阻塞窗口）
 *   READ_ERR err=   cst836u_read 失败（finger=当时手指是否按下）
 *   RAW_INVALID     触摸计数字节连续无效（cst836u.c 内部重试后仍失败）
 *   MULTI n=        单指场景出现多点
 *   PRESS x= y=     触摸按下边沿
 *   GEND t=         手势终态（TAP/SWIPE/LONG/CANCEL 及位移、时长）
 *   KILL_BY_ERR     手势跟踪期间 I2C 出错被强制取消（整次点击报废的特征）
 *   TAPZ out=       阅读页点击分区结果（上一页/下一页/无）
 *   TURN dir=       turn_page 入口
 *   PRESENT ms=     一次 app_present 刷屏耗时 >100ms
 *   STATS ...       每 60s 汇总计数
 */
#pragma once

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_TOUCH_DIAG
#define TD_LOGI(fmt, ...) ESP_LOGI("touchdiag", fmt, ##__VA_ARGS__)
/// 被丢弃的输入事件短鸣一声（TOUCH_DIAG_BEEP 时才发声）；不阻塞调用方。
/// Beep once for a discarded input event (sounds only with TOUCH_DIAG_BEEP); never blocks.
void touch_diag_beep(void);
/// 计时包裹主循环里的周期性嫌疑任务，>50ms 的记 BLOCK src=，定位吞点击的阻塞源。
/// Time periodic main-loop suspects; log BLOCK src= over 50ms to locate stall sources.
void touch_diag_block(const char* src, int64_t t0_us);
#define TD_TIME(src, ...) do { \
    int64_t td_t0_ = esp_timer_get_time(); \
    __VA_ARGS__; \
    touch_diag_block(src, td_t0_); \
} while (0)
#else
#define TD_LOGI(fmt, ...) do { } while (0)
static inline void touch_diag_beep(void) { }
#define TD_TIME(src, ...) do { __VA_ARGS__; } while (0)
#endif
