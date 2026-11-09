/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus（ESP32-S31）板级：I2C、TCA9555、TPS65185、EPD 总线与电源轨。
 *
 * Metalio E-INK4 Plus (ESP32-S31) board: I2C, TCA9555, TPS65185, EPD bus and rails.
 * 通过公共 BSP API 向 main/ 提供板级资源。
 * Exposes board resources to main/ through the common BSP API.
 *
 * TPS65185 上电、VCOM 与重试参考原厂 display/epd_board_metalio_eink4_plus.c。
 * TPS65185 power, VCOM and retries follow vendor display/epd_board_metalio_eink4_plus.c.
 * 原厂来源版本和 MIT 许可见 components/board_i2c/README.md 与 LICENSE.Metalio。
 * Vendor revision and MIT notice: components/board_i2c/README.md and LICENSE.Metalio.
 */

#include "board_hardware.h"

#include <string.h>
#include <stdatomic.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "board_i2c.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_DISPLAY_POWER_DIAG
#include "esp_timer.h"
#endif

#include "epd_display.h"
#include "epdiy.h"
#include "fca9555.h"
#include "board_epd_timing.h"

#define TAG "read_pico"

/* ------------------------------------------------------------------------- */
/* 引脚 / 扩展器位（Metalio V1.3/1.4）                                        */
/* Pinout / expander bits (Metalio V1.3/1.4)                                  */
/* ------------------------------------------------------------------------- */

#define EPD_PIN_D0 GPIO_NUM_8
#define EPD_PIN_D1 GPIO_NUM_9
#define EPD_PIN_D2 GPIO_NUM_10
#define EPD_PIN_D3 GPIO_NUM_11
#define EPD_PIN_D4 GPIO_NUM_12
#define EPD_PIN_D5 GPIO_NUM_13
#define EPD_PIN_D6 GPIO_NUM_14
#define EPD_PIN_D7 GPIO_NUM_15

#define EPD_PIN_XCL GPIO_NUM_16
#define EPD_PIN_XLE GPIO_NUM_17
#define EPD_PIN_XOE GPIO_NUM_18
#define EPD_PIN_XSTL GPIO_NUM_4
#define EPD_PIN_SPV GPIO_NUM_35
#define EPD_PIN_CKV GPIO_NUM_40
#define EPD_PIN_MODE GPIO_NUM_19
#define EPD_PIN_BORDER GPIO_NUM_37

#define BOARD_I2C_SDA GPIO_NUM_0
#define BOARD_I2C_SCL GPIO_NUM_1
#define BOARD_IOE_INT GPIO_NUM_2
// 上键是机身顶部那颗实体键，接在 S31 的 BOOT 脚上，低有效；esp32s3 没有 61 号脚。
// The up key is the top-edge physical key on the S31 BOOT pin, active low; esp32s3 has no GPIO61.
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#define BOARD_UP_KEY GPIO_NUM_61
#endif

#define BOARD_TCA_ADDR 0x20
#define BOARD_TPS_ADDR 0x68
#define BOARD_TPS_I2C_HZ 100000

/* TCA9555 位号：手册 P0x/P1x → 0..15。 */
/* TCA9555 bit numbers: P0x/P1x -> 0..15. */
#define IOE_BIT(io) ((uint8_t)(1u << ((unsigned)(io) & 7u)))
#define IOE_PORT(io) ((int)((unsigned)(io) >> 3))

#define IO_VCOM_CTRL 0  // P00
#define IO_PWRUP 1      // P01
#define IO_WAKEUP 2     // P02
#define IO_MOTOR 3      // P03
#define IO_PA_EN 4      // P04
#define IO_CAM_SCR 5    // P05
#define IO_PWR_GOOD 7   // P07
#define IO_VOL_UP 8     // P10
#define IO_VOL_DOWN 9   // P11
#define IO_TP_RST 10    // P12
#define IO_PWR_PULSE 11 // P13
#define IO_BT_PA_PWR 12 // P14
#define IO_RTC_INT 14   // P16
#define IO_POWER 15     // P17

#define P0_TPS_MASK (IOE_BIT(IO_VCOM_CTRL) | IOE_BIT(IO_PWRUP) | IOE_BIT(IO_WAKEUP))
#define P0_OUTPUT_MASK \
    (P0_TPS_MASK | IOE_BIT(IO_MOTOR) | IOE_BIT(IO_PA_EN) | IOE_BIT(IO_CAM_SCR))
#define P0_INPUT_MASK ((uint8_t)~P0_OUTPUT_MASK)

#define P1_RAIL_MASK (IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR))
#define P1_OUTPUT_MASK (P1_RAIL_MASK | IOE_BIT(IO_PWR_PULSE))
#define P1_INPUT_MASK ((uint8_t)~P1_OUTPUT_MASK)

#define EPD_VCOM_MV 1200

/* TPS65185 寄存器（与 Metalio tps65185.h 一致）。 */
/* TPS65185 registers (same as Metalio's tps65185.h). */
#define TPS_REG_ENABLE 0x01
#define TPS_REG_VCOM1 0x03
#define TPS_REG_VCOM2 0x04
#define TPS_REG_UPSEQ0 0x09
#define TPS_REG_UPSEQ1 0x0A
#define TPS_REG_PG 0x0F
#define TPS_REG_REVID 0x10

/* ------------------------------------------------------------------------- */
/* 状态                                                                       */
/* ------------------------------------------------------------------------- */

static i2c_master_bus_handle_t s_i2c_bus;
static fca9555_handle_t s_ioe;
static i2c_master_dev_handle_t s_tps;

static bool s_ioe_ok;
static atomic_bool s_tps_ok;
static atomic_bool s_rails_on;

static atomic_bool s_pwrup;
static atomic_bool s_vcom_ctrl;
static atomic_bool s_wakeup;
#if CONFIG_DISPLAY_POWER_DIAG
// 仅记录请求时序；软件锁存不能替代实际轨电压测量。
// Record request timing only; software latches do not measure rail voltages.
static int64_t s_diag_power_on_us, s_diag_power_off_us;
#endif
static atomic_uchar s_p0_extra; // Motor / PA / CAM 锁存
static uint8_t s_p1_out = (uint8_t)(IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR));
static int s_vcom_mv = EPD_VCOM_MV;

static lcd_bus_config_t s_lcd_bus = {
    .clock = EPD_PIN_XCL,
    .ckv = EPD_PIN_CKV,
    .leh = EPD_PIN_XLE,
    .start_pulse = EPD_PIN_XSTL,
    .stv = EPD_PIN_SPV,
    .data = {
        EPD_PIN_D0, EPD_PIN_D1, EPD_PIN_D2, EPD_PIN_D3,
        EPD_PIN_D4, EPD_PIN_D5, EPD_PIN_D6, EPD_PIN_D7,
        EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7,
        EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7, EPD_PIN_D7,
    },
};

/* ------------------------------------------------------------------------- */
/* I2C / TCA9555                                                              */
/* ------------------------------------------------------------------------- */

static esp_err_t ioe_write(uint8_t reg, uint8_t value) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    switch (reg) {
        case 2: return fca9555_set_output(s_ioe, 0, value);
        case 3: return fca9555_set_output(s_ioe, 1, value);
        case 4: return fca9555_set_inversion(s_ioe, 0, value);
        case 5: return fca9555_set_inversion(s_ioe, 1, value);
        case 6: return fca9555_set_config(s_ioe, 0, value);
        case 7: return fca9555_set_config(s_ioe, 1, value);
        default: return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t ioe_read(uint8_t reg, uint8_t* value) {
    if (!s_ioe_ok || value == NULL) return ESP_ERR_INVALID_STATE;
    return fca9555_read_reg(s_ioe, reg, value);
}

static uint8_t p0_value(void) {
    uint8_t v = s_p0_extra;
    if (s_pwrup) v |= IOE_BIT(IO_PWRUP);
    if (s_vcom_ctrl) v |= IOE_BIT(IO_VCOM_CTRL);
    if (s_wakeup) v |= IOE_BIT(IO_WAKEUP);
    return v;
}

static esp_err_t p0_write(void) {
    // 获得总线锁后才读取供电位，防止并发马达写回过期的上电快照。
    // Read power bits only after acquiring the bus lock so concurrent motor writes cannot restore a stale powered snapshot.
    esp_err_t lock_err = board_i2c_lock(50);
    if (lock_err != ESP_OK) return lock_err;
    esp_err_t err = ioe_write(2, p0_value()); // OUT0
    board_i2c_unlock();
    if (err != ESP_OK) ESP_LOGW(TAG, "TCA OUT0 write: %s", esp_err_to_name(err));
    return err;
}

static void p1_write(void) {
    esp_err_t err = ioe_write(3, s_p1_out); // OUT1
    if (err != ESP_OK) ESP_LOGW(TAG, "TCA OUT1 write: %s", esp_err_to_name(err));
}

static esp_err_t ioe_init(void) {
    fca9555_config_t cfg = FCA9555_CONFIG_DEFAULT();
    cfg.i2c_addr = BOARD_TCA_ADDR;
    cfg.scl_speed_hz = 100000;
    cfg.int_gpio = BOARD_IOE_INT;
    if (s_ioe == NULL && fca9555_init(s_i2c_bus, &cfg, &s_ioe) != ESP_OK) return ESP_FAIL;

    s_p0_extra = IOE_BIT(IO_CAM_SCR);
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = false;

    esp_err_t err = ESP_FAIL;
    // USB/主机复位可能截断 I2C 事务；在其他消费者启动前有界重置总线并重试。
    // USB/host reset may interrupt an I2C transaction; reset/retry boundedly before starting other consumers.
    for (int attempt = 0; attempt < 3; ++attempt) {
        s_ioe_ok = true;
        err = ioe_write(6, P0_INPUT_MASK); // CFG0
        if (err == ESP_OK) err = ioe_write(7, P1_INPUT_MASK); // CFG1
        if (err == ESP_OK) err = ioe_write(4, 0x00); // POL0
        if (err == ESP_OK) err = ioe_write(5, 0x00); // POL1
        if (err == ESP_OK) err = ioe_write(3, s_p1_out); // OUT1
        if (err == ESP_OK) err = p0_write();
        if (err == ESP_OK) break;
        s_ioe_ok = false;
        if (attempt < 2) {
            (void)i2c_master_bus_reset(s_i2c_bus);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 init: %s", esp_err_to_name(err));
        s_ioe_ok = false;
        return err;
    }

    // 读一遍输入松开 INT#。 / Read inputs to release INT#.
    (void)fca9555_clear_int(s_ioe);
    ESP_LOGI(TAG, "TCA9555 ready at 0x%02X", BOARD_TCA_ADDR);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* TPS65185                                                                   */
/* ------------------------------------------------------------------------- */

static esp_err_t tps_write(uint8_t reg, uint8_t value) {
    if (s_tps == NULL) return ESP_ERR_INVALID_STATE;
    const uint8_t buf[2] = {reg, value};
    return board_i2c_transmit(s_tps, buf, sizeof(buf), 50);
}

static esp_err_t tps_read(uint8_t reg, uint8_t* value) {
    if (s_tps == NULL || value == NULL) return ESP_ERR_INVALID_STATE;
    return board_i2c_read_stop(s_tps, reg, value, 1, 50);
}

static esp_err_t tps_probe(void) {
    if (s_i2c_bus == NULL) return ESP_ERR_INVALID_STATE;
    return board_i2c_probe(s_i2c_bus, BOARD_TPS_ADDR, 50);
}

static esp_err_t tps_init(void) {
    esp_err_t err = board_i2c_add_device(s_i2c_bus, BOARD_TPS_ADDR, &s_tps);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TPS device add: %s", esp_err_to_name(err));
        s_tps = NULL;
        return err;
    }
    if (tps_probe() != ESP_OK) {
        ESP_LOGW(TAG, "TPS65185 not responding at 0x%02X", BOARD_TPS_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t tps_set_vcom_mv(int mv) {
    if (mv < 0) mv = -mv;
    if (mv > 5000) mv = 5000;
    const unsigned steps = (unsigned)mv / 10;
    esp_err_t err = tps_write(TPS_REG_VCOM2, (uint8_t)((steps >> 8) & 0x01));
    if (err == ESP_OK) err = tps_write(TPS_REG_VCOM1, (uint8_t)(steps & 0xFF));
    return err;
}

/* ------------------------------------------------------------------------- */
/* EPD 控制 GPIO                                                              */
/* ------------------------------------------------------------------------- */

static void epd_ctrl_gpios_low(void) {
    const gpio_num_t pins[] = {EPD_PIN_XOE, EPD_PIN_MODE, EPD_PIN_BORDER};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }
}

static void lcd_init(void) {
    const EpdDisplay_t* display = epd_get_display();
    const int init_pclk_mhz = display->bus_speed; // 8-bit：bus_speed 已是 MHz / bus_speed is MHz at 8-bit
    board_epd_scan_t scan;
    board_epd_scan(init_pclk_mhz, BOARD_EPD_SCAN_FULL, &scan);
    board_epd_init_lcd(&s_lcd_bus, display->bus_width, display->width, display->height, &scan);
}

/* ------------------------------------------------------------------------- */
/* EpdBoardDefinition                                                         */
/* ------------------------------------------------------------------------- */

static void board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    if (mask->ep_output_enable) {
        gpio_set_level(EPD_PIN_XOE, state->ep_output_enable ? 1 : 0);
    }
    if (mask->ep_mode) {
        gpio_set_level(EPD_PIN_MODE, state->ep_mode ? 1 : 0);
    }
    if (mask->ep_output_enable || mask->ep_mode || mask->ep_stv) {
        p0_write();
    }
}

static void board_init(uint32_t epd_row_width) {
    (void)epd_row_width;

    // 深睡可能锁住并行总线，先释放。 / Release GPIO holds left by deep sleep.
    const gpio_num_t bus_pins[] = {
        EPD_PIN_D0, EPD_PIN_D1, EPD_PIN_D2, EPD_PIN_D3,
        EPD_PIN_D4, EPD_PIN_D5, EPD_PIN_D6, EPD_PIN_D7,
        EPD_PIN_XCL, EPD_PIN_XLE, EPD_PIN_XOE, EPD_PIN_XSTL,
        EPD_PIN_SPV, EPD_PIN_CKV, EPD_PIN_MODE, EPD_PIN_BORDER,
    };
    for (size_t i = 0; i < sizeof(bus_pins) / sizeof(bus_pins[0]); i++) {
        gpio_hold_dis(bus_pins[i]);
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    if (ioe_init() != ESP_OK) {
        ESP_LOGE(TAG, "no TCA9555; panel power unavailable");
    }
    vTaskDelay(pdMS_TO_TICKS(80));
    (void)tps_init();

    epd_ctrl_gpios_low();
    lcd_init();
    ESP_LOGI(TAG, "Metalio E-INK4 Plus bus initialized (8-bit, pclk %d MHz)",
             epd_get_display()->bus_speed);
}

static void board_deinit(void) {
    epd_lcd_deinit();
    if (s_tps) {
        i2c_master_bus_rm_device(s_tps);
        s_tps = NULL;
    }
    if (s_ioe) {
        fca9555_deinit(s_ioe);
        s_ioe = NULL;
        s_ioe_ok = false;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

#if CONFIG_DISPLAY_POWER_DIAG
// 失败后才做额外读回，正常上电轮询与扫描不增加 I2C 访问。
// Extra readback happens only after failure, adding no I2C access to normal power polling or scanning.
static void power_diag_failed(void) {
    uint8_t input = 0, output = 0, pg = 0, enable = 0;
    esp_err_t input_err = ioe_read(0, &input);
    esp_err_t output_err = ioe_read(2, &output);
    esp_err_t pg_err = tps_read(TPS_REG_PG, &pg);
    esp_err_t enable_err = tps_read(TPS_REG_ENABLE, &enable);
    ESP_LOGW(TAG, "POWER_DIAG event=failed in=0x%02x in_err=%d out=0x%02x out_err=%d pg=0x%02x pg_err=%d enable=0x%02x enable_err=%d request=0x%02x",
             input, input_err, output, output_err, pg, pg_err, enable, enable_err, p0_value());
}
#endif

static void board_poweroff(epd_ctrl_state_t* state);

static bool board_poweron_once(epd_ctrl_state_t* state) {
    s_tps_ok = false;
    s_rails_on = false;
    state->ep_output_enable = false;
    state->ep_mode = false;
    gpio_set_level(EPD_PIN_XOE, 0);
    gpio_set_level(EPD_PIN_MODE, 0);
    if (!s_ioe_ok || s_tps == NULL) return false;
    state->ep_stv = true;
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = true;
    if (p0_write() != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t revid = 0;
    if (tps_probe() != ESP_OK || tps_read(TPS_REG_REVID, &revid) != ESP_OK ||
        tps_write(TPS_REG_UPSEQ0, 0xE4) != ESP_OK ||
        tps_write(TPS_REG_UPSEQ1, 0x55) != ESP_OK) return false;
    s_pwrup = true;
    s_vcom_ctrl = true;
    if (p0_write() != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    bool pgood = false;
    for (int i = 0; i < 200; ++i) {
        uint8_t in0 = 0;
        if (ioe_read(0, &in0) == ESP_OK && (in0 & IOE_BIT(IO_PWR_GOOD))) {
            pgood = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!pgood) return false;
    if (tps_write(TPS_REG_ENABLE, 0x3F) != ESP_OK ||
        tps_set_vcom_mv(s_vcom_mv) != ESP_OK) return false;
    bool pg_ready = false;
    for (int i = 0; i < 200; ++i) {
        uint8_t pg = 0;
        if (tps_read(TPS_REG_PG, &pg) == ESP_OK && (pg & 0xFA) == 0xFA) {
            pg_ready = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!pg_ready || p0_write() != ESP_OK) return false;
    state->ep_output_enable = true;
    gpio_set_level(EPD_PIN_XOE, 1);
    s_rails_on = true;
    s_tps_ok = true;
    ESP_LOGI(TAG, "EPD rails on, VCOM=-%d mV", s_vcom_mv);
    return true;
}

static void board_poweron(epd_ctrl_state_t* state) {
    if (s_rails_on) return;
#if CONFIG_DISPLAY_POWER_DIAG
    const int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "POWER_DIAG event=on_start off_ms=%lld request=0x%02x oe=%d mode=%d",
             (long long)(s_diag_power_off_us ? (started_us - s_diag_power_off_us) / 1000 : -1),
             p0_value(), state->ep_output_enable, state->ep_mode);
#endif
    // 原厂初次尝试加三次完整掉电重试；持续失败时重启，避免错误供电下继续扫描。
    // Vendor initial attempt plus three complete power-cycle retries; restart instead of scanning on failed rails.
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (board_poweron_once(state)) {
#if CONFIG_DISPLAY_POWER_DIAG
            s_diag_power_on_us = esp_timer_get_time();
            ESP_LOGI(TAG, "POWER_DIAG event=ready elapsed_ms=%lld request=0x%02x oe=%d mode=%d",
                     (long long)((s_diag_power_on_us - started_us) / 1000),
                     p0_value(), state->ep_output_enable, state->ep_mode);
#endif
            return;
        }
#if CONFIG_DISPLAY_POWER_DIAG
        power_diag_failed();
#endif
        board_poweroff(state);
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGE(TAG, "TPS PWR_GOOD timeout");
    esp_restart();
}

static bool board_poweroff_checked(epd_ctrl_state_t* state) {
#if CONFIG_DISPLAY_POWER_DIAG
    const bool was_on = s_rails_on;
#endif
    // 只有真正关电才撤 VCOM；release_drive 不碰它。
    // VCOM drops only on real power-off; release_drive never touches it.
    state->ep_stv = false;
    state->ep_output_enable = false;
    state->ep_mode = false;
    gpio_set_level(EPD_PIN_XOE, 0);
    gpio_set_level(EPD_PIN_MODE, 0);
    // 先撤像素驱动至少12us，再公开撤电状态，防止并发马达写提前关闭电源。
    // Lower pixel drive for at least 12 us before publishing off bits, including concurrent motor writes.
    esp_rom_delay_us(12);
    s_vcom_ctrl = false;
    s_pwrup = false;
    (void)p0_write();
    vTaskDelay(pdMS_TO_TICKS(2));
    s_wakeup = false;
    const bool confirmed = p0_write() == ESP_OK;
    // 最后一次成功写入包含全部关断位；失败不能把软件状态伪装成已关电。
    // The final successful write contains all shutdown bits; never report power-off after a failed command.
    s_rails_on = !confirmed;
    s_tps_ok = false;
#if CONFIG_DISPLAY_POWER_DIAG
    s_diag_power_off_us = esp_timer_get_time();
    ESP_LOGI(TAG, "POWER_DIAG event=off_request was_on=%d held_ms=%lld request=0x%02x oe=%d mode=%d",
             was_on, (long long)(was_on && s_diag_power_on_us ?
                 (s_diag_power_off_us - s_diag_power_on_us) / 1000 : -1),
             p0_value(), state->ep_output_enable, state->ep_mode);
#endif
    return confirmed;
}

static void board_poweroff(epd_ctrl_state_t* state) { (void)board_poweroff_checked(state); }

static void board_release_drive(void) { (void)board_epd_release_drive(); }

static void board_measure_vcom(epd_ctrl_state_t* state) { (void)state; }

static float board_temperature(void) { return 25.0f; }

static void board_set_vcom(int value) {
    if (value < 0) value = -value;
    s_vcom_mv = value > 5000 ? 5000 : value;
}

const EpdBoardDefinition epd_board_reader = {
    .poweroff_checked = board_poweroff_checked,
    .release_drive = board_release_drive,
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .poweroff = board_poweroff,
    .measure_vcom = board_measure_vcom,
    .get_temperature = board_temperature,
    .set_vcom = board_set_vcom,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};

/* ------------------------------------------------------------------------- */
/* 板级接口 / BSP API                                                       */
/* ------------------------------------------------------------------------- */

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c_bus; }

void board_frontlight_pins(gpio_num_t* cool, gpio_num_t* warm) {
    // Metalio V1.3/1.4：冷光 GPIO6，暖光 GPIO7，原厂 20 kHz PWM。
    // Metalio V1.3/1.4: cool GPIO6, warm GPIO7, vendor 20 kHz PWM.
    if (cool != NULL) *cool = GPIO_NUM_6;
    if (warm != NULL) *warm = GPIO_NUM_7;
}

fca9555_handle_t board_fca9555(void) { return s_ioe; }

static board_i2c_census_t s_census;

void board_i2c_census_take(void) {
    static const struct {
        uint8_t addr;
        const char* name;
    } map[BOARD_I2C_DEV_N] = {
        {BOARD_TCA_ADDR, "TCA9555"},
        {BOARD_TPS_ADDR, "TPS65185"},
        {0x38, "FT6336U"},
        {0x19, "SC7A20H"},
        {0x55, "BQ27220"},
    };

    s_census = (board_i2c_census_t){0};
    s_census.all_online = true;
    for (int i = 0; i < BOARD_I2C_DEV_N; i++) {
        s_census.dev[i].name = map[i].name;
        s_census.dev[i].addr = map[i].addr;
        s_census.dev[i].online =
            s_i2c_bus != NULL && board_i2c_probe(s_i2c_bus, map[i].addr, 50) == ESP_OK;
        if (!s_census.dev[i].online) {
            s_census.offline_n++;
            s_census.all_online = false;
        }
    }
}

const board_i2c_census_t* board_i2c_census(void) { return &s_census; }

int board_ioe_int_level(void) { return gpio_get_level(BOARD_IOE_INT) ? 1 : 0; }

esp_err_t board_read_volume_keys(uint8_t* pressed) {
    if (pressed == NULL) return ESP_ERR_INVALID_ARG;
    *pressed = 0;
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    uint8_t p1 = 0;
    esp_err_t err = fca9555_read_input(s_ioe, 1, &p1);
    if (err != ESP_OK) return err;
    // P10/P11 低有效；P10 音量+，P11 音量-。/ P10/P11 are active low: P10 up, P11 down.
    if ((p1 & IOE_BIT(IO_VOL_UP)) == 0) *pressed |= BOARD_VOLUME_UP;
    if ((p1 & IOE_BIT(IO_VOL_DOWN)) == 0) *pressed |= BOARD_VOLUME_DOWN;
    return ESP_OK;
}

esp_err_t board_read_up_key(bool* pressed) {
    if (pressed == NULL) return ESP_ERR_INVALID_ARG;
    *pressed = false;
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 第一次读才配置，避免没人用到时占用 BOOT 脚；上拉、无中断，轮询去抖。
    // Configure on first read so an unused key leaves the BOOT pin alone; pull-up, no IRQ,
    // debounced by polling.
    static bool s_ready;
    if (!s_ready) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << (unsigned)BOARD_UP_KEY,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) != ESP_OK) return ESP_FAIL;
        s_ready = true;
    }
    *pressed = gpio_get_level(BOARD_UP_KEY) == 0;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t fill_ioe_status(board_status_t* status, bool refresh_cfg) {
    (void)refresh_cfg;
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;

    fca9555_map_t map = {0};
    esp_err_t err = fca9555_read_all(s_ioe, &map);
    if (err != ESP_OK) return err;
    status->ioe_input = map.input;
    status->ioe_output = map.output;
    status->ioe_invert = map.invert;
    status->ioe_config = map.config;
    status->ioe_int_level = board_ioe_int_level();
    status->rails_on = s_rails_on;
    return ESP_OK;
}

esp_err_t board_get_ioe_status(board_status_t* status) {
    return fill_ioe_status(status, false);
}

esp_err_t board_get_ioe_status_full(board_status_t* status) {
    return fill_ioe_status(status, true);
}

esp_err_t board_get_status(board_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    (void)fill_ioe_status(status, false);

    status->rails_on = s_rails_on;
    return ESP_OK;
}

esp_err_t board_toggle_vcomctl(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    s_vcom_ctrl = !s_vcom_ctrl;
    p0_write();
    return ESP_OK;
}

bool board_rails_on(void) { return s_rails_on; }

bool board_epd_release_drive(void) {
    if (!s_rails_on) return true;
    epd_ctrl_state_t* state = epd_ctrl_state();
    state->ep_mode = false;
    state->ep_output_enable = false;
    gpio_set_level(EPD_PIN_MODE, 0);
    gpio_set_level(EPD_PIN_XOE, 0);
    // VCOM_CTRL 保持到板级关电；每次推屏后拉低会让同一 burst 内后续扫描失去公共电压。
    // Keep VCOM_CTRL until board power-off; dropping it after each output leaves later scans in the same burst without a common voltage.
    return true;
}

esp_err_t board_sd_detect(bool* present) {
    if (present == NULL) return ESP_ERR_INVALID_ARG;
    // Metalio 没有卡座 CD 脚；挂载层负责实际探测。 / No card-detect pin on Metalio.
    *present = true;
    return ESP_OK;
}

bool board_sd_present(void) { return true; }

// Metalio 没有卡座 CD 脚，拔卡只能靠存储层的软件检测。
// No slot CD on Metalio; removal is soft-detected by the storage layer.
bool board_sd_has_cd(void) { return false; }

void board_clear_ioe_int(void) {
    if (!s_ioe_ok) return;
    (void)fca9555_clear_int(s_ioe);
}

esp_err_t board_ioe_sleep(bool host_off) {
    (void)host_off;
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    // 浅睡期间 TCA 锁存保持：马达必须在睡前关断，否则会一直通电。
    // / The TCA latch holds through light sleep; the motor must be off before sleeping or it stays powered.
    s_p0_extra &= (uint8_t)~IOE_BIT(IO_MOTOR);
    s_pwrup = false;
    s_vcom_ctrl = false;
    s_wakeup = false;
    p0_write();
    // 无线已停栈且前光已灭，按原厂浅睡流程关闭 P14；关机脉冲回低。
    // Radios are stopped and frontlight is off; cut P14 as in vendor light sleep and lower the power pulse.
    s_p1_out &= (uint8_t)~(IOE_BIT(IO_PWR_PULSE) | IOE_BIT(IO_BT_PA_PWR));
    p1_write();
    // P12 是触摸复位，保持输出高；按键仍为输入，P14 在唤醒入口恢复。
    // P12 is touch reset and stays high; keys remain inputs and wake restores P14.
    (void)ioe_write(7, P1_INPUT_MASK);
    (void)fca9555_clear_int(s_ioe);
    return ESP_OK;
}

esp_err_t board_ioe_wake(void) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ioe_write(6, P0_INPUT_MASK);
    if (err == ESP_OK) err = ioe_write(7, P1_INPUT_MASK);
    if (err == ESP_OK) p0_write();
    if (err == ESP_OK) {
        s_p1_out |= (uint8_t)(IOE_BIT(IO_TP_RST) | IOE_BIT(IO_BT_PA_PWR));
        p1_write();
    }
    (void)fca9555_clear_int(s_ioe);
    return err;
}

esp_err_t board_tp_rst(bool high) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    if (high) {
        s_p1_out |= IOE_BIT(IO_TP_RST);
    } else {
        s_p1_out &= (uint8_t)~IOE_BIT(IO_TP_RST);
    }
    p1_write();
    return ESP_OK;
}

esp_err_t board_touch_reset(void) {
    esp_err_t err = board_tp_rst(false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = board_tp_rst(true);
    vTaskDelay(pdMS_TO_TICKS(300));
    return err;
}

esp_err_t board_pwr_pulse(bool high) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    if (high) {
        s_p1_out |= IOE_BIT(IO_PWR_PULSE);
    } else {
        s_p1_out &= (uint8_t)~IOE_BIT(IO_PWR_PULSE);
    }
    p1_write();
    return ESP_OK;
}

bool board_has_motor(void) { return true; }

esp_err_t board_motor(bool on) {
    if (!s_ioe_ok) return ESP_ERR_INVALID_STATE;
    // P03 高有效；只改这一个锁存位，TPS/PA/CAM 的状态保持不动。
    // / P03 is active high; touch only this latch bit and leave TPS/PA/CAM alone.
    if (on) {
        s_p0_extra |= IOE_BIT(IO_MOTOR);
    } else {
        s_p0_extra &= (uint8_t)~IOE_BIT(IO_MOTOR);
    }
    return p0_write();
}
