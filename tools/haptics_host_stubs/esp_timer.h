/* SPDX-License-Identifier: Apache-2.0 */
/* 宿主替身：单次定时器的创建、启动与停止。/ Host shim for one-shot timer create/start/stop. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct esp_timer* esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void*);
typedef enum { ESP_TIMER_TASK, ESP_TIMER_ISR } esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
