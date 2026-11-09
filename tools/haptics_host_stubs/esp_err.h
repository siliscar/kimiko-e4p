/* SPDX-License-Identifier: Apache-2.0 */
/* 宿主替身：haptics.c 只需要错误码。/ Host shim: haptics.c only needs error codes. */
#pragma once

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
