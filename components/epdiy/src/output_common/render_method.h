#pragma once

#include "sdkconfig.h"

// 本 fork 使用 LCD/CAM 外设输出路径：ESP32-S3（Xtensa）与 ESP32-S31（RISC-V）。
// This fork uses the LCD/CAM output path on ESP32-S3 (Xtensa) and ESP32-S31 (RISC-V).
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "this epdiy fork only supports ESP32-S3 / ESP32-S31 (LCD render path)"
#endif
#define RENDER_METHOD_LCD 1

#ifdef __clang__
#define IRAM_ATTR
// define this if we're using clangd to make it accept the GCC builtin
void __assert_func(const char* file, int line, const char* func, const char* failedexpr);
#endif
