/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：冷/暖双路前光，两路 LEDC PWM。亮度是较强通道的目标值，色温按百分比在
 * 两路之间分配；0=全暖，50=冷暖同开，100=全冷。输出再乘 20% 上限，滑杆拉满
 * 也不到原厂满亮度。不涉及 EPD 电源轨或面板时序。
 *
 * English: Dual cold/warm frontlight driven by two LEDC PWM channels. Brightness
 * is the target of the stronger channel and colour temperature shares it between
 * the two channels by percent: 0 = warm only, 50 = both, 100 = cool only. Output
 * then passes a 20% ceiling, so a full slider stays below the vendor's full
 * brightness. EPD rails and panel timing are out of scope.
 *
 * 冻结：不得改用 GPIO 开关或改动 PWM 频率；不得在这里碰 VCOM / EPD 轨。
 * Frozen: do not fall back to plain GPIO switching or change the PWM frequency;
 * never touch VCOM or the EPD rails here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 亮度下限；开关关闭由独立的 on 状态负责。/ Lower brightness bound; the on flag owns "off".
#define FRONTLIGHT_BRIGHTNESS_MIN 1
#define FRONTLIGHT_BRIGHTNESS_MAX 100
/// 输出上限：滑杆拉满只到原厂满亮度的 20%，百分比与 NVS 仍保持 1..100。
/// Output ceiling: a full slider reaches 20% of the factory full duty; percentages and NVS
/// keep their 1..100 range.
#define FRONTLIGHT_OUTPUT_CEILING_PERCENT 20
/// 色温百分比端点：暖、中性、冷。/ Colour-temperature endpoints: warm, neutral, cool.
#define FRONTLIGHT_CCT_WARM 0
#define FRONTLIGHT_CCT_NEUTRAL 50
#define FRONTLIGHT_CCT_COOL 100

/// 初始化两路 PWM。任一引脚为 GPIO_NUM_NC 或两者相同则返回 ESP_ERR_NOT_SUPPORTED。
/// / Initialise both PWM channels. Returns ESP_ERR_NOT_SUPPORTED when either pin is NC or both match.
esp_err_t frontlight_init(gpio_num_t cool_gpio, gpio_num_t warm_gpio);
/// 关输出并释放 GPIO；未初始化时无操作。/ Drop output and release the pins; a no-op when not initialised.
void frontlight_deinit(void);
/// 本板前光是否可用。/ Whether this board has a usable frontlight.
bool frontlight_ready(void);

/// 一次写入开关、亮度和色温，参数越界时夹紧。/ Apply on, brightness and CCT together, clamping out-of-range values.
esp_err_t frontlight_apply(bool on, uint8_t brightness, uint8_t cct);
esp_err_t frontlight_set_on(bool on);
esp_err_t frontlight_set_brightness(uint8_t percent);
esp_err_t frontlight_set_cct(uint8_t percent);

bool frontlight_on(void);
uint8_t frontlight_brightness(void);
uint8_t frontlight_cct(void);

/// 睡前全灭但保留用户状态；醒后恢复该状态。/ Blank output for sleep while keeping the user state; resume restores it.
void frontlight_suspend(void);
void frontlight_resume(void);

#ifdef __cplusplus
}
#endif
