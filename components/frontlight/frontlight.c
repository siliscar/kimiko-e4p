/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：前光双路 PWM 实现。色温只改变两路权重，亮度是较强一路的目标占空比；
 * 50% 即原厂「冷暖同开」，因此向中间调色温时总输出会上升；该占空比再乘 20%
 * 输出上限，滑杆拉满也不到原厂满亮度。
 *
 * English: Dual-channel frontlight implementation. CCT only redistributes the two
 * channel weights while brightness is the stronger channel's target duty; 50% is
 * the vendor's "both on" state, so total output rises toward the middle. That duty
 * is then scaled by a 20% output ceiling, so even a full slider stays below the
 * vendor's full brightness.
 *
 * 冻结：不写 EPD 电源、VCOM 或面板时序；只用 LEDC。
 * Frozen: never touch EPD power, VCOM or panel timing; LEDC only.
 */
#include "frontlight.h"

#include "driver/ledc.h"
#include "esp_log.h"

#define TAG "frontlight"

// 避开 pwm_audio 的 timer 0 / channel 0-1；S31 没有蜂鸣器，但保留余量。
// Avoid pwm_audio's timer 0 / channels 0-1; the S31 board has no buzzer but keep the margin.
#define FRONTLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define FRONTLIGHT_LEDC_TIMER LEDC_TIMER_1
#define FRONTLIGHT_CH_COOL LEDC_CHANNEL_2
#define FRONTLIGHT_CH_WARM LEDC_CHANNEL_3

#define FRONTLIGHT_LEDC_RES LEDC_TIMER_10_BIT
#define FRONTLIGHT_DUTY_MAX 1023u
#define FRONTLIGHT_PWM_HZ 20000u

static struct {
    bool ready;
    bool on;
    bool suspended;
    bool suspended_on;
    uint8_t brightness;
    uint8_t cct;
    gpio_num_t cool_gpio;
    gpio_num_t warm_gpio;
} s_frontlight = {
    .brightness = 40,
    .cct = FRONTLIGHT_CCT_NEUTRAL,
};

static uint8_t clamp_u8(uint8_t value, uint8_t lo, uint8_t hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

// 色温权重：两端单路 100%，中间两路各 100%。/ CCT weights: one channel at 100% at the ends, both at 100% in the middle.
static void cct_weights(uint8_t cct, uint32_t* warm, uint32_t* cool) {
    *warm = cct <= FRONTLIGHT_CCT_NEUTRAL ? 100u : (uint32_t)(100 - cct) * 2u;
    *cool = cct >= FRONTLIGHT_CCT_NEUTRAL ? 100u : (uint32_t)cct * 2u;
}

// 亮度百分比先乘输出上限：滑杆 100% 只给 20% 占空比，避免最高档过亮。
// Scale the percentage by the output ceiling first: a full slider yields a 20% duty cycle so
// the top of the range is not glaring.
static uint32_t scale_duty(uint8_t brightness, uint32_t weight) {
    return (FRONTLIGHT_DUTY_MAX * (uint32_t)brightness * weight *
                FRONTLIGHT_OUTPUT_CEILING_PERCENT + 500000u) / 1000000u;
}

static esp_err_t write_output(void) {
    if (!s_frontlight.ready) return ESP_ERR_INVALID_STATE;
    uint32_t warm_weight = 0, cool_weight = 0;
    cct_weights(s_frontlight.cct, &warm_weight, &cool_weight);
    const uint32_t warm = s_frontlight.on
        ? scale_duty(s_frontlight.brightness, warm_weight) : 0u;
    const uint32_t cool = s_frontlight.on
        ? scale_duty(s_frontlight.brightness, cool_weight) : 0u;
    // 浅睡期间保持 0，不为一次写入重新拉起 LEDC。
    // Keep both at zero while suspended instead of waking LEDC for one write.
    const uint32_t out_warm = s_frontlight.suspended ? 0u : warm;
    const uint32_t out_cool = s_frontlight.suspended ? 0u : cool;
    esp_err_t err = ledc_set_duty(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_WARM, out_warm);
    if (err == ESP_OK) err = ledc_update_duty(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_WARM);
    if (err == ESP_OK) err = ledc_set_duty(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_COOL, out_cool);
    if (err == ESP_OK) err = ledc_update_duty(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_COOL);
    return err;
}

esp_err_t frontlight_init(gpio_num_t cool_gpio, gpio_num_t warm_gpio) {
    if (s_frontlight.ready) return ESP_OK;
    if (cool_gpio == GPIO_NUM_NC || warm_gpio == GPIO_NUM_NC || cool_gpio == warm_gpio) {
        ESP_LOGI(TAG, "no frontlight on this board");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = FRONTLIGHT_LEDC_MODE,
        .duty_resolution = FRONTLIGHT_LEDC_RES,
        .timer_num = FRONTLIGHT_LEDC_TIMER,
        .freq_hz = FRONTLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer: %s", esp_err_to_name(err));
        return err;
    }

    const ledc_channel_config_t channel = {
        .gpio_num = cool_gpio,
        .speed_mode = FRONTLIGHT_LEDC_MODE,
        .channel = FRONTLIGHT_CH_COOL,
        .timer_sel = FRONTLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };
    err = ledc_channel_config(&channel);
    if (err == ESP_OK) {
        ledc_channel_config_t warm = channel;
        warm.gpio_num = warm_gpio;
        warm.channel = FRONTLIGHT_CH_WARM;
        err = ledc_channel_config(&warm);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel: %s", esp_err_to_name(err));
        return err;
    }

    s_frontlight.cool_gpio = cool_gpio;
    s_frontlight.warm_gpio = warm_gpio;
    s_frontlight.ready = true;
    ESP_LOGI(TAG, "init cool=%d warm=%d freq=%u", (int)cool_gpio, (int)warm_gpio,
             (unsigned)FRONTLIGHT_PWM_HZ);
    return write_output();
}

void frontlight_deinit(void) {
    if (!s_frontlight.ready) return;
    s_frontlight.on = false;
    (void)write_output();
    (void)ledc_stop(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_COOL, 0);
    (void)ledc_stop(FRONTLIGHT_LEDC_MODE, FRONTLIGHT_CH_WARM, 0);
    gpio_reset_pin(s_frontlight.cool_gpio);
    gpio_reset_pin(s_frontlight.warm_gpio);
    s_frontlight.ready = false;
}

bool frontlight_ready(void) { return s_frontlight.ready; }

esp_err_t frontlight_apply(bool on, uint8_t brightness, uint8_t cct) {
    s_frontlight.on = on;
    s_frontlight.brightness = clamp_u8(brightness, FRONTLIGHT_BRIGHTNESS_MIN,
                                       FRONTLIGHT_BRIGHTNESS_MAX);
    s_frontlight.cct = clamp_u8(cct, FRONTLIGHT_CCT_WARM, FRONTLIGHT_CCT_COOL);
    if (!s_frontlight.ready) return ESP_ERR_NOT_SUPPORTED;
    return write_output();
}

esp_err_t frontlight_set_on(bool on) {
    if (s_frontlight.on == on) return ESP_OK;
    s_frontlight.on = on;
    return s_frontlight.ready ? write_output() : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t frontlight_set_brightness(uint8_t percent) {
    percent = clamp_u8(percent, FRONTLIGHT_BRIGHTNESS_MIN, FRONTLIGHT_BRIGHTNESS_MAX);
    if (s_frontlight.brightness == percent) return ESP_OK;
    s_frontlight.brightness = percent;
    return s_frontlight.ready ? write_output() : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t frontlight_set_cct(uint8_t percent) {
    percent = clamp_u8(percent, FRONTLIGHT_CCT_WARM, FRONTLIGHT_CCT_COOL);
    if (s_frontlight.cct == percent) return ESP_OK;
    s_frontlight.cct = percent;
    return s_frontlight.ready ? write_output() : ESP_ERR_NOT_SUPPORTED;
}

bool frontlight_on(void) { return s_frontlight.on; }
uint8_t frontlight_brightness(void) { return s_frontlight.brightness; }
uint8_t frontlight_cct(void) { return s_frontlight.cct; }

void frontlight_suspend(void) {
    if (!s_frontlight.ready || s_frontlight.suspended) return;
    s_frontlight.suspended_on = s_frontlight.on;
    s_frontlight.suspended = true;
    (void)write_output();
}

void frontlight_resume(void) {
    if (!s_frontlight.ready || !s_frontlight.suspended) return;
    s_frontlight.suspended = false;
    s_frontlight.on = s_frontlight.suspended_on;
    (void)write_output();
}
