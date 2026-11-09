/*
 * 中文：打开组件库或选城的跨页面意图，页面之间不直接引用。
 * English: Cross-page intent to open the component library or city picker, without page dependencies.
 */
#pragma once
#include "app.h"
void app_components_open(app_ctx_t *ctx, bool city_picker);
/// 从指定首页区域打开独立组件选择。/ Open independent component selection from a Home region.
void app_components_open_home(app_ctx_t *ctx, unsigned slot);
