/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 的 device_power 兼容层：
 * BQ27220 电量、CX25601N 充电状态、PCF8563 RTC、TCA9555 P17 电源键。
 * 电量在电压/电流之上滤波：负载补偿、EMA、分段曲线、单向限速，并持久化到 NVS。
 *
 * device_power compatibility layer for Metalio E-INK4 Plus:
 * BQ27220 gauge, CX25601N charger status, PCF8563 RTC and TCA9555 P17 key.
 * The percent is filtered on top of voltage/current: load compensation, EMA, a piecewise
 * curve and one-direction rate limiting, persisted to NVS.
 * 没有 CW32 PMU，因此软睡/关机按新板电源路径实现或降级。
 * There is no CW32 PMU; soft sleep / power-off follow the new board's rails.
 *
 * 充电与 RTC 参考 Metalio 原厂 power/cx25601n.cc 和 sensor/pcf8563.cc。
 * Charging and RTC follow Metalio vendor power/cx25601n.cc and sensor/pcf8563.cc.
 * 原厂来源版本和 MIT 许可见 components/board_i2c/README.md 与 LICENSE.Metalio。
 * Vendor revision and MIT notice: components/board_i2c/README.md and LICENSE.Metalio.
 */

#include "device_power.h"
#include "battery_log.h"

#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_system.h"
#include "board_i2c.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "fca9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define TAG "pmu"

#define BQ27220_ADDR 0x55
#define CX25601N_ADDR 0x6B
#define PCF8563_ADDR 0x51
#define I2C_TIMEOUT_MS 100

#define BQ_REG_VOLTAGE 0x08
#define BQ_REG_CURRENT 0x0C
// StateOfCharge() 寄存器，地址按 TRM 复核。 / StateOfCharge() register; re-check the address against the TRM.
#define BQ_REG_SOC 0x2C
#define CX_REG_ICHG_LO 0x02
#define CX_REG_ICHG_HI 0x03
#define CX_REG_VREG_LO 0x04
#define CX_REG_VREG_HI 0x05
#define CX_REG_IINDPM_LO 0x06
#define CX_REG_IINDPM_HI 0x07
#define CX_REG_IPRECHG_LO 0x10
#define CX_REG_IPRECHG_HI 0x11
#define CX_REG_ITERM_LO 0x12
#define CX_REG_ITERM_HI 0x13
#define CX_REG_CHG_CTRL0 0x14
#define CX_REG_CHG_TMR 0x15
#define CX_REG_CHG_CTRL1 0x16
#define CX_REG_CHG_CTRL3 0x18
#define CX_REG_STATUS1 0x1E
#define CX_REG_PART_INFO 0x38
#define CX_REG_UNLOCK 0x70
// STATUS1 bits[4:3]：0 未充电/已满、1 涓流/预充/CC、2 恒压降流、3 top-off。
// 编码取自原厂驱动 cx25601n.h（CX25601N_CHG_STAT_*）。
// STATUS1 bits[4:3]: 0 not charging/full, 1 trickle/precharge/CC, 2 CV taper, 3 top-off.
// The encoding comes from the vendor driver's cx25601n.h (CX25601N_CHG_STAT_*).
#define CX_CHG_STAT_CC 1
#define CX_CHG_STAT_CV 2
#define CX_CHG_STAT_TOPOFF 3
// 原厂板级默认快速充电 1000 mA（MetalioPower_Init 的 kDefaultIchgMa），输入限流同值，
// 充电截止 4.4 V。不写这些寄存器时芯片停在上次遗留值，换电池/掉电后可能回落到默认档。
// Vendor board default fast charge is 1000 mA (MetalioPower_Init's kDefaultIchgMa), the
// input limit follows it and VREG is 4.4 V. Without these writes the chip keeps whatever
// an earlier firmware left behind and can fall back to its power-on defaults.
#define CX_CHG_ICHG_MA 1000
#define CX_CHG_VREG_MV 4400

#define PCF_REG_SECONDS 0x02
#define PCF_BIT_VL 0x80

#define KEY_LONG_MS 1000
#define KEY_DEBOUNCE_MS 30
// 按下沿同一次轮询内复查的间隔：主循环 200 ms 才读一次键，单次 I2C 毛刺原本会直接
// 变成短按并把设备锁屏。复查只延后确认，按下时刻仍取第一次低电平。
// / Gap for rechecking a press edge inside one poll: the loop reads the key only every
// 200 ms, so a single I2C glitch used to become a short press that locks the device. The
// recheck only delays confirmation; the press time stays at the first low sample.
#define KEY_CONFIRM_DELAY_MS 20
#define KEY_SAMPLE_PERIOD_MS 10
#define KEY_WAKE_BATT_REUSE_US (10LL * 1000)

// TCA9555 P17 = Port1 bit7。 / TCA9555 P17 = Port1 bit7.
#define KEY_POWER_MASK 0x80
#define PWR_PULSE_COUNT 15
#define PWR_PULSE_INTERVAL_MS 100
// 单轮 train 不掉电就按原厂再打；打满仍活着才进 ESP 深睡兜底。
// If one train does not cut power, repeat like the factory firmware; ESP deep sleep is the last resort.
#define PWR_OFF_TRAIN_LIMIT 10

// board_support 组件在 final link 提供；不反向依赖，避免组件环。
// Provided by the board_support component at final link; declared here to avoid a component cycle.
extern fca9555_handle_t board_fca9555(void);
extern esp_err_t board_pwr_pulse(bool high);

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_bq;
static i2c_master_dev_handle_t s_cx;
static i2c_master_dev_handle_t s_rtc;

static bool s_ready;
static pmu_snapshot_t s_snap;
static int s_vcom_mv = 1200;

static bool s_key_down;
static int64_t s_key_down_us;
static bool s_key_long_fired;
static bool s_key_short_pending;
static bool s_key_long_pending;
// 按键状态由主循环和快采任务共享；锁内完成整次读数及防抖，暂停返回时已无在途读取。
// The loop and fast sampler share key state; the mutex covers each read/debounce so pause joins in-flight reads.
static StaticSemaphore_t s_key_mutex_storage;
static SemaphoreHandle_t s_key_mutex;
static portMUX_TYPE s_key_init_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_key_task;
static bool s_key_sampling;

static void key_lock(void) {
    portENTER_CRITICAL(&s_key_init_mux);
    if (s_key_mutex == NULL) s_key_mutex = xSemaphoreCreateMutexStatic(&s_key_mutex_storage);
    portEXIT_CRITICAL(&s_key_init_mux);
    (void)xSemaphoreTake(s_key_mutex, portMAX_DELAY);
}

static void key_unlock(void) { (void)xSemaphoreGive(s_key_mutex); }

static uint8_t bcd_to_dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }
static uint8_t dec_to_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t dev_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* data, size_t len) {
    if (dev == NULL || data == NULL) return ESP_ERR_INVALID_STATE;
    return board_i2c_read(dev, reg, data, len, I2C_TIMEOUT_MS);
}

static esp_err_t dev_write(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t* data,
                           size_t len) {
    if (dev == NULL || data == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t buf[16];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_ARG;
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return board_i2c_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t add_dev(uint8_t addr, i2c_master_dev_handle_t* out) {
    *out = NULL;
    esp_err_t err = board_i2c_probe(s_bus, addr, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    return board_i2c_add_device(s_bus, addr, out);
}

/* ---- CX25601N 充电配置 / CX25601N charger setup ---- */

// 位域更新沿用原厂驱动的语义：mask 是字段宽度，shift 是字段起始位。
// Field update with the vendor driver's semantics: mask is the field width, shift its start bit.
static esp_err_t cx_update_bits(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t field) {
    esp_err_t err = board_i2c_lock(I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    uint8_t cur = 0;
    err = dev_read(s_cx, reg, &cur, 1);
    if (err == ESP_OK) {
        cur = (uint8_t)((cur & ~(mask << shift)) | ((field & mask) << shift));
        err = dev_write(s_cx, reg, &cur, 1);
    }
    board_i2c_unlock();
    return err;
}

// 充电截止电压，10 mV 一档：低 5 位在 0x04[7:3]，高 4 位在 0x05[3:0]。
// Charge regulation in 10 mV steps: low 5 bits in 0x04[7:3], high 4 in 0x05[3:0].
static esp_err_t cx_set_vreg_mv(uint32_t mv) {
    if (mv < 3840) mv = 3840;
    if (mv > 4800) mv = 4800;
    const uint32_t code = mv / 10;
    esp_err_t err = cx_update_bits(CX_REG_VREG_LO, 0x1F, 3, (uint8_t)(code & 0x1F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_VREG_HI, 0x0F, 0, (uint8_t)((code >> 5) & 0x0F));
    return err;
}

// 充电电流 80 mA 一档，并像原厂一样把输入限流（20 mA 一档）设成同一个值。
// Charge current in 80 mA steps; the input limit (20 mA steps) follows it as the vendor does.
static esp_err_t cx_set_ichg_ma(uint32_t ma) {
    if (ma < 80) ma = 80;
    if (ma > 3040) ma = 3040;
    const uint32_t icode = ma / 80;
    const uint32_t applied_ma = icode * 80;
    const uint32_t in_ma = applied_ma < 100 ? 100 : (applied_ma > 3000 ? 3000 : applied_ma);
    const uint32_t pcode = in_ma / 20;
    esp_err_t err = cx_update_bits(CX_REG_ICHG_LO, 0x03, 6, (uint8_t)(icode & 0x03));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ICHG_HI, 0x0F, 0, (uint8_t)((icode >> 2) & 0x0F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IINDPM_LO, 0x0F, 4, (uint8_t)(pcode & 0x0F));
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IINDPM_HI, 0x0F, 0, (uint8_t)((pcode >> 4) & 0x0F));
    return err;
}

// 私有寄存器区：按原厂顺序解锁（0x70 写 00/50/57/44，直到读回 0x03）。
// Private block: unlock in the vendor's order (0x70 = 00/50/57/44 until it reads back 0x03).
static esp_err_t cx_unlock_private(void) {
    for (int i = 0; i < 10; i++) {
        static const uint8_t seq[] = {0x00, 0x50, 0x57, 0x44};
        for (size_t k = 0; k < sizeof(seq); k++) {
            esp_err_t err = dev_write(s_cx, CX_REG_UNLOCK, &seq[k], 1);
            if (err != ESP_OK) return err;
        }
        uint8_t v = 0;
        esp_err_t err = dev_read(s_cx, CX_REG_UNLOCK, &v, 1);
        if (err != ESP_OK) return err;
        if (v == 0x03) return ESP_OK;
    }
    ESP_LOGW(TAG, "CX25601N private unlock failed");
    return ESP_ERR_INVALID_STATE;
}

// 照搬原厂 hw_init_defaults()：关 DPDM 探测和看门狗，预充/终止用原厂码值，VREG 4.4 V，
// 充电与输入限流 1 A，充电期间由轮询服务按原厂 VBAT/VREG 策略控制终止，最后开 EN_CHG。
// Mirrors the vendor hw_init_defaults(): DPDM probing and the watchdog stay off, precharge and
// termination keep the vendor codes, VREG is 4.4 V, charge and input limit are 1 A, termination
// follows the vendor VBAT/VREG policy through polling, then EN_CHG is set.
static esp_err_t cx_charger_setup(void) {
    if (s_cx == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t part = 0;
    esp_err_t err = dev_read(s_cx, CX_REG_PART_INFO, &part, 1);
    if (err != ESP_OK) goto failed;

    err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_TMR, 0x01, 6, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 4, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x03, 0, 0);
    // 预充与终止电流沿用原厂码值，单位未公开。
    // Precharge and termination keep the vendor codes; their units are not public.
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IPRECHG_LO, 0x0F, 4, 0x0C);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_IPRECHG_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ITERM_LO, 0x1F, 3, 0x06);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_ITERM_HI, 0x01, 0, 0x00);
    if (err == ESP_OK) err = cx_set_vreg_mv(CX_CHG_VREG_MV);
    if (err == ESP_OK) err = cx_set_ichg_ma(CX_CHG_ICHG_MA);
    if (err != ESP_OK) goto failed;

    // 私有区三个值只由原厂驱动写入，含义未公开，照抄。
    // The three private values are written only by the vendor driver; their meaning is undocumented.
    err = cx_unlock_private();
    const uint8_t v86 = 0x06, v3a = 0x10, v46 = 0x20, zero = 0x00;
    if (err == ESP_OK) err = dev_write(s_cx, 0x86, &v86, 1);
    if (err == ESP_OK) err = dev_write(s_cx, 0x3A, &v3a, 1);
    if (err == ESP_OK) err = dev_write(s_cx, 0x46, &v46, 1);
    const esp_err_t relock = dev_write(s_cx, CX_REG_UNLOCK, &zero, 1);
    if (err == ESP_OK) err = relock;
    if (err != ESP_OK) goto failed;

    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 0, 1);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 2, 0);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL3, 0x01, 2, 0);
    if (err == ESP_OK) err = cx_update_bits(0x1A, 0x01, 7, 1);
    if (err == ESP_OK) err = cx_update_bits(0x23, 0x07, 2, 0x07);
    if (err == ESP_OK) err = cx_update_bits(0x24, 0x01, 3, 1);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 1);

    // 回读配置：串口一眼能看出快充有没有写进去。
    // Read the configuration back so the serial log shows whether fast charge took effect.
    uint8_t lo = 0, hi = 0;
    long ichg_ma = -1, iindpm_ma = -1, vreg_mv = -1;
    if (dev_read(s_cx, CX_REG_ICHG_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_ICHG_HI, &hi, 1) == ESP_OK) {
        ichg_ma = (long)((((lo >> 6) & 0x03) | ((hi & 0x0F) << 2)) * 80);
    }
    if (dev_read(s_cx, CX_REG_IINDPM_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_IINDPM_HI, &hi, 1) == ESP_OK) {
        iindpm_ma = (long)((((lo >> 4) & 0x0F) | ((hi & 0x0F) << 4)) * 20);
    }
    if (dev_read(s_cx, CX_REG_VREG_LO, &lo, 1) == ESP_OK &&
        dev_read(s_cx, CX_REG_VREG_HI, &hi, 1) == ESP_OK) {
        vreg_mv = (long)((((lo >> 3) & 0x1F) | ((hi & 0x0F) << 5)) * 10);
    }
    ESP_LOGI(TAG, "CX25601N part=0x%02X ichg=%ldmA iindpm=%ldmA vreg=%ldmV (%s)", part,
             ichg_ma, iindpm_ma, vreg_mv, err == ESP_OK ? "ok" : esp_err_to_name(err));
    if (err == ESP_OK) return ESP_OK;
failed:
    // 部分初始化失败时尽力恢复硬件终止并停充，避免无维护的半配置状态。
    // On partial setup failure, restore hardware termination and disable charging where communication permits.
    (void)cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 2, 1);
    (void)cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 0);
    return err;
}


static bool s_charger_configured, s_charger_uv_shutdown;
static int64_t s_charger_next_us, s_charger_resume_us;

// 主机停止维护前恢复硬件终止，不能把补充充电的暂时停充阶段留给断电后的芯片。
// Restore hardware termination before host maintenance stops; never leave a recharge disable pending after power-off.
static esp_err_t cx_charger_park(void) {
    if (!s_charger_configured || s_cx == NULL) return ESP_OK;
    esp_err_t err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 2, 1);
    if (s_charger_resume_us != 0) {
        esp_err_t resume = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 4, 0);
        if (resume == ESP_OK) resume = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 1);
        if (resume == ESP_OK) s_charger_resume_us = 0;
        if (err == ESP_OK) err = resume;
    }
    return err;
}

// 原厂 charger_vreg_task 的寄存器策略，转为已有轮询上的非阻塞步骤。
// Vendor charger_vreg_task register policy expressed as nonblocking steps on existing polling.
static void cx_charger_poll(int64_t now, bool have_v, uint16_t mv,
                            bool have_i, int16_t ma, bool have_status, uint8_t status) {
    if (!s_charger_configured) return;
    if (s_charger_resume_us != 0) {
        if (now < s_charger_resume_us) return;
        esp_err_t err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 4, 0);
        if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 1);
        if (err != ESP_OK) return;
        s_charger_resume_us = 0;
        s_charger_next_us = now + 500000;
        return;
    }
    if (now < s_charger_next_us) return;
    s_charger_next_us = now + 2000000;
    // 无有效电压时不作充电终止或欠压判断；通信失败会在下轮重试。
    // Invalid voltage never drives termination or undervoltage decisions; retry communication next time.
    if (!have_v) return;
    const bool below = mv < CX_CHG_VREG_MV;
    const uint8_t vbus = status & 0x07;
    if (!s_charger_uv_shutdown && mv <= 3100 && have_status && vbus == 0) {
        device_power_batt_log_event(DEVICE_POWER_BATT_LOG_UNDERVOLTAGE);
        const uint8_t shutdown = 0x01;
        if (dev_write(s_cx, CX_REG_CHG_CTRL3, &shutdown, 1) == ESP_OK) {
            s_charger_uv_shutdown = true;
        }
        return;
    }
    if (mv > 3200) s_charger_uv_shutdown = false;
    if (s_charger_uv_shutdown && (!have_status || vbus == 0)) return;
    esp_err_t err = cx_set_vreg_mv(CX_CHG_VREG_MV);
    if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL0, 0x01, 2, !below);
    if (err != ESP_OK) return;
    s_charger_next_us = now + (below ? 2000000 : 10000000);
    // 补充充电只接受有效且确有输入的状态，负向电流按原厂折为零。
    // Recharge requires valid input-present status; negative current is clamped to zero as in the vendor.
    const int32_t charge_ma = ma > 0 ? ma : 0;
    if (below && have_i && have_status && vbus != 0 && ((status >> 3) & 3) == 0 && charge_ma < 5) {
        err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 5, 0);
        if (err == ESP_OK) err = cx_update_bits(CX_REG_CHG_CTRL1, 0x01, 4, 0);
        // 禁止充电的写入即使返回失败也可能已生效，必须安排恢复。
        // Even a failed disable write may have reached the chip; always schedule restoration.
        s_charger_resume_us = now + 200000;
    }
}


static esp_err_t bq_read_voltage(uint16_t* mv) {
    uint8_t b[2] = {0};
    esp_err_t err = dev_read(s_bq, BQ_REG_VOLTAGE, b, sizeof(b));
    if (err != ESP_OK) return err;
    *mv = (uint16_t)(b[0] | (b[1] << 8));
    return ESP_OK;
}

static esp_err_t bq_read_current(int16_t* ma) {
    uint8_t b[2] = {0};
    esp_err_t err = dev_read(s_bq, BQ_REG_CURRENT, b, sizeof(b));
    if (err != ESP_OK) return err;
    *ma = (int16_t)(b[0] | (b[1] << 8));
    return ESP_OK;
}

/* ---- 电池滤波状态机 / Battery filter ---- */

// 滤波参数：真机日志（batt: ...）校准后可调。 / Filter tunables; calibrate from the "batt:" logs.
// 电池内阻：2026-10-25 卡上日志 13 次充放电切换（60 s 采样、ΔI≥80 mA）中位 180 mΩ。
// 原 100 mΩ 补偿不足，拔掉充电器那一分钟负载压降只补了一半，满电显示瞬间掉 3%。
// 取该组中位；2026-10-27 复核 14 次切换中位 206 mΩ，其中三次 1 A 大步 175–192 mΩ，维持 180。
// Pack resistance: the 2026-10-25 card log's 13 charge/discharge switches (60 s samples,
// ΔI ≥ 80 mA) median 180 mΩ. The old 100 mΩ under-compensated the load step, so the full
// display fell 3% in the first minute off the charger. Re-checked 2026-10-27: 14 switches
// median 206 mΩ, the three clean 1 A steps 175–192 mΩ; 180 stays.
#define BATT_R_MOHM 180
#define BATT_COMP_CAP_MV 150             // 负载补偿上限。 / Load compensation cap.
#define BATT_EMA_TAU_MS 25000            // 电压 EMA 时间常数。 / Voltage EMA time constant.
#define BATT_DEADBAND_PERMILLE 10        // 差距小于 1% 不动显示。 / Ignore sub-1% movement.
#define BATT_MAX_STEP_PERMILLE 10        // 单次可见变化最多 1%。 / At most 1% per visible step.
#define BATT_MIN_STEP_INTERVAL_MS 60000  // 正常电量每分钟最多变化 1%。 / Normal levels move at most 1% per minute.
#define BATT_LOW_STEP_INTERVAL_MS 10000  // 低电量保留快速跟随。 / Keep faster tracking at low levels.
#define BATT_LOW_PERMILLE 140            // 保留 3.6 V 及以下的低电量锚点。 / Preserve low anchors at/below 3.6 V.
#define BATT_RESTORE_TOL_PERMILLE 150    // 开机恢复容差。 / Boot-restore tolerance.
#define BATT_STATE_VERSION 1            // 新曲线不恢复旧的无版本百分比。 / Do not restore unversioned percentages onto the revised curve.
#define BATT_DONE_MIN_MV 4300            // 判满电压门槛，防状态位误判。 / Voltage gate for the done snap.
// 涓流判满：原厂在充电期间关掉终止，STATUS1 停在恒压码 2，top-off 码 3 不会出现，
// done 路径可能永不触发；高电压下持续近零电流即视为充满。
// Taper-full: the vendor keeps termination off while charging, so STATUS1 stays on the CV
// code 2 and the top-off code 3 never appears; the done path may never fire, so sustained
// near-zero current at a high voltage counts as full.
#define BATT_FULL_MV 4360                // 涓流判满的电压门槛。 / Voltage gate for the taper-full call.
#define BATT_FULL_MA 25                  // 高于该正向电流视为还在真充电。 / Above this positive current the pack is still really charging.
#define BATT_FULL_CONFIRM_MS 300000      // 持续近零电流多久判满。 / How long near-zero current must hold.
#define BATT_SAVE_INTERVAL_MS 300000     // NVS 周期保存间隔。 / Periodic NVS save cadence.
#define BATT_SAVE_CHANGE_PERMILLE 20     // 相差 2% 时提前保存，仍至少间隔一分钟。 / Save a 2% change early, at most once per minute.
#define BATT_FULL_MAX_GAP_US 5000000     // 未观测时段不算持续判满。 / Unobserved gaps do not count toward full confirmation.
#define BATT_LOG_INTERVAL_MS 60000       // 校准日志间隔。 / Calibration log cadence.
// 卡上校准日志：不连电脑也能积累日常放电曲线；串口与卡写同一行。
// Calibration log on the card: daily discharge curves accumulate without a host;
// serial and card carry the same line.
#define BATT_LOG_PATH "/sdcard/batt.log"

// 4.4 V 高压电池 OCV-SOC 锚点（mV → ‰），mV 降序。2026-10-25 卡上日志实测充满平台
// 4374–4385 mV（289 点、|I|≤15 mA，中位 4379、免载最高 4385）：原 4400 mV 锚点不可达，
// 曲线满电只算到 98%，再被涓流判满硬拉到 100%，形成 2% 跳变。顶锚点改为 4380 mV。
// 中段锚点 2026-10-27 用回充对账校准（batt_副本.log，849 行、34.5 h）：满电拔线深放
// 8.4 h 至 4075 mV 后回满，净充 346 mAh；另一次夜间放到 4241 mV 回充 ~93 mAh。两条
// 回充腿与放电库仑积分互相一致（容量取规格 2050 mAh）：4300→955、4200→875、4100→790。
// 原锚点为无数据推测，中段偏低 6–11%。2026-11-05 新日志的连续清醒段以 4100 mV / 790‰
// 为相对基准：至 4050/4000/3950 mV 分别放出 116379/195716/330144 µAh（2050 mAh 假定容量），
// 得到 733/695/629‰；这些是相对估算，不是绝对 SOC 实测。3950–3600 mV 暂用直线衔接，
// 不沿用无依据的 3900 mV 陡降；3600 mV 及以下保留原锚点，完整低段仍待整周期校准。
// OCV-SOC anchors for the 4.4 V LiHV pack (mV -> permille), descending in mV. The
// 2026-10-25 card log measured a full-charge plateau of 4374–4385 mV (289 samples,
// |I| ≤ 15 mA, median 4379, 4385 unloaded max): the old 4400 mV anchor was unreachable, so
// the curve topped out near 98% and the taper-full snap pulled it to 100% as a 2% jump.
// The top anchor is now 4380 mV. Mid anchors were recalibrated 2026-10-27 by refill
// accounting (batt_副本.log, 849 rows, 34.5 h): a full pack discharged 8.4 h to 4075 mV
// took 346 mAh net to recharge, and a second overnight leg to 4241 mV took ~93 mAh. Both
// refill legs agree with the discharge coulomb integral (capacity per spec, 2050 mAh):
// 4300→955, 4200→875, 4100→790. The old mid anchors were guesses and read 6–11% low.
// The continuous awake segment supplied on 2026-11-05 uses 4100 mV / 790 permille as
// a relative baseline: 116379/195716/330144 uAh to 4050/4000/3950 mV yields 733/695/629
// permille at the assumed 2050 mAh capacity, not measured absolute SOC. Provisionally
// interpolate 3950–3600 mV rather than introduce another unsupported cliff at 3900 mV.
// Anchors at/below 3600 mV remain unchanged; the low range still needs a full-cycle calibration.
static const int32_t s_soc_curve[][2] = {
    {4380, 1000}, {4300, 955}, {4200, 875}, {4100, 790}, {4050, 733},
    {4000, 695},  {3950, 629}, {3600, 140}, {3500, 80},
    {3400, 40},   {3300, 0},
};

// 滤波状态：跨 poll 保持，首采时初始化。 / Filter state held across polls, seeded by the first sample.
typedef struct {
    bool inited;
    bool charging;        // 上次显示方向，用于插拔后重新计时。 / Last display direction; restart timing on transitions.
    int32_t ema_q8;        // 补偿后电压 EMA，×256 定点。 / EMA of compensated OCV, fixed-point ×256.
    int32_t disp_permille; // 对外显示值。 / The value consumers see.
    int64_t last_poll_us;
    int64_t last_move_us;
} batt_filter_t;

// BQ 电流正充负放，不能用异步读取的 CX 状态决定补偿符号。
// BQ current is positive into the pack; asynchronous CX status must not choose its sign.
static int32_t batt_compensated_mv(uint16_t mv, bool have_i, int16_t ma) {
    int32_t comp = have_i ? (int32_t)ma * BATT_R_MOHM / 1000 : 0;
    if (comp > BATT_COMP_CAP_MV) comp = BATT_COMP_CAP_MV;
    if (comp < -BATT_COMP_CAP_MV) comp = -BATT_COMP_CAP_MV;
    return (int32_t)mv - comp;
}

// 查表线性插值。 / Linear interpolation over the anchor table.
static int32_t batt_curve_soc(int32_t mv) {
    const size_t n = sizeof(s_soc_curve) / sizeof(s_soc_curve[0]);
    if (mv >= s_soc_curve[0][0]) return s_soc_curve[0][1];
    for (size_t i = 1; i < n; i++) {
        if (mv >= s_soc_curve[i][0]) {
            const int32_t span = s_soc_curve[i - 1][0] - s_soc_curve[i][0];
            const int32_t rise = s_soc_curve[i - 1][1] - s_soc_curve[i][1];
            return s_soc_curve[i][1] + (mv - s_soc_curve[i][0]) * rise / span;
        }
    }
    return s_soc_curve[n - 1][1];
}

// 一步滤波：负载补偿 → EMA → 查表 → 死区/单向/限速；纯函数，便于主机对拍。
// One filter step: load compensation -> EMA -> table -> deadband/one-direction/rate limit;
// pure so host tests can replay it.
static void batt_filter_step(batt_filter_t* f, int64_t now_us, uint16_t raw_mv, bool have_i,
                             int16_t raw_ma, bool charging, bool charge_done, int32_t saved_permille,
                             int32_t* out_mv, int32_t* out_permille) {
    const int32_t ocv = batt_compensated_mv(raw_mv, have_i, raw_ma);

    // poll 间隔不固定（200/40/20/500/1500 ms 都有），按实际 dt 算系数。
    // Poll intervals vary (200/40/20/500/1500 ms); derive the factor from the real dt.
    int64_t dt_us = f->inited ? now_us - f->last_poll_us : 0;
    if (dt_us < 0) dt_us = 0;
    f->last_poll_us = now_us;

    if (!f->inited) {
        f->inited = true;
        f->charging = charging;
        f->ema_q8 = ocv * 256;
        const int32_t table = batt_curve_soc(ocv);
        // 首采恢复：保存值在表值容差内则沿用，深睡唤醒显示连续。
        // First-sample restore: keep the saved value inside tolerance, so deep-standby
        // wake keeps the display continuous.
        f->disp_permille = table;
        if (saved_permille >= 0 && saved_permille <= 1000 &&
            saved_permille - table <= BATT_RESTORE_TOL_PERMILLE &&
            table - saved_permille <= BATT_RESTORE_TOL_PERMILLE) {
            f->disp_permille = saved_permille;
        }
        f->last_move_us = now_us;
    } else {
        if (charging != f->charging || now_us < f->last_move_us) {
            f->charging = charging;
            f->last_move_us = now_us;
        }
        const int64_t tau_us = (int64_t)BATT_EMA_TAU_MS * 1000;
        f->ema_q8 += (int32_t)(((int64_t)ocv * 256 - f->ema_q8) * dt_us / (tau_us + dt_us));
    }

    // 充电完成即认为满电，直接对齐 100%；电压门槛挡住状态位误判。
    // Charge done means full: align to 100% directly; the voltage gate guards a misread status.
    if (charge_done && raw_mv >= BATT_DONE_MIN_MV) {
        f->disp_permille = 1000;
        f->last_move_us = now_us;
    } else {
        // 单向步进：放电只降不升，充电只升不降。
        // One-direction stepping: discharge only falls, charge only rises.
        int32_t target = batt_curve_soc(f->ema_q8 / 256);
        if (charging) {
            if (target < f->disp_permille) target = f->disp_permille;
        } else if (target > f->disp_permille) {
            target = f->disp_permille;
        }
        const int32_t delta = target - f->disp_permille;
        // 正常每分钟一格；低电量按原速度跟随，0% 不被死区卡在 1%。
        // Normal steps sit a minute apart; retain low-level responsiveness and let 0% clear the deadband.
        if (delta != 0) {
            const int32_t abs_delta = delta < 0 ? -delta : delta;
            const int32_t interval_ms = !charging && target <= BATT_LOW_PERMILLE
                ? BATT_LOW_STEP_INTERVAL_MS : BATT_MIN_STEP_INTERVAL_MS;
            if ((abs_delta >= BATT_DEADBAND_PERMILLE || target == 0) &&
                now_us - f->last_move_us >= (int64_t)interval_ms * 1000) {
                const int32_t step =
                    abs_delta > BATT_MAX_STEP_PERMILLE ? BATT_MAX_STEP_PERMILLE : abs_delta;
                f->disp_permille += delta < 0 ? -step : step;
                f->last_move_us = now_us;
            }
        }
    }

    *out_mv = f->ema_q8 / 256;
    *out_permille = f->disp_permille;
}

// 涓流判满的纯判定，便于主机对拍：CX done 位优先；否则充电中、高电压且正向电流
// 在近零范围持续满 BATT_FULL_CONFIRM_MS 才算满；明显放电必须重新确认。
// 状态经 *since_us 传入传出；采样长空白由 poll_battery 清窗。
// Pure taper-full decision for host replay: a real CX done bit wins; otherwise
// charging at a high voltage with positive current under the gate must hold for
// BATT_FULL_CONFIRM_MS. Material discharge restarts confirmation; state travels via
// *since_us, and poll_battery resets the window across long sampling gaps.
static bool batt_full_now(bool charge_done, bool charging, bool have_v, int32_t raw_mv,
                          bool have_i, int16_t raw_ma, int64_t now_us, int64_t *since_us) {
    if (charge_done) {
        *since_us = -1;
        return true;
    }
    if (!charging || !have_v || !have_i || raw_mv < BATT_FULL_MV ||
        raw_ma >= BATT_FULL_MA || raw_ma <= -BATT_FULL_MA) {
        *since_us = -1;
        return false;
    }
    if (*since_us < 0) *since_us = now_us;
    return now_us - *since_us >= (int64_t)BATT_FULL_CONFIRM_MS * 1000;
}

// BQ27220 内部库仑计 SOC，只读对比用。 / BQ27220 internal coulomb-counted SOC, read-only comparison.
static int bq_read_soc(void) {
    uint8_t pct = 0;
    if (dev_read(s_bq, BQ_REG_SOC, &pct, sizeof(pct)) != ESP_OK) return -1;
    return pct <= 100 ? pct : -1;
}

// 电量持久化到 NVS，供深睡唤醒后恢复显示。 / Persist the percent to NVS for deep-standby wake restore.
static int32_t s_batt_persisted_permille = -1;

static void batt_save(int32_t permille) {
    if (permille < 0 || permille > 1000 || permille == s_batt_persisted_permille) return;
    nvs_handle_t nvs;
    if (nvs_open("pmu", NVS_READWRITE, &nvs) != ESP_OK) return;
    if (nvs_set_i32(nvs, "soc", permille) == ESP_OK &&
        nvs_set_u32(nvs, "soc_ver", BATT_STATE_VERSION) == ESP_OK && nvs_commit(nvs) == ESP_OK) {
        s_batt_persisted_permille = permille;
    }
    nvs_close(nvs);
}

static int32_t batt_load(void) {
    nvs_handle_t nvs;
    int32_t value = -1;
    uint32_t version = 0;
    if (nvs_open("pmu", NVS_READONLY, &nvs) != ESP_OK) return -1;
    if (nvs_get_u32(nvs, "soc_ver", &version) != ESP_OK || version != BATT_STATE_VERSION ||
        nvs_get_i32(nvs, "soc", &value) != ESP_OK || value < 0 || value > 1000) value = -1;
    nvs_close(nvs);
    return value;
}

static void batt_save_current(void) {
    if (s_snap.flags & PMU_STATUS_BATTERY_VALID) batt_save(s_snap.soc_permille);
}

/* ---- 校准日志 / Calibration log ---- */

// 快照、事件与队列只在短临界区处理；文件 I/O 独占另一把互斥锁。
// Snapshots, events and queues use short critical sections; file I/O owns a separate mutex.
static batt_log_state_t s_batt_log;
static portMUX_TYPE s_batt_log_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_batt_io_storage;
static SemaphoreHandle_t s_batt_io_mutex;
static bool s_batt_io_available;
static bool s_batt_log_busy;
static TaskHandle_t s_batt_log_task;
static uint32_t s_batt_io_errors, s_batt_file_missed;
static uint64_t s_batt_boot_id;
static char s_batt_metadata[768];
static int8_t s_batt_ic_soc = -1;
static int64_t s_batt_ic_us = -1;

static int64_t batt_log_wall(void) {
    const time_t wall = time(NULL);
    return wall >= 1704067200 ? (int64_t)wall : -1;
}

static bool batt_log_enabled(void) {
    portENTER_CRITICAL(&s_batt_log_mux);
    const bool enabled = s_batt_log.enabled;
    portEXIT_CRITICAL(&s_batt_log_mux);
    return enabled;
}

static void batt_log_notify(void) {
    if (s_batt_log_task != NULL) xTaskNotifyGive(s_batt_log_task);
}

static SemaphoreHandle_t batt_log_io_mutex(void) {
    portENTER_CRITICAL(&s_batt_log_mux);
    if (s_batt_io_mutex == NULL) s_batt_io_mutex = xSemaphoreCreateMutexStatic(&s_batt_io_storage);
    portEXIT_CRITICAL(&s_batt_log_mux);
    return s_batt_io_mutex;
}

bool device_power_batt_log_storage(bool available, uint32_t timeout_ms) {
    SemaphoreHandle_t mutex = batt_log_io_mutex();
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    s_batt_io_available = available;
    xSemaphoreGive(mutex);
    return true;
}

void device_power_set_batt_log(bool enabled) {
    const int64_t now = esp_timer_get_time();
    const int64_t wall = batt_log_wall();
    portENTER_CRITICAL(&s_batt_log_mux);
    if (enabled && !s_batt_log.enabled) {
        s_batt_ic_soc = -1;
        s_batt_ic_us = -1;
    }
    batt_log_set_enabled(&s_batt_log, enabled, now, wall);
    portEXIT_CRITICAL(&s_batt_log_mux);
    batt_log_notify();
}

void device_power_batt_log_event(device_power_batt_log_event_t event) {
    const int64_t now = esp_timer_get_time();
    const int64_t wall = batt_log_wall();
    portENTER_CRITICAL(&s_batt_log_mux);
    batt_log_note(&s_batt_log, (uint32_t)event, now, wall);
    const bool pending = s_batt_log.count != 0;
    portEXIT_CRITICAL(&s_batt_log_mux);
    if (pending) batt_log_notify();
}

bool device_power_batt_log_flush(uint32_t timeout_ms) {
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    batt_log_notify();
    for (;;) {
        portENTER_CRITICAL(&s_batt_log_mux);
        const bool drained = s_batt_log.count == 0 && !s_batt_log_busy;
        portEXIT_CRITICAL(&s_batt_log_mux);
        if (drained) return true;
        if (s_batt_log_task == NULL || esp_timer_get_time() >= deadline) return false;
        vTaskDelay(1);
    }
}

// 只被事件或新到期采样唤醒；不为日志增加睡眠唤醒定时器。
// Wake only for events or newly due samples; logging adds no sleep wakeup timer.
static void batt_log_task(void *arg) {
    (void)arg;
    batt_log_file_t file = {.header = true};
    uint32_t serial_session = 0;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            batt_log_record_t record;
            portENTER_CRITICAL(&s_batt_log_mux);
            const bool have = batt_log_pop(&s_batt_log, &record);
            s_batt_log_busy = have;
            const uint32_t errors = s_batt_io_errors, missed = s_batt_file_missed;
            portEXIT_CRITICAL(&s_batt_log_mux);
            if (!have) break;
            char line[BATT_LOG_LINE_SIZE];
            const int n = batt_log_format(line, sizeof(line), &record, s_batt_boot_id,
                                          esp_timer_get_time(), errors, missed);
            bool ok = n > 0;
            bool attempted = false;
            if (ok) {
                if (serial_session != record.session) {
                    ESP_LOGI(TAG, "batt-meta: fmt=2 session=%u %s", (unsigned)record.session, s_batt_metadata);
                    serial_session = record.session;
                }
                ESP_LOGI(TAG, "batt: %.*s", n - 1, line);
                SemaphoreHandle_t mutex = batt_log_io_mutex();
                (void)xSemaphoreTake(mutex, portMAX_DELAY);
                attempted = s_batt_io_available;
                ok = attempted && batt_log_append(&file, BATT_LOG_PATH, BATT_LOG_PATH ".1",
                    BATT_LOG_FILE_LIMIT, record.session, s_batt_metadata, line);
                xSemaphoreGive(mutex);
            }
            portENTER_CRITICAL(&s_batt_log_mux);
            if (!ok) {
                ++s_batt_file_missed;
                if (attempted || n <= 0) ++s_batt_io_errors;
            }
            s_batt_log_busy = false;
            portEXIT_CRITICAL(&s_batt_log_mux);
            if (!ok) file.header = true;
            if (!ok && attempted) ESP_LOGW(TAG, "battery log write/sync failed");
        }
    }
}

static void batt_log_metadata(void) {
    if (!s_batt_boot_id) s_batt_boot_id = ((uint64_t)esp_random() << 32) | esp_random();
    const esp_app_desc_t *app = esp_app_get_description();
    char sha[65];
    char built[40];
    snprintf(built, sizeof(built), "%.15sT%.15s", app->date, app->time);
    for (char *p = built; *p; ++p) if (*p == ' ') *p = '_';
    for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i)
        snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    // 算法参数和曲线散列辅助对照源码；同版本的本地构建由 ELF 散列区分。
    // Parameters and a curve hash identify the source algorithm; ELF hashes distinguish local builds of one version.
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(s_soc_curve) / sizeof(s_soc_curve[0]); ++i)
        for (unsigned j = 0; j < 2; ++j) hash = (hash ^ (uint32_t)s_soc_curve[i][j]) * 16777619u;
    snprintf(s_batt_metadata, sizeof(s_batt_metadata),
        "fw=%.31s elf=%s built=%s boot=%016llx reset=%d curve_hash=%08lx "
        "r_mohm=%d comp_cap_mv=%d ema_ms=%d deadband=%d step=%d step_ms=%d "
        "full_mv=%d full_ma=%d full_ms=%d max_dt_ms=%lld q=sampled_estimate "
        "units=mv,ma,permille,ms,uah valid_bits=v1,i2,cx4,ic8",
        app->version, sha, built, (unsigned long long)s_batt_boot_id,
        (int)esp_reset_reason(), (unsigned long)hash, BATT_R_MOHM, BATT_COMP_CAP_MV,
        BATT_EMA_TAU_MS, BATT_DEADBAND_PERMILLE, BATT_MAX_STEP_PERMILLE,
        BATT_MIN_STEP_INTERVAL_MS, BATT_FULL_MV, BATT_FULL_MA, BATT_FULL_CONFIRM_MS,
        (long long)(BATT_LOG_MAX_INTERVAL_US / 1000));
}

static batt_filter_t s_batt_filter;
// NVS 恢复值，只在首采时被消费一次。 / NVS restore value, consumed once by the first sample.
static int32_t s_batt_saved_permille = -1;
static bool s_batt_charging_last;
static int64_t s_batt_last_save_us;
// 涓流判满的确认窗口起点（µs，-1 表示未在窗口内）。 / Start of the taper-full
// confirmation window (µs; -1 when outside the window).
static int64_t s_batt_full_since_us = -1;
static int64_t s_batt_poll_us;
static bool s_batt_poll_reusable;

static esp_err_t rtc_init(void) {
    uint8_t ctrl[2] = {0};
    esp_err_t err = dev_read(s_rtc, 0x00, ctrl, sizeof(ctrl));
    const uint8_t run = 0;
    if (err == ESP_OK) err = dev_write(s_rtc, 0x00, &run, 1);
    // 清原厂支持的 alarm/timer 锁存，避免遗留中断占住共享线路。
    // Clear vendor-supported alarm/timer latches so inherited flags do not hold the shared line.
    if (err == ESP_OK) {
        ctrl[1] &= (uint8_t)~0x0C;
        err = dev_write(s_rtc, 0x01, &ctrl[1], 1);
    }
    return err;
}

static uint32_t rtc_unix_utc(const struct tm* tm) {
    uint32_t days = 0;
    const int year = tm->tm_year + 1900;
    for (int y = 1970; y < year; ++y) {
        days += 365 + ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
    }
    static const uint16_t before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    days += before_month[tm->tm_mon] + (uint32_t)tm->tm_mday - 1;
    if (tm->tm_mon > 1 && year % 4 == 0) ++days;
    return ((days * 24 + (uint32_t)tm->tm_hour) * 60 + (uint32_t)tm->tm_min) * 60 + (uint32_t)tm->tm_sec;
}

static esp_err_t rtc_read(struct tm* out, bool* valid) {
    uint8_t r[7] = {0};
    esp_err_t err = dev_read(s_rtc, PCF_REG_SECONDS, r, sizeof(r));
    if (err != ESP_OK) return err;
    *valid = (r[0] & PCF_BIT_VL) == 0;
    static const uint8_t masks[] = {0x7F,0x7F,0x3F,0x3F,0x07,0x1F,0xFF};
    for (unsigned i = 0; i < sizeof(r); ++i) {
        const uint8_t value = r[i] & masks[i];
        if ((value & 0x0F) > 9 || (value >> 4) > 9) *valid = false;
    }
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd_to_dec(r[0] & 0x7F);
    out->tm_min = bcd_to_dec(r[1] & 0x7F);
    out->tm_hour = bcd_to_dec(r[2] & 0x3F);
    out->tm_mday = bcd_to_dec(r[3] & 0x3F);
    out->tm_wday = bcd_to_dec(r[4] & 0x07);
    out->tm_mon = bcd_to_dec(r[5] & 0x1F) - 1;
    out->tm_year = 100 + bcd_to_dec(r[6]);
    out->tm_isdst = 0;
    if (out->tm_mon < 0 || out->tm_mon > 11 || out->tm_mday < 1 || out->tm_mday > 31 ||
        out->tm_hour > 23 || out->tm_min > 59 || out->tm_sec > 59) {
        *valid = false;
    }
    if (*valid) {
        static const uint8_t month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        const int max_day = month_days[out->tm_mon] +
                            (out->tm_mon == 1 && (out->tm_year + 1900) % 4 == 0);
        if (out->tm_mday > max_day || out->tm_wday > 6) *valid = false;
    }
    return ESP_OK;
}

static esp_err_t rtc_write(time_t now) {
    struct tm tm;
    if (gmtime_r(&now, &tm) == NULL) return ESP_ERR_INVALID_ARG;
    const int year = tm.tm_year + 1900;
    if (year < 2000 || year > 2099) return ESP_ERR_INVALID_ARG;
    uint8_t r[7] = {
        dec_to_bcd((uint8_t)tm.tm_sec),
        dec_to_bcd((uint8_t)tm.tm_min),
        dec_to_bcd((uint8_t)tm.tm_hour),
        dec_to_bcd((uint8_t)tm.tm_mday),
        dec_to_bcd((uint8_t)tm.tm_wday),
        dec_to_bcd((uint8_t)(tm.tm_mon + 1)),
        dec_to_bcd((uint8_t)(year - 2000)),
    };
    return dev_write(s_rtc, PCF_REG_SECONDS, r, sizeof(r));
}

static void poll_key(void) {
    fca9555_handle_t ioe = board_fca9555();
    if (ioe == NULL) return;
    uint8_t p1 = 0;
    if (fca9555_read_input(ioe, 1, &p1) != ESP_OK) return;

    const bool down = (p1 & KEY_POWER_MASK) == 0;
    const int64_t now = esp_timer_get_time();
    if (down && !s_key_down) {
        // 同一轮询内复查：毛刺挺不过 20 ms，真实按住两次都为低；读失败按未按下处理。
        // / Recheck inside the same poll: a glitch does not outlast 20 ms while a real hold stays
        // low; a failed read counts as not pressed.
        vTaskDelay(pdMS_TO_TICKS(KEY_CONFIRM_DELAY_MS));
        uint8_t again = 0;
        if (fca9555_read_input(ioe, 1, &again) != ESP_OK) return;
        if ((again & KEY_POWER_MASK) != 0) return;
        s_key_down = true;
        s_key_down_us = now;
        s_key_long_fired = false;
    } else if (down && s_key_down) {
        if (!s_key_long_fired && now - s_key_down_us >= (int64_t)KEY_LONG_MS * 1000) {
            s_key_long_fired = true;
            s_key_long_pending = true;
        }
    } else if (!down && s_key_down) {
        s_key_down = false;
        const int64_t held_us = now - s_key_down_us;
        if (!s_key_long_fired && held_us >= (int64_t)KEY_DEBOUNCE_MS * 1000) {
            s_key_short_pending = true;
        }
    }
}

// 仅快采按键，不读电量、不访问文件或绘屏；停用后阻塞等待恢复通知。
// Sample keys only: no battery, file or display work; block for a resume notification when disabled.
static void key_sampler_task(void *arg) {
    (void)arg;
    for (;;) {
        key_lock();
        const bool active = s_key_sampling;
        if (active) poll_key();
        key_unlock();
        if (active) vTaskDelay(pdMS_TO_TICKS(KEY_SAMPLE_PERIOD_MS));
        else (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

bool device_power_key_sampler_start(void) {
    if (!s_ready) return false;
    key_lock();
    if (s_key_task == NULL &&
        xTaskCreatePinnedToCore(key_sampler_task, "power_key", 3072, NULL, 4, &s_key_task, 1) != pdPASS) {
        s_key_task = NULL;
        key_unlock();
        ESP_LOGW(TAG, "power-key sampler unavailable; keep direct polling");
        return false;
    }
    key_unlock();
    return true;
}

bool device_power_key_sampler_enable(bool enabled) {
    key_lock();
    const bool previous = s_key_sampling;
    s_key_sampling = enabled && s_ready && s_key_task != NULL;
    // 交还锁屏或独占方时丢弃旧动作，按住状态的释放也不带到下一次启用。
    // Drop stale actions on handoff to lock/exclusive use, including the release of a held press.
    if (previous && !s_key_sampling) {
        s_key_short_pending = s_key_long_pending = false;
        if (s_key_down) s_key_long_fired = true;
        s_snap.pending_events = 0;
    }
    if (s_key_sampling && !previous) xTaskNotifyGive(s_key_task);
    key_unlock();
    return previous;
}

static void poll_battery(void) {
    const int64_t now = esp_timer_get_time();
    s_batt_poll_reusable = false;

    uint16_t mv = 0;
    int16_t ma = 0;
    const bool have_v = s_bq != NULL && bq_read_voltage(&mv) == ESP_OK && mv >= 2500 && mv <= 5000;
    const bool have_i = s_bq != NULL && bq_read_current(&ma) == ESP_OK;

    // 充电检测照旧：CX STATUS1 优先，电流作兜底。
    // Charger detection unchanged: CX STATUS1 first, current as the fallback.
    uint8_t chg_stat = 0, charger_status = 0;
    bool have_chg = false;
    if (s_cx != NULL) {
        uint8_t st = 0;
        if (dev_read(s_cx, CX_REG_STATUS1, &st, 1) == ESP_OK) {
            charger_status = st;
            chg_stat = (uint8_t)((st >> 3) & 0x03);
            have_chg = true;
        }
    }
    const bool logging = batt_log_enabled();
    const int64_t sampled = logging ? esp_timer_get_time() : 0;
    const int64_t sample_wall = logging ? batt_log_wall() : -1;
    cx_charger_poll(now, have_v, mv, have_i, ma, have_chg, charger_status);
    bool charging;
    if (have_chg) {
        charging = chg_stat != 0;
    } else if (have_i) {
        charging = ma > 20;
    } else {
        charging = false;
    }
    const bool charge_done_cx = have_chg && chg_stat == CX_CHG_STAT_TOPOFF;
    // 涓流判满兜底：CX 在零电流下也不报 done（见 BATT_FULL_* 注释），高电压持续
    // 近零电流同样进入滤波的判满对齐。
    // Taper-full fallback: the CX never reports done even at zero current (see
    // the BATT_FULL_* notes); sustained near-zero current at a high voltage also
    // enters the filter's charge-done alignment.
    if (now < s_batt_poll_us || now - s_batt_poll_us > BATT_FULL_MAX_GAP_US)
        s_batt_full_since_us = -1;
    const bool charge_done = batt_full_now(charge_done_cx, charging, have_v, (int32_t)mv,
                                           have_i, ma, now, &s_batt_full_since_us);
    s_snap.charge_state = charging ? PMU_CHARGE_CHARGING : PMU_CHARGE_NOT_CHARGING;
    if (charging) {
        s_snap.flags |= PMU_STATUS_CHARGING_ACTIVE;
    } else {
        s_snap.flags &= ~PMU_STATUS_CHARGING_ACTIVE;
    }

    if (have_v) {
        int32_t ocv_mv = (int32_t)mv;
        int32_t disp = 0;
        batt_filter_step(&s_batt_filter, now, mv, have_i, ma, charging, charge_done,
                         s_batt_saved_permille, &ocv_mv, &disp);
        s_batt_saved_permille = -1;  // 恢复值只喂首采。 / The restore value feeds the first sample only.
        s_snap.battery_mv = (uint16_t)ocv_mv;
        s_snap.battery_raw = mv;
        s_snap.soc_permille = (uint16_t)disp;
        s_snap.flags |= PMU_STATUS_BATTERY_VALID;
        s_snap.status_ok = true;

        // 保存：周期、充电状态切换及累计变化；不逐轮写 NVS。
        // Save periodically, on charger transitions or accumulated movement, never on every poll.
        const bool charging_changed = charging != s_batt_charging_last;
        s_batt_charging_last = charging;
        const int32_t saved_delta = disp - s_batt_persisted_permille;
        const bool moved = s_batt_persisted_permille < 0 ||
            saved_delta >= BATT_SAVE_CHANGE_PERMILLE || saved_delta <= -BATT_SAVE_CHANGE_PERMILLE;
        if (charging_changed ||
            (moved && now - s_batt_last_save_us >= (int64_t)BATT_MIN_STEP_INTERVAL_MS * 1000) ||
            now - s_batt_last_save_us >= (int64_t)BATT_SAVE_INTERVAL_MS * 1000) {
            s_batt_last_save_us = now;
            batt_save(disp);
        }
    } else {
        s_snap.flags &= ~PMU_STATUS_BATTERY_VALID;
    }

    if (logging) {
        if (s_batt_ic_us < 0 || sampled - s_batt_ic_us >= BATT_LOG_PERIOD_US) {
            s_batt_ic_soc = (int8_t)bq_read_soc();
            s_batt_ic_us = esp_timer_get_time();
        }
        const batt_log_sample_t sample = {
            .up_us = sampled, .wall_sec = sample_wall, .read_us = (uint32_t)(sampled - now),
            .have_v = have_v, .have_i = have_i, .have_chg = have_chg,
            .raw_mv = mv, .ma = ma, .charging = charging, .charge_done = charge_done,
            .done_source = charge_done ? (charge_done_cx ? 1 : 2) : 0,
            .charger_status = charger_status,
            .comp_mv = batt_compensated_mv(mv, have_i, ma),
            .ocv_mv = have_v ? s_snap.battery_mv : -1,
            .curve_permille = have_v ? batt_curve_soc(s_snap.battery_mv) : -1,
            .disp_permille = have_v ? s_snap.soc_permille : -1,
            .ic_soc = s_batt_ic_soc, .ic_us = s_batt_ic_us,
        };
        portENTER_CRITICAL(&s_batt_log_mux);
        batt_log_observe(&s_batt_log, &sample);
        const bool pending = s_batt_log.count != 0;
        portEXIT_CRITICAL(&s_batt_log_mux);
        if (pending) batt_log_notify();
    }
    s_batt_poll_us = now;
    s_batt_poll_reusable = have_v && have_i && (s_cx == NULL || have_chg);
}

// 只合并浅睡检查中紧邻的成功采样；到期维护、失败或时钟回退必须重读。
// Coalesce only adjacent successful sleep-check samples; due maintenance, failures and clock rollback force a read.
static bool wake_battery_snapshot_reusable(int64_t now) {
    if (!s_batt_poll_reusable || now < s_batt_poll_us ||
        now - s_batt_poll_us >= KEY_WAKE_BATT_REUSE_US) return false;
    if (s_charger_configured &&
        ((s_charger_resume_us != 0 && now >= s_charger_resume_us) ||
         (s_charger_resume_us == 0 && now >= s_charger_next_us))) return false;
    return true;
}

esp_err_t device_power_init(i2c_master_bus_handle_t bus_handle,
                             const device_power_config_t* config) {
    (void)config;
    if (bus_handle == NULL) return ESP_ERR_INVALID_ARG;
    if (s_ready) return ESP_OK;
    s_batt_poll_reusable = false;
    s_bus = bus_handle;
    memset(&s_snap, 0, sizeof(s_snap));

    (void)add_dev(BQ27220_ADDR, &s_bq);
    (void)add_dev(CX25601N_ADDR, &s_cx);
    (void)add_dev(PCF8563_ADDR, &s_rtc);

    // 充电芯片按原厂配置写一次（1 A 请求、4.4 V 截止）；失败不阻塞 PMU，并尽力停充。
    // Configure the charger once with vendor defaults (1 A requested, 4.4 V regulation);
    // a failure never blocks the PMU and attempts to disable incomplete charging.
    s_charger_next_us = s_charger_resume_us = 0;
    s_charger_uv_shutdown = false;
    s_charger_configured = s_cx != NULL && cx_charger_setup() == ESP_OK;
    const bool rtc_ready = s_rtc != NULL && rtc_init() == ESP_OK;
    if (s_cx != NULL && !s_charger_configured) {
        ESP_LOGW(TAG, "CX25601N setup incomplete; charge disable attempted");
    }

    s_snap.present = s_bq != NULL || s_cx != NULL || s_rtc != NULL;
    s_snap.identity_ok = s_snap.present;
    s_snap.config_ok = (s_cx == NULL || s_charger_configured) && (s_rtc == NULL || rtc_ready);
    s_snap.time_ok = rtc_ready;
    s_snap.alarm_ok = false;
    s_snap.power_state = PMU_PWR_RUNNING;
    s_snap.config.key_long_ms = KEY_LONG_MS;
    s_snap.config.key_debounce_ms = KEY_DEBOUNCE_MS;

    nvs_handle_t nvs;
    if (nvs_open("panel", NVS_READONLY, &nvs) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(nvs, "vcom", &v) == ESP_OK && v >= 500 && v <= 2500) {
            s_vcom_mv = v;
        }
        nvs_close(nvs);
    }
    // 上次电量恢复值：深睡唤醒（复位重启）后显示连续。
    // Last-percent restore: keeps the display continuous after a deep-standby wake (reset boot).
    s_batt_saved_permille = batt_load();
    s_batt_persisted_permille = s_batt_saved_permille;

    s_ready = true;
    // 校准日志任务：起不来只影响串口/卡日志，不影响电量功能。
    // Calibration logger task: failing to start only costs the serial/card log.
    batt_log_metadata();
    if (s_batt_log_task == NULL &&
        xTaskCreate(batt_log_task, "battlog", 6144, NULL, 1, &s_batt_log_task) != pdPASS) {
        ESP_LOGW(TAG, "battery log task not started");
    }
    batt_log_notify();
    ESP_LOGI(TAG, "Metalio PMU shim ready (BQ=%d CX=%d RTC=%d VCOM=%dmV)", s_bq != NULL,
             s_cx != NULL, s_rtc != NULL, s_vcom_mv);
    return ESP_OK;
}

esp_err_t device_power_deinit(void) {
    // 句柄释放前收齐采样；任务留在通知等待中，后续初始化可复用。
    // Join sampling before releasing handles; retain the notification-blocked task for reinitialization.
    (void)device_power_key_sampler_enable(false);
    const esp_err_t parked = cx_charger_park();
    if (parked != ESP_OK) return parked;
    s_charger_configured = false;
    s_charger_next_us = s_charger_resume_us = 0;
    device_power_set_batt_log(false);
    if (!device_power_batt_log_flush(2000)) return ESP_ERR_TIMEOUT;
    // 保留等待通知的任务，避免异步删除打断文件关闭；后续初始化复用。
    // Retain the notification-blocked task so asynchronous deletion cannot interrupt close; reinitialize by reusing it.
    if (s_bq) i2c_master_bus_rm_device(s_bq);
    if (s_cx) i2c_master_bus_rm_device(s_cx);
    if (s_rtc) i2c_master_bus_rm_device(s_rtc);
    s_bq = s_cx = s_rtc = NULL;
    s_ready = false;
    return ESP_OK;
}

bool device_power_ready(void) { return s_ready; }
const pmu_snapshot_t* device_power_get(void) { return &s_snap; }

esp_err_t device_power_poll(void) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    key_lock();
    poll_key();
    s_snap.pending_events = (uint8_t)(s_key_short_pending || s_key_long_pending);
    key_unlock();
    poll_battery();
    return ESP_OK;
}

esp_err_t device_power_refresh(void) { return device_power_poll(); }

esp_err_t device_power_cmd(uint16_t code, const uint8_t* payload, uint8_t plen) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (code == PMU_CMD_TIME_GET) {
        struct tm tm;
        bool valid = false;
        esp_err_t err = rtc_read(&tm, &valid);
        if (err != ESP_OK) return err;
        if (!valid) { s_snap.time_ok = false; s_snap.time_synced = 0; return ESP_ERR_INVALID_STATE; }
        const time_t t = (time_t)rtc_unix_utc(&tm);
        s_snap.unix_sec = (uint32_t)t;
        s_snap.unix_millis = 0;
        s_snap.time_synced = valid ? 1 : 0;
        s_snap.time_ok = valid;
        if (valid) {
            struct timeval tv = {.tv_sec = t, .tv_usec = 0};
            settimeofday(&tv, NULL);
        }
        return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (code == PMU_CMD_TIME_SYNC) {
        if (payload == NULL || plen < 4) return ESP_ERR_INVALID_ARG;
        const uint32_t sec = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                             ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
        return rtc_write((time_t)sec);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_power_vcom_get(int* out_mv) {
    if (out_mv == NULL) return ESP_ERR_INVALID_ARG;
    *out_mv = s_vcom_mv;
    return ESP_OK;
}

esp_err_t device_power_vcom_set(int mv) {
    if (mv < 500 || mv > 2500 || (mv % 10) != 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("panel", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(nvs, "vcom", mv);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) s_vcom_mv = mv;
    return err;
}

esp_err_t device_power_uid_get(uint8_t out[PMU_CHIP_UID_LEN]) {
    (void)out;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t device_power_event_ack(uint16_t event_id) {
    (void)event_id;
    return ESP_OK;
}

esp_err_t device_power_action(uint8_t action, uint16_t delay_ms, uint16_t reason) {
    (void)delay_ms;
    (void)reason;
    switch (action) {
        case PMU_ACTION_HOST_NORMAL_RESTART:
        case PMU_ACTION_HOST_HARD_RESET:
            batt_save_current();
            device_power_batt_log_event(DEVICE_POWER_BATT_LOG_RESTART);
            (void)device_power_batt_log_flush(250);
            esp_restart();
            return ESP_OK;
        case PMU_ACTION_HOST_LOGICAL_OFF:
            return device_power_power_off();
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

void device_power_drain_events(void) {
    key_lock();
    s_key_short_pending = false;
    s_key_long_pending = false;
    s_snap.pending_events = 0;
    key_unlock();
}

device_power_key_action_t device_power_take_key_action(void) {
    (void)device_power_poll();
    key_lock();
    device_power_key_action_t action = DEVICE_POWER_KEY_NONE;
    if (s_key_long_pending) {
        action = DEVICE_POWER_KEY_LONG;
    } else if (s_key_short_pending) {
        action = DEVICE_POWER_KEY_SHORT;
    }
    s_key_short_pending = false;
    s_key_long_pending = false;
    s_snap.pending_events = 0;
    key_unlock();
    return action;
}

bool device_power_take_key_short(void) {
    (void)device_power_poll();
    key_lock();
    const bool got = s_key_short_pending;
    s_key_short_pending = false;
    s_snap.pending_events = 0;
    key_unlock();
    return got;
}

bool device_power_take_key_wakeup(void) {
    if (s_ready) {
        key_lock();
        poll_key();
        s_snap.pending_events = (uint8_t)(s_key_short_pending || s_key_long_pending);
        key_unlock();
        // 按键每次都读，只有电量允许短暂复用；活动页的完整轮询不变。
        // Always read the key; only battery samples may be briefly reused, leaving active-page polls unchanged.
        if (!wake_battery_snapshot_reusable(esp_timer_get_time())) poll_battery();
    }
    key_lock();
    const bool got = s_key_short_pending || s_key_down;
    s_key_short_pending = false;
    s_snap.pending_events = 0;
    key_unlock();
    return got;
}

void device_power_consume_key_press(void) {
    (void)device_power_poll();
    key_lock();
    s_key_short_pending = false;
    s_key_long_pending = false;
    // 键仍按着时连释放一起吞掉：标记长按已触发，松手不再生成短按。
    // Swallow the release while the key is still down: mark the hold fired so it yields no short press.
    if (s_key_down) s_key_long_fired = true;
    s_snap.pending_events = 0;
    key_unlock();
}

esp_err_t device_power_report_sleep(void) {
    // 无 PMU EN 可拉；软睡由 main/power/sleep.c 的 esp_light_sleep 路径负责。
    // No PMU EN to drop; main/power/sleep.c handles light sleep directly.
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_power_report_ready(void) {
    s_snap.power_state = PMU_PWR_RUNNING;
    return ESP_OK;
}

esp_err_t device_power_power_off(void) {
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_OFF);
    (void)device_power_batt_log_flush(250);
    const esp_err_t parked = cx_charger_park();
    if (parked != ESP_OK) return parked;
    batt_save_current();
    // 每轮 train 打 PWR_PULSE_COUNT 次；PMU 不掉电就按原厂反复打，打满才进深睡兜底。
    // Each train fires PWR_PULSE_COUNT pulses; repeat factory-style while the host lives on
    // and fall back to ESP deep sleep only after the trains are exhausted.
    for (unsigned train = 0; train < PWR_OFF_TRAIN_LIMIT; ++train) {
        for (int i = 0; i < PWR_PULSE_COUNT; i++) {
            esp_err_t err = board_pwr_pulse(true);
            if (err != ESP_OK) return err;
            vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
            err = board_pwr_pulse(false);
            if (err != ESP_OK) return err;
            vTaskDelay(pdMS_TO_TICKS(PWR_PULSE_INTERVAL_MS));
        }
        ESP_LOGW(TAG, "power-off train %u done, host still alive", train + 1);
    }
    ESP_LOGW(TAG, "power-off pulses exhausted; entering deep sleep");
    esp_deep_sleep_start();
    return ESP_OK;
}

/* 名称辅助沿用原 CW32 实现，页面与日志格式不变。 */
/* Keep the original CW32 name helpers so page and log formats stay unchanged. */

const char* pmu_power_name(uint8_t state) {
    switch (state) {
        case PMU_PWR_OFF: return "OFF";
        case PMU_PWR_POWERING_ON: return "PWR_ON";
        case PMU_PWR_BOOT_WAIT: return "BOOT_WAIT";
        case PMU_PWR_RUNNING: return "RUNNING";
        case PMU_PWR_SHUTDOWN_PENDING: return "SHUT_PEND";
        case PMU_PWR_RESETTING: return "RESET";
        case PMU_PWR_DOWNLOAD_MODE: return "DOWNLOAD";
        case PMU_PWR_FAULT: return "FAULT";
        case PMU_PWR_SOFT_SLEEP: return "SOFT_SLEEP";
        default: return "?";
    }
}

const char* pmu_charge_name(uint8_t state) {
    switch (state) {
        case PMU_CHARGE_UNKNOWN: return "UNK";
        case PMU_CHARGE_NOT_CHARGING: return "IDLE";
        case PMU_CHARGE_CHARGING: return "CHG";
        case PMU_CHARGE_FULL_INFERRED: return "FULL";
        case PMU_CHARGE_FAULT: return "FAULT";
        default: return "?";
    }
}

const char* pmu_event_name(uint8_t type) {
    switch (type) {
        case PMU_EVT_KEY_DOWN: return "KEY_DOWN";
        case PMU_EVT_KEY_UP: return "KEY_UP";
        case PMU_EVT_KEY_SHORT: return "KEY_SHORT";
        case PMU_EVT_KEY_LONG: return "KEY_LONG";
        case PMU_EVT_KEY_FORCE_OFF: return "KEY_FORCE";
        case PMU_EVT_CHARGE_STATE_CHANGED: return "CHARGE";
        case PMU_EVT_BATTERY_SAMPLE_READY: return "BAT_RDY";
        case PMU_EVT_BATTERY_LOW: return "BAT_LOW";
        case PMU_EVT_BATTERY_CRITICAL: return "BAT_CRIT";
        case PMU_EVT_BATTERY_SOC_LOW: return "SOC_LOW";
        case PMU_EVT_ALARM_FIRED: return "ALARM";
        case PMU_EVT_HOST_STARTED: return "HOST_START";
        case PMU_EVT_HOST_READY: return "HOST_RDY";
        case PMU_EVT_SHUTDOWN_REQUESTED: return "SHUT_REQ";
        case PMU_EVT_HOST_RESET_PERFORMED: return "HOST_RST";
        case PMU_EVT_COMMAND_COMPLETED: return "CMD_DONE";
        case PMU_EVT_CONFIG_RECOVERED: return "CFG_RCV";
        case PMU_EVT_CW_RESET: return "CW_RST";
        case PMU_EVT_OVERFLOW: return "OVERFLOW";
        default: return "EVT";
    }
}

const char* pmu_status_name(uint16_t status) {
    switch (status) {
        case PMU_STATUS_OK: return "OK";
        case PMU_STATUS_ACCEPTED: return "ACCEPTED";
        case PMU_STATUS_BUSY: return "BUSY";
        case PMU_STATUS_BAD_CRC: return "BAD_CRC";
        case PMU_STATUS_BAD_MAGIC: return "BAD_MAGIC";
        case PMU_STATUS_BAD_LENGTH: return "BAD_LEN";
        case PMU_STATUS_UNSUPPORTED_VERSION: return "BAD_VER";
        case PMU_STATUS_UNKNOWN_COMMAND: return "UNK_CMD";
        case PMU_STATUS_INVALID_ARGUMENT: return "BAD_ARG";
        case PMU_STATUS_SEQUENCE_CONFLICT: return "SEQ";
        case PMU_STATUS_STALE_SESSION: return "STALE";
        case PMU_STATUS_NOT_SUPPORTED: return "NOSUP";
        case PMU_STATUS_INVALID_STATE: return "BAD_ST";
        case PMU_STATUS_NOT_ARMED: return "NOARM";
        case PMU_STATUS_TOKEN_EXPIRED: return "EXPIRED";
        case PMU_STATUS_PERMISSION_DENIED: return "DENIED";
        default: return "ERR";
    }
}

const char* pmu_led_name(uint8_t color) {
    switch (color) {
        case PMU_LED_OFF: return "OFF";
        case PMU_LED_RED: return "RED";
        case PMU_LED_WHITE: return "WHT";
        case PMU_LED_RED_WHITE: return "BOTH";
        default: return "?";
    }
}

const char* pmu_action_name(uint8_t action) {
    switch (action) {
        case PMU_ACTION_HOST_LOGICAL_OFF: return "LOG_OFF";
        case PMU_ACTION_HOST_HARD_RESET: return "HRST";
        case PMU_ACTION_HOST_NORMAL_RESTART: return "RESTART";
        case PMU_ACTION_HOST_ENTER_DOWNLOAD_MODE: return "DL";
        case PMU_ACTION_HOST_EXIT_DOWNLOAD_MODE: return "EXIT_DL";
        case PMU_ACTION_HOST_POWER_ON: return "PWR_ON";
        case PMU_ACTION_HOST_SOFT_SLEEP: return "SLEEP";
        default: return "ACT";
    }
}
