/* SPDX-License-Identifier: Apache-2.0
 * SD 探测主机测试卡座检测接口。/ Host SD-probe presence stub.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
esp_err_t board_sd_detect(bool* present);
bool board_sd_present(void);
bool board_sd_has_cd(void);
