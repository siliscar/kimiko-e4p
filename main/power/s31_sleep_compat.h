/*
 * 中文：S31 浅睡时钟兼容入口；板级外设和缓存恢复仍由 sleep.c 管理。
 * English: S31 light-sleep clock compatibility; sleep.c still owns peripherals and cache restore.
 */
#pragma once

#include "esp_err.h"

/// 初始化原厂 MPLL 校准前置条件。/ Initialize the vendor MPLL calibration prerequisites.
void app_s31_sleep_init(void);

/// 显式浅睡并限定唤醒校准补丁的作用域。/ Explicitly light-sleep within the wake-calibration workaround scope.
esp_err_t app_s31_light_sleep_start(void);
