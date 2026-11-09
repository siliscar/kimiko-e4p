/*
 * 中文：旧调度回归的静默天气替身；真实有界会话由weather_host_test独立覆盖。
 * English: Quiet weather stand-in for existing scheduler regressions; weather_host_test covers real bounded sessions.
 */
#pragma once
#include <stdbool.h>
static inline bool app_weather_busy(void) {return false;}
static inline void app_weather_cancel_join(void) {}
