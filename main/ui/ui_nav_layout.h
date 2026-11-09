/* SPDX-License-Identifier: Apache-2.0
 * 中文：轻量底栏边界，绘制和刷新共用，防止局部扫描碰到固定图标。
 * English: Lightweight navigation bounds shared by painting and presentation to protect fixed icons.
 */
#pragma once
#define UI_NAV_TOP 1112
#define UI_NAV_DIVIDER_H 2
// 只扩大触控热区，不移动图标或改变内容/底栏的刷新边界。
// Expand touch admission only; icons and content/navigation refresh boundaries stay fixed.
#define UI_NAV_TOUCH_TOP (UI_NAV_TOP - 16)
#define UI_NAV_MARKER_TOP (UI_NAV_TOP + 3)
#define UI_NAV_MARKER_BOTTOM (UI_NAV_TOP + 8)
// 内容截止与实际导航顶部一致，固定区域不再占用上方24px。
// Content ends at the actual navigation top, without an extra 24px fixed region.
#define UI_NAV_REFRESH_END UI_NAV_TOP
// 仅全像素清理向内对齐，防止扩列驱动导航；普通内容差分不受此限制。
// Only full-pixel cleanup aligns inward to avoid driving navigation; ordinary content differences use the actual boundary.
#define UI_NAV_CLEAN_END (UI_NAV_TOP & ~31)
#define UI_NAV_MARKER_SCAN_END ((UI_NAV_MARKER_BOTTOM + 31) & ~31)
