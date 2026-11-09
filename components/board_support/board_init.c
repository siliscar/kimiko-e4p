/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 板级开机装配：墨水屏、SD 探测、加速度计、PMU、触摸。
 *
 * Board bring-up: EPD, SD probe, accelerometer, PMU, touch.
 */

#include "board_init.h"

#include "epdiy.h"
#include "esp_log.h"
#include "frontlight.h"
#include "board_hardware.h"
#include "board_epd_timing.h"
#include "e0470_epaper_waveform.h"
#include "device_power.h"
#include "board_sd.h"

static const char* TAG = "read_pico_init";

#define BOARD_PCLK_MHZ 20
#define BOARD_VCOM_MV 1200
#define BOARD_ACCEL_INT1 GPIO_NUM_2

esp_err_t board_init(board_handle_t* hw) {
    if (hw == NULL) return ESP_ERR_INVALID_ARG;
    *hw = (board_handle_t){ 0 };

    ESP_LOGI(TAG, "%s", BOARD_PRODUCT_NAME);
    // 前光先于首帧：先关着初始化，保存的开关状态由 app_main 在设置加载后恢复。
    // Frontlight before the first frame: initialise dark; app_main restores the saved state after settings load.
    gpio_num_t fl_cool = GPIO_NUM_NC, fl_warm = GPIO_NUM_NC;
    board_frontlight_pins(&fl_cool, &fl_warm);
    esp_err_t fl_err = frontlight_init(fl_cool, fl_warm);
    if (fl_err != ESP_OK && fl_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "frontlight init %s", esp_err_to_name(fl_err));
    }
    ESP_LOGI(TAG, "Initializing 684x1216 EPD");
    // LUT 用 1K：LCD 路径下 epdiy 走 S3 向量实现，64K 里实际只用到 1K。
    // / 1K LUT: the LCD path uses the S3 vector impl; only 1K of the 64K is used.
    epd_init(&epd_board_reader, &E0470_DISPLAY, EPD_LUT_1K);
    // CD 走 FCA9555，必须等板级 I2C 起来再探测。
    // / CD is on the FCA9555; probe after board I2C is up.
    board_sd_start_probe();
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    // 当前取 20MHz：8-bit 总线下正好 16 µs 一行，与波形标定的 11090 µs 对齐。
    // / 20 MHz gives the vendor's 16 µs line on the 8-bit bus, matching the 11090 µs setpoint.
    board_epd_set_pclk(BOARD_PCLK_MHZ);
    esp_log_level_set("epdiy", ESP_LOG_WARN);
    epd_set_vcom(BOARD_VCOM_MV);

    // 默认灰阶表是开机从原厂表裁出来的，必须先算好再挂给 highlevel；
    // 差分刷新跳过整屏都在保持的前导相位。
    // / Default gray table is trimmed from the vendor table at boot; build it
    // before highlevel. Diff refresh skips leading all-hold phases.
    e0470_waveform_init();
    epd_set_leading_skip(true);
    hw->hl = epd_hl_init(&E0470_WAVEFORM);
    hw->framebuffer = epd_hl_get_framebuffer(&hw->hl);
    if (hw->framebuffer == NULL) {
        ESP_LOGE(TAG, "EPD framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    sc7a20h_config_t acc = {
        .i2c_addr = SC7A20H_ADDR_DEFAULT,
        .int1_gpio = BOARD_ACCEL_INT1,
        .sensor = SC7A20H_CONFIG_IDLE,
    };
    if (sc7a20h_init(board_i2c_bus(), &acc, &hw->sensor) == ESP_OK) {
        sc7a20h_sample_t sample;
        if (board_accel_read(hw->sensor, &sample) == ESP_OK) {
            ESP_LOGI(
                TAG, "SC7A20H: X=%dmg Y=%dmg Z=%dmg",
                sample.x_mg, sample.y_mg, sample.z_mg
            );
        }
        sc7a20h_version(hw->sensor, &hw->sensor_whoami, &hw->sensor_version);
        sc7a20h_power_down(hw->sensor);
        hw->sensor_ready = true;
    } else {
        ESP_LOGW(TAG, "SC7A20H not ready");
    }

    device_power_config_t pmu = DEVICE_POWER_CONFIG_DEFAULT();
    hw->pmu_ready = device_power_init(board_i2c_bus(), &pmu) == ESP_OK;
    if (!hw->pmu_ready) {
        ESP_LOGW(TAG, "PMU / CW32 0x2A not ready");
    }

    cst836u_config_t tp = {
        .i2c_addr = CST836U_ADDR_DEFAULT,
        .scl_speed_hz = CST836U_I2C_HZ_DEFAULT,
        .int_gpio = (gpio_num_t)BOARD_TP_INT_GPIO,
        .rst_gpio = GPIO_NUM_NC,
        .reset_fn = board_touch_reset,
    };
    if (cst836u_init(board_i2c_bus(), &tp, &hw->touch) == ESP_OK) {
        hw->touch_ready = true;
    } else {
        ESP_LOGW(TAG, "CST836U not ready");
    }

    return ESP_OK;
}

void board_deinit(board_handle_t* hw) {
    if (hw == NULL) return;
    if (hw->touch != NULL) {
        (void)cst836u_deinit(hw->touch);
        hw->touch = NULL;
    }
    if (hw->pmu_ready) {
        (void)device_power_deinit();
        hw->pmu_ready = false;
    }
    if (hw->sensor != NULL) {
        (void)sc7a20h_deinit(hw->sensor);
        hw->sensor = NULL;
    }
    epd_deinit();
    *hw = (board_handle_t){ 0 };
}

void board_sensor_wake(sc7a20h_handle_t h) {
    if (h && !sc7a20h_powered(h)) {
        sc7a20h_power_up(h);
    }
}

void board_sensor_sleep(sc7a20h_handle_t h) {
    if (h && sc7a20h_powered(h)) {
        sc7a20h_power_down(h);
    }
}

void board_accel_to_device(sc7a20h_sample_t* sample) {
    if (sample == NULL) return;
    int16_t xc = sample->x_mg;
    int16_t yc = sample->y_mg;
    int16_t zc = sample->z_mg;
    sample->x_mg = (int16_t)(-yc);
    sample->y_mg = (int16_t)(-xc);
    sample->z_mg = (int16_t)(-zc);
    xc = sample->x_raw;
    yc = sample->y_raw;
    zc = sample->z_raw;
    sample->x_raw = (int16_t)(-yc);
    sample->y_raw = (int16_t)(-xc);
    sample->z_raw = (int16_t)(-zc);
}

uint8_t board_accel_map_aoi(uint8_t src) {
    uint8_t out = (uint8_t)(src & 0x40);
    if (src & 0x01) out |= 0x08;
    if (src & 0x02) out |= 0x04;
    if (src & 0x04) out |= 0x02;
    if (src & 0x08) out |= 0x01;
    if (src & 0x10) out |= 0x20;
    if (src & 0x20) out |= 0x10;
    return out;
}

esp_err_t board_accel_read(sc7a20h_handle_t h, sc7a20h_sample_t* sample) {
    esp_err_t err = sc7a20h_read(h, sample);
    if (err == ESP_OK) board_accel_to_device(sample);
    return err;
}
