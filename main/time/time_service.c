/* SPDX-License-Identifier: Apache-2.0
 * 中文：S3 RTC 离线走时；每六小时自动对时一次，已联网就复用现有连接，否则短连已保存 WiFi。
 * 准入在 rc93 有界后台校准上补齐产品守卫：设置开关、电量门与失败冷却/开机上限
 * （判定复用 time_sync_policy.h，无硬件依赖，host 测试覆盖）。
 * English: S3 RTC keeps offline time; auto-calibrate every six hours, reusing an existing link
 * when online and briefly dialing the saved WiFi when offline.
 * rc93's bounded background calibration gains the product guards: the settings switch, a battery
 * floor and failure cooldown/attempt caps (decisions reuse time_sync_policy.h, hardware-free and
 * covered by the host test).
 *
 * 冻结：会话所有权在 wifi_transfer 内部；主循环独占 PMU 与 NVS 交接；手动对时经
 * 组件内抢占，本文件不再管理无线会话。
 * Frozen: session ownership lives inside wifi_transfer; the main loop alone hands time to
 * PMU/NVS; manual syncs preempt inside the component, so this file never manages radio sessions.
 */
#include "time_service.h"
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "device_power.h"
#include "wifi_transfer.h"
#include "settings.h"
#include "sleep.h"
#include "time_sync_policy.h"

#define CLOCK_VALID_MIN 1704067200ULL
#define CLOCK_VALID_MAX 4102444799ULL
#define CLOCK_INTERVAL (6ULL * 3600)
#define CLOCK_RETRY_US (5LL * 60 * 1000000)
// 电量低于该百分比且未充电时不为对时开无线；油量表未知视为可以通过。
// Below this state of charge, and while not charging, keep the radio off; an unknown gauge passes.
#define CLOCK_MIN_BATTERY_PERCENT 10
static atomic_bool s_running;
static bool s_loaded;
static uint64_t s_last_sync;
static int64_t s_next_attempt_us = 15000000;
// 失败冷却与开机尝试上限共用我方策略头；时钟无效时只有首次尝试没有冷却依据。
// Failure cooldown and the per-boot attempt cap share our policy header; with an invalid clock
// there is no cooldown basis, so only a first attempt is allowed.
static unsigned s_boot_attempts;

static void clock_worker(void *arg) {
    (void)arg;
    uint32_t utc = 0;
    esp_err_t err = wifi_transfer_sync_time_auto(&utc);
    ESP_LOGI("clock", "background calibration: %s", esp_err_to_name(err));
    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

void app_time_service_cancel(void) {
    if (atomic_load(&s_running)) {
        // 让组件内 100 ms 分片的 SNTP 等待感知取消；worker 收尾后自行清 running。
        // The component's 100 ms sliced SNTP wait notices the cancel; the worker clears running.
        wifi_transfer_cancel_time_sync();
    }
}

void app_time_service_note_success(uint32_t utc_seconds) {
    if (!app_time_sync_clock_valid(utc_seconds)) return;
    // 手动/升级页成功也算本轮已校准：同步组件持久化记录并清开机重试预算。
    // A manual or updater success counts for this cycle: mirror the component record and clear
    // the per-boot retry budget.
    s_last_sync = utc_seconds;
    s_boot_attempts = 0;
    s_next_attempt_us = esp_timer_get_time() + CLOCK_RETRY_US;
    // 设置页「上次对时」读这条设置记录，判定仍用上面的 last_ntp。
    // The settings page reads this record for "last sync"; decisions still use last_ntp above.
    app_settings_set_time_sync_last(utc_seconds);
    nvs_handle_t handle;
    if (nvs_open("kiiko_clock", NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_u64(handle, "last_ntp", s_last_sync) == ESP_OK) (void)nvs_commit(handle);
        nvs_close(handle);
    }
}

app_time_sync_result_t app_time_service_sync_manual(void) {
    uint32_t utc = 0;
    wifi_transfer_status_t network = {0};
    wifi_transfer_get_status(&network);
    // 手动对时优先：组件内的准入会在同一次调用里自动抢占后台对时。
    // A manual sync goes first: the component's admission preempts an automatic run for us.
    esp_err_t err = wifi_transfer_sync_time(&utc);
    bool network_time_received = err == ESP_OK;
    if (network_time_received) {
        // 临时 WiFi 收尾可能耗时；写入 PMU 时取当前 S3 时间，避免回写旧秒数。
        // Temporary WiFi teardown takes time; seed the PMU from the current S3 clock.
        utc = (uint32_t)time(NULL);
        uint8_t payload[4] = {(uint8_t)utc, (uint8_t)(utc >> 8), (uint8_t)(utc >> 16), (uint8_t)(utc >> 24)};
        err = device_power_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
        if (err == ESP_OK) err = device_power_cmd(PMU_CMD_TIME_GET, NULL, 0);
        if (err == ESP_OK) {
            const pmu_snapshot_t *pmu = device_power_get();
            if (!pmu || !pmu->time_synced || pmu->unix_sec < utc || pmu->unix_sec - utc > 10)
                err = ESP_ERR_INVALID_RESPONSE;
        }
        // 手动对时也算一次成功，后台对时不必再联网重复。
        // A manual sync counts as a success, so background sync need not go online again.
        if (err == ESP_OK) app_time_service_note_success(utc);
    }
    ESP_LOGI("device_settings", "time sync network=%d ready=%d utc=%lu result=%s",
             network.mode, network.network_ready, (unsigned long)utc, esp_err_to_name(err));
    return (app_time_sync_result_t){.error = err, .network_time_received = network_time_received};
}

void app_time_service_poll(bool idle) {
    if (!idle) return;
    if (!s_loaded) {
        nvs_handle_t handle;
        if (nvs_open("kiiko_clock", NVS_READONLY, &handle) == ESP_OK) {
            if (nvs_get_u64(handle, "last_ntp", &s_last_sync) != ESP_OK) s_last_sync = 0;
            nvs_close(handle);
        }
        if (s_last_sync < CLOCK_VALID_MIN || s_last_sync > CLOCK_VALID_MAX) s_last_sync = 0;
        s_loaded = true;
    }
    uint32_t received;
    if (wifi_transfer_take_time_sync(&received)) {
        // 只有真实网络成功才持久化；普通重启/手动设时间不能伪装成网络校准。
        // Persist only a real network success; reboot/manual edits do not count as calibration.
        s_last_sync = received;
        s_boot_attempts = 0;
        (void)app_sync_time_to_pmu();
        // 设置页「上次对时」与后台判定同源；缺了这条记录那行就永远停在旧值。
        // Keep the settings record in step with the background decision, or the page freezes.
        app_settings_set_time_sync_last(received);
        nvs_handle_t handle;
        if (nvs_open("kiiko_clock", NVS_READWRITE, &handle) == ESP_OK) {
            if (nvs_set_u64(handle, "last_ntp", s_last_sync) == ESP_OK) (void)nvs_commit(handle);
            nvs_close(handle);
        }
    }
    const int64_t monotonic = esp_timer_get_time();
    if (atomic_load(&s_running) || monotonic < s_next_attempt_us) return;
    // 用户开关优先；关着就不做任何后台联网，也不推进尝试预算。
    // The user switch wins: while off there is no background networking and no attempt budgeting.
    if (!app_settings_auto_time_sync()) return;
    wifi_transfer_status_t status;
    wifi_transfer_get_status(&status);
    bool online = status.state == WIFI_TRANSFER_READY &&
                  status.mode == WIFI_TRANSFER_MODE_STA && status.network_ready;
    if (!online && status.state != WIFI_TRANSFER_STOPPED) return;
    time_t now = time(NULL);
    // 节奏固定 6 小时：已联网就复用现有连接，不额外开射频；离线才短连已保存 WiFi。
    // A flat six-hour cadence: reuse an existing link when online, otherwise dial the saved WiFi.
    uint64_t interval = CLOCK_INTERVAL;
    // 记录比时钟超前（时钟被调回）按刚校准处理，避免追赶式联网；时钟无效只允许开机首试。
    // 重试节奏由 next_attempt_us（5 分钟，单调时钟）与开机上限承担；持久化的 tsync_try
    // 只做记录，读回来会破坏测试锁定的 6 小时节奏。
    // A record ahead of the clock (clock moved back) counts as fresh to avoid catch-up storms;
    // with an invalid clock only the first boot attempt is allowed. Retry rhythm comes from
    // next_attempt_us (5 minutes, monotonic) and the boot cap; the persisted tsync_try stays a
    // record only — reading it back would break the tested six-hour cadence.
    if (app_time_sync_clock_valid((uint32_t)now)) {
        if (!app_time_sync_record_usable((uint32_t)now, (uint32_t)s_last_sync)) return;
        if (s_last_sync && (uint64_t)now >= s_last_sync && (uint64_t)now - s_last_sync < interval)
            return;
    } else if (s_boot_attempts != 0) {
        return;
    }
    if (s_boot_attempts >= APP_TIME_SYNC_ATTEMPTS_PER_BOOT) return;
    // 电量极低且未充电时不为了对时开无线。/ Low and unplugged: not worth a radio session.
    const pmu_snapshot_t *pmu = device_power_get();
    int percent = pmu_battery_percent(pmu);
    if (percent >= 0 && percent < CLOCK_MIN_BATTERY_PERCENT && !pmu_battery_charging(pmu)) return;
    s_next_attempt_us = monotonic + CLOCK_RETRY_US;
    if (!online) {
        char ssid[33]; bool configured = false;
        if (wifi_transfer_get_saved_wifi(ssid, &configured) != ESP_OK || !configured) return;
    }
    atomic_store(&s_running, true);
    // 仅对时期间保留任务栈；不创建常驻无线任务或页面缓存。
    // Keep a task stack only during calibration; no permanent radio task or page cache.
    if (xTaskCreate(clock_worker, "rtc_sync", 6144, NULL, 1, NULL) != pdPASS) {
        atomic_store(&s_running, false);
        ESP_LOGW("clock", "background calibration deferred: no task memory");
        return;
    }
    // 失败也要记预算：attempt 记在尝试开始，冷却由 next_attempt_us 与策略头共同兜底。
    // Failures budget too: the attempt is recorded at start, with cooldown from next_attempt_us
    // and the policy header as the backstop.
    ++s_boot_attempts;
    app_settings_set_time_sync_attempt(app_time_sync_clock_valid((uint32_t)now) ? (uint32_t)now : 0);
}
