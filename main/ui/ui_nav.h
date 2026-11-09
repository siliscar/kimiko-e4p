/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：四个产品主页面共用的底部导航；阅读页自行隐藏。
 * English: Shared four-tab product navigation; the reader hides it.
 */
#pragma once

#include "app.h"
#include "ui_nav_layout.h"

#define UI_NAV_BACK_BORDER_GRAY 0x58
/// 状态栏与阅读页眉共用的时间字号。/ Shared clock type size for the status bar and reader header.
#define UI_STATUS_TIME_PX 28

void ui_nav_draw(uint8_t *fb, int active);
void ui_nav_status(uint8_t *fb);
/// 电量或充电标志是否与上次绘制的状态栏不同；调用方负责可见性及推屏。
/// Whether percentage or charging differs from the last painted status bar; callers own visibility and presentation.
bool ui_nav_status_power_changed(void);
/// 系统字体就绪时缓存状态栏字宽，阅读页只复用，不重载整份系统字体。
/// Cache status metrics when the system face is ready; reading reuses them without reloading that font.
void ui_nav_status_font_context(bool reading, bool face_changed);
/// 当前 UTC 分钟，用于显示更新判断；不读取卡或请求网络校时。
/// Current UTC minute for display updates, without card IO or network time requests.
int64_t ui_nav_clock_minute(void);
/// 只绘制状态栏时间，供主页面按分钟局部更新。/ Draw only the status clock for local minute updates.
void ui_nav_clock_draw(uint8_t *fb);
/// 状态栏同款时间文本，供阅读页眉等自定义位置复用。/ Status-bar clock text for reuse at custom spots such as the reader header.
void ui_nav_clock_text(char *clock, size_t capacity);
/// 状态栏同时钟来源的日期文本「MM-DD 周X」；未对时返回空串，槽位自然不画。
/// "MM-DD 周X" from the same clock source as the status bar; empty when unsynced so custom slots skip it.
void ui_nav_date_text(char *date, size_t capacity);
/// 在 (x,cy) 画状态栏同款电池（含电量填充与充电闪电），x 为电池左沿。
/// Draw the status-bar battery (fill and charging bolt included) at (x,cy); x is the battery's left edge.
void ui_nav_battery_draw(uint8_t *fb, int x, int cy);
/// 绘制设置页与状态栏共用的 WiFi 标志。/ Draw the WiFi mark shared by Settings and the status bar.
void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray);
/// 二级页面共用的圆形返回按钮。/ Shared circular back control for secondary pages.
void ui_nav_back(uint8_t *fb, int x, int y);
/// 四栏整列热区向上扩16px，互不重叠；不改变画面和刷新区域。/ Four nonoverlapping full-column hits extend 16px upward, without changing paint or refresh bounds.
int ui_nav_hit(uint16_t x, uint16_t y);
void ui_nav_request(app_ctx_t *ctx, int index);
