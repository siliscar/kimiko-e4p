/* 中文：调度测试的前光替身。/ English: Frontlight shim for the scheduler test. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t frontlight_apply(bool on, uint8_t brightness, uint8_t cct);
bool frontlight_ready(void);
