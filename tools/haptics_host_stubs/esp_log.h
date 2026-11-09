/* SPDX-License-Identifier: Apache-2.0 */
/* 宿主替身：日志不落盘。/ Host shim: logging is discarded. */
#pragma once

#define ESP_LOGI(tag, fmt, ...) ((void)(tag), (void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)(tag), (void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)(tag), (void)0)
