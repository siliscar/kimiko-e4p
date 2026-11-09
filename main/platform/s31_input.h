/*
 * 中文：S31 触摸驱动到页面快照的适配入口；不持有设备、不去抖、不分发事件。
 * English: S31 driver-to-page snapshot adapter; no device ownership, debounce or event dispatch.
 */
#pragma once
#include "app/app_touch.h"
#include "esp_err.h"

/// device 借用当前板级触摸句柄；逐字段传递读写载荷和错误，不分配资源。
/// Borrow the board touch handle as device; transfer fields and errors without allocating resources.
esp_err_t s31_input_read_touch(void *device, app_touch_t *touch);
