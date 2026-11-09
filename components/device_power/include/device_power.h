/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 设备电源接口：电池、充电、RTC、电源键与关机。
 * Metalio 后端提供实现，共用快照与旧协议标识保持兼容。
 *
 * Device power interfaces: battery, charging, RTC, power keys and power-off.
 * The Metalio backend implements these APIs while retaining shared snapshot
 * and legacy protocol identifiers.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "pmu_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t low_mv;
    uint16_t critical_mv;
    uint16_t key_long_ms;
    uint16_t key_force_event_ms;
    uint16_t key_power_on_ms;
    uint16_t key_force_off_ms;
    uint16_t full_mv;
    uint32_t generation;
    uint16_t forced_off_mv;
    uint16_t hysteresis_mv;
    uint16_t key_debounce_ms;
    uint16_t host_boot_timeout_ms;
    uint16_t shutdown_timeout_ms;
    uint16_t led_override_timeout_ms;
    uint8_t wake_on_charge;
    uint8_t key_raw_events;
    uint8_t charge_led;
    uint8_t low_soc_enable;
    uint16_t low_soc_threshold;
} pmu_config_t;

typedef struct {
    bool present;
    bool identity_ok;
    bool status_ok;
    bool diag_ok;
    bool event_ok;
    bool config_ok;
    bool time_ok;
    bool alarm_ok;

    uint8_t proto_major;
    uint8_t proto_minor;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t hw_rev;
    uint16_t device_id;
    uint8_t uid[PMU_CHIP_UID_LEN];
    bool uid_ok;
    uint32_t boot_id;
    uint32_t caps;
    uint16_t build_number;
    uint32_t build_hash;

    uint16_t generation;
    uint8_t power_state;
    uint8_t wake_reason;
    uint32_t flags;
    uint16_t battery_mv;
    uint16_t battery_raw;
    uint16_t soc_permille;
    int16_t mcu_temp_centi;
    uint8_t charge_state;
    uint8_t key_state;
    uint8_t led_state;
    uint8_t host_state;
    uint32_t fault_flags;
    uint8_t pending_events;
    uint8_t reset_reason;
    uint8_t last_cmd_status;
    uint32_t uptime_ms;
    uint32_t config_generation;
    uint16_t last_cmd_seq;
    uint16_t last_event_id;
    uint32_t last_action_reason;

    uint16_t diag_i2c_rx;
    uint16_t diag_i2c_recov;
    uint16_t diag_crc;
    uint16_t diag_bad_len;
    uint16_t diag_unknown;
    uint16_t diag_overflow;
    uint16_t diag_sleep;
    uint16_t diag_host_rst;
    uint16_t diag_adc;

    bool quick_ok;
    uint16_t qb_mv;
    uint16_t qb_soc;
    uint8_t qb_charge;
    uint8_t qb_flags;

    pmu_event_t event;
    pmu_config_t config;
    uint32_t unix_sec;
    uint16_t unix_millis; ///< TIME_GET 毫秒部分 / Millisecond fraction
    uint8_t time_synced;
    uint8_t alarm_mode;
    uint32_t alarm_remain;
    uint32_t alarm_target;
    /// RTC_RAW：CR0 CR1 CR2 PSC DATE TIME SSCNT SYSCTRL_CR1 SYSCTRL_LSI
    /// / RTC_RAW: CR0 CR1 CR2 PSC DATE TIME SSCNT SYSCTRL_CR1 SYSCTRL_LSI
    uint32_t rtc_raw[9];
    bool rtc_raw_ok;

    uint16_t last_op_code;
    uint16_t last_op_status;
    esp_err_t last_err;

} pmu_snapshot_t;

/// 共用电量接口：优先采用有效的兼容轻量快照，否则读取状态快照；未知返回 -1。
/// / Shared battery API: prefer a valid compatibility quick snapshot, then status; unknown returns -1.
static inline int pmu_battery_percent(const pmu_snapshot_t *p) {
    if (!p || !p->present) return -1;
    uint16_t soc;
    if (p->quick_ok) {
        if ((p->qb_flags & 3) != 3) return -1;
        soc = p->qb_soc;
    } else {
        if (!p->status_ok || !(p->flags & PMU_STATUS_BATTERY_VALID)) return -1;
        soc = p->soc_permille;
    }
    return soc <= 1000 ? (soc + 5) / 10 : -1;
}
static inline bool pmu_battery_charging(const pmu_snapshot_t *p) {
    return p && p->present && (p->quick_ok ? (p->qb_flags & 4) != 0 :
        p->status_ok && (p->flags & PMU_STATUS_CHARGING_ACTIVE) != 0);
}

#ifndef DEVICE_POWER_I2C_HZ_DEFAULT
#define DEVICE_POWER_I2C_HZ_DEFAULT 400000
#endif

typedef struct {
    uint16_t i2c_addr;
    uint32_t scl_speed_hz;
} device_power_config_t;

#define DEVICE_POWER_CONFIG_DEFAULT() \
    { \
        .i2c_addr = PMU_I2C_ADDR, \
        .scl_speed_hz = DEVICE_POWER_I2C_HZ_DEFAULT, \
    }

esp_err_t device_power_init(i2c_master_bus_handle_t bus_handle, const device_power_config_t* config);
esp_err_t device_power_deinit(void);
bool device_power_ready(void);
const pmu_snapshot_t* device_power_get(void);

/// 刷新当前板状态；Metalio 后端与 poll 共用电量和电源键采样。
/// / Refresh current-board state; the Metalio backend shares battery/key sampling with poll.
esp_err_t device_power_refresh(void);
/// 轮询电量、充电状态与待处理电源键，供页面 tick 使用。
/// / Poll battery, charging state and pending power keys for page ticks.
esp_err_t device_power_poll(void);

/// 创建默认暂停的电源键快采任务；失败时保留原轮询，由主循环调用一次。
/// Create the initially paused fast key sampler once from the loop; failure retains direct polling.
bool device_power_key_sampler_start(void);
/// 主循环切换快采并返回之前的启用状态；停用等待在途读取、丢弃旧动作及当前释放，浅睡/独占前必须停用。
/// Loop-only: switch sampling and return its previous state; disable joins reads and discards stale actions/releases before sleep/exclusive use.
bool device_power_key_sampler_enable(bool enabled);

/// 校准日志默认关闭；清醒时每分钟及关键事件异步写串口和 TF，格式 2 区分采样时间与有效性。
/// Calibration logging defaults off; awake minute samples and key events asynchronously reach serial/TF in timestamped, validity-aware format 2.
void device_power_set_batt_log(bool enabled);

typedef enum {
    DEVICE_POWER_BATT_LOG_LOCK = 1u << 7,         ///< 锁屏等待开始 / Locked wait begins
    DEVICE_POWER_BATT_LOG_WAKE = 1u << 8,         ///< 锁屏等待结束 / Locked wait ends
    DEVICE_POWER_BATT_LOG_OFF = 1u << 9,          ///< 请求断电 / Power-off requested
    DEVICE_POWER_BATT_LOG_RESTART = 1u << 10,     ///< 请求重启 / Restart requested
    DEVICE_POWER_BATT_LOG_UNDERVOLTAGE = 1u << 11, ///< 欠压保护请求 / Undervoltage protection requested
} device_power_batt_log_event_t;
/// 只排队，不读硬件或写卡；事件时间与最近采样时间分别保留，关闭时只跟踪锁屏状态。
/// Enqueue without hardware/file I/O; preserve separate event/sample times, tracking only lock state while disabled.
void device_power_batt_log_event(device_power_batt_log_event_t event);
/// 有界等待日志任务交还；成功只说明已处理，实际文件失败计数见日志。
/// Bounded wait for logger handoff; success means processed, with file-failure counters in the log.
bool device_power_batt_log_flush(uint32_t timeout_ms);
/// 板级挂载后开放写入，卸载/格式化前关闭并等待在途 I/O；超时不得卸载。
/// Board mounts allow writes; close the gate and join in-flight I/O before unmount/format, aborting those operations on timeout.
bool device_power_batt_log_storage(bool available, uint32_t timeout_ms);

esp_err_t device_power_cmd(uint16_t code, const uint8_t* payload, uint8_t plen);
/// 读出厂绑定的面板 VCOM。valid 则 *out_mv 为 500..2500，
/// 未标定 / 旧固件 / 读失败一律 ESP_ERR_NOT_FOUND。
/// / Read factory-bound panel VCOM. When valid, *out_mv is 500..2500.
/// Unset / old firmware / read failure all return ESP_ERR_NOT_FOUND.
esp_err_t device_power_vcom_get(int* out_mv);
/// 出厂标定页写入。mv 须 500..2500 且整十。
/// / Factory setup-page write. mv must be 500..2500 and a multiple of 10.
esp_err_t device_power_vcom_set(int mv);
/// 旧协议 UID 兼容接口；Metalio 后端返回 ESP_ERR_NOT_FOUND。
/// / Legacy protocol UID compatibility API; the Metalio backend returns ESP_ERR_NOT_FOUND.
esp_err_t device_power_uid_get(uint8_t out[PMU_CHIP_UID_LEN]);
esp_err_t device_power_event_ack(uint16_t event_id);
esp_err_t device_power_action(uint8_t action, uint16_t delay_ms, uint16_t reason);
typedef enum {
    DEVICE_POWER_KEY_NONE = 0,
    DEVICE_POWER_KEY_SHORT,
    DEVICE_POWER_KEY_LONG,
} device_power_key_action_t;
/// 抽干事件队列，不解释按键（开机丢掉上电/烧录残留）。
/// / Drain the event queue without interpreting keys (drop boot/flash leftovers).
void device_power_drain_events(void);
/// 抽干事件队列并返回本轮最高优先级按键动作；长按优先于短按。
/// / Drain the queue and return the highest-priority key action; long wins over short.
device_power_key_action_t device_power_take_key_action(void);
/// 抽干事件队列；若有电源键短按返回 true。
/// / Drain the queue; true if a power-key short press was seen.
bool device_power_take_key_short(void);
/// 抽干队列；按下或短按都算唤醒（浅睡常先收到 KEY_DOWN）。
/// / Drain the queue; KEY_DOWN or SHORT both count as wake (light sleep often sees KEY_DOWN first).
bool device_power_take_key_wakeup(void);
/// 丢弃本次唤醒按键的按下/释放动作，避免解锁后被当成新的短按或长按。
/// / Discard the wake press/release so it cannot become a new short or long press after unlock.
void device_power_consume_key_press(void);
/// 软睡兼容入口；Metalio 返回不支持，由应用处理关机交接。
/// / Soft-sleep compatibility entry; Metalio reports unsupported for application power-off handoff.
esp_err_t device_power_report_sleep(void);
/// 将兼容快照标记为 RUNNING。
/// / Mark the compatibility snapshot RUNNING.
esp_err_t device_power_report_ready(void);
/// 当前板关机：发送电源键脉冲，耗尽重试后进入 ESP 深睡。
/// / Current-board power-off: send power-key pulses, then enter ESP deep sleep after the retry limit.
esp_err_t device_power_power_off(void);

const char* pmu_power_name(uint8_t state);
const char* pmu_charge_name(uint8_t state);
const char* pmu_event_name(uint8_t type);
const char* pmu_status_name(uint16_t status);
const char* pmu_led_name(uint8_t color);
const char* pmu_action_name(uint8_t action);

#ifdef __cplusplus
}
#endif
