#pragma once

#include "epd_highlevel.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// 跑一遍 EPD bring-up 测试图案并等待 10 秒，供 CONFIG_MOGO_EPD_TEST_PATTERN 使用。
/// Run the EPD bring-up pattern and wait 10 s; used by CONFIG_MOGO_EPD_TEST_PATTERN.
void epd_test_run(EpdiyHighlevelState* hl, uint8_t* fb);

#ifdef __cplusplus
}
#endif
