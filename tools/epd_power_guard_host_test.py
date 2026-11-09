#!/usr/bin/env python3
"""实际关电任务的并发、期限和失败回归。/ Concurrency, deadline and failure tests for the real shutdown worker."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="epd-power-") as folder:
    work = Path(folder)
    (work / "freertos").mkdir()
    (work / "freertos/FreeRTOS.h").write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef unsigned TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 25
#define pdMS_TO_TICKS(ms) (ms)
''')
    (work / "freertos/semphr.h").write_text('''#pragma once
#include "FreeRTOS.h"
typedef struct HostSem *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
int xSemaphoreGiveRecursive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
''')
    (work / "freertos/task.h").write_text('''#pragma once
#include "FreeRTOS.h"
typedef struct HostTask *TaskHandle_t;
BaseType_t xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int, TickType_t);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);
''')
    (work / "esp_err.h").write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_INVALID_ARG -2
''')
    (work / "esp_timer.h").write_text('''#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct HostTimer *esp_timer_handle_t;
typedef struct {void (*callback)(void*); void *arg; int dispatch_method; const char *name;} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t*);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_start_once_at(esp_timer_handle_t, uint64_t);
int esp_timer_stop_blocking(esp_timer_handle_t, uint32_t);
int esp_timer_delete(esp_timer_handle_t);
''')
    (work / "esp_log.h").write_text('''#pragma once
void host_log(const char*, const char*, ...);
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
''')
    binary = work / "power"
    for diag in (0, 1):
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g", "-pthread",
                        "-fsanitize=" + os.environ.get("HOST_TEST_SANITIZER", "address,undefined"),
                        f"-DCONFIG_DISPLAY_POWER_DIAG={diag}",
                        "-I" + str(work), "-Icomponents/epdiy/src", "tools/epd_power_guard_host_test.c",
                        "components/epdiy/src/epd_power_guard.c", "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], check=True, timeout=20)
