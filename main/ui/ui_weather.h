/*
 * 中文：全宽天气组件绘制与命中区域，不启动网络或修改设置。
 * English: Full-width weather rendering and hit regions, without networking or settings changes.
 */
#pragma once
#include "epdiy.h"
#include "weather_service.h"
typedef enum { UI_WEATHER_NONE, UI_WEATHER_CITY, UI_WEATHER_TODAY, UI_WEATHER_TOMORROW, UI_WEATHER_REFRESH, UI_WEATHER_SETTINGS } ui_weather_action_t;
bool ui_weather_prepare(void);
void ui_weather_release(void);
/// 绘制屏内684×472组件区；不分配暂存，不改变天气状态。/ Paint an on-screen 684x472 region without scratch allocation or weather-state changes.
void ui_weather_draw(uint8_t *fb, EpdRect region, bool tomorrow, const app_weather_view_t *view, int64_t now);
/// 使用与绘制一致的组件区域，边界外不命中。/ Hit-test the same region as drawing, excluding its outer boundary.
ui_weather_action_t ui_weather_hit(EpdRect region, int x, int y);
