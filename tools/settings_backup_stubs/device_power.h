#pragma once
#include <stdbool.h>

// 设置备份测试不连接 PMU，电量日志开关仅作为硬件边界替身。
// Settings backup tests do not connect to the PMU; the battery-log toggle is a hardware-boundary double.
static inline void device_power_set_batt_log(bool enabled) { (void)enabled; }
