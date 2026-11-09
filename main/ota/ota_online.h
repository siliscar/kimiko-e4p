/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：联网更新的版本清单与可取消后台任务。/ English: release metadata and cancellable online update jobs.
 * 冻结：只写空闲应用槽，完整校验后由 UI 确认启动。/ Frozen: only write the inactive app slot; UI commits boot after verification.
 * 用户修订：固件擦写与屏幕推送互斥，进度只追加变化像素；不并行驱动 PSRAM 画面和闪存擦写。
 * User revision: serialize firmware flash operations with display output; append progress changes without concurrent PSRAM scans and flash writes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PICO_OTA_LAYOUT "mogo-e4p-dual-5m-v1"
#define PICO_OTA_BOARD "METALIO-EINK4P"
#define PICO_OTA_CAPABILITY "PICO_HTTPS_OTA_V1"
// 升级源在 menuconfig 中配置；宿主测试不带 sdkconfig，回落到当前正式站点。
// Sources come from menuconfig; host tests without sdkconfig fall back to the public sites.
#ifdef CONFIG_PICO_OTA_FEED_URL
#define PICO_OTA_FEED_URL CONFIG_PICO_OTA_FEED_URL
#else
#define PICO_OTA_FEED_URL "https://mogo.mantake.shop/api/v2/ota/devices/mogo-e4p/update.json"
#endif
#ifdef CONFIG_PICO_OTA_FEED_FALLBACK_URL
#define PICO_OTA_FEED_FALLBACK_URL CONFIG_PICO_OTA_FEED_FALLBACK_URL
#else
#define PICO_OTA_FEED_FALLBACK_URL "https://wegooo-cell.github.io/read-pico-reader/update.json"
#endif
// 单槽容量决定清单允许的最大镜像，与 partitions_16M.csv 的 5 MB 槽一致。
// One slot caps the manifest image size; matches the 5 MB slots in partitions_16M.csv.
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#define PICO_OTA_SLOT_SIZE 0x500000u
#else
#define PICO_OTA_SLOT_SIZE 0x400000u
#endif

typedef enum { PICO_UPDATE_IDLE, PICO_UPDATE_CHECKING, PICO_UPDATE_AVAILABLE,
               PICO_UPDATE_LATEST, PICO_UPDATE_DOWNLOADING, PICO_UPDATE_READY,
               PICO_UPDATE_FAILED, PICO_UPDATE_CANCELLED } pico_update_state_t;
typedef struct {
    char version[33], project[33], board[24], layout[32], minimum_base_version[33];
    char url[256], sha256[65], notes[384];
    uint32_t size;
} pico_release_t;
typedef struct {
    pico_update_state_t state;
    pico_release_t release;
    uint32_t revision, received;
    char message[96];
    bool busy;
} pico_update_status_t;

/// 有界解析、型号与版本验证；不接触网络。/ Bounded parsing, board and version validation without networking.
esp_err_t pico_release_parse(const char *json, size_t length, pico_release_t *release);
int pico_version_compare(const char *a, const char *b);
bool pico_release_url_valid(const char *url);
/// 检查或下载唯一后台任务；离页、锁屏先取消并等待。/ Start one worker; cancel and join on exit or lock.
esp_err_t pico_online_check(void);
esp_err_t pico_online_download(void);
bool pico_online_busy(void);
void pico_online_get_status(pico_update_status_t *status);
void pico_online_cancel_join(void);
/// 刷屏与 OTA 擦写共用递归锁；未初始化 OTA 时不分配内存。/ Recursive display/OTA exclusion; no allocation before OTA initialization.
bool pico_online_display_begin(void);
void pico_online_display_end(bool held);
/// 已验证镜像切换启动槽；失败不切换。/ Select a verified image; failures keep the existing boot slot.
esp_err_t pico_online_commit(void);
