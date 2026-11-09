/* 中文：SD 交接测试的日志写入门控。/ English: Logger write gate for SD handoff tests. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool device_power_batt_log_storage(bool available, uint32_t timeout_ms);
