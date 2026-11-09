/* 中文：单调时钟测试替身。/ English: Monotonic clock test shim. */
#pragma once
#include <stdint.h>

int64_t esp_timer_get_time(void);
