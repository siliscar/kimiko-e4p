/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：触摸震动的短脉冲与强度档位；Metalio 的马达挂在 TCA9555 P03，高有效。
 * English: Short touch-vibration pulses and intensity steps on the Metalio TCA9555 P03 motor (active high).
 *
 * 冻结：只做短脉冲，不提供连续震动；睡前与断电前必须关断。
 * Frozen: short pulses only, never continuous vibration; always stop before sleep or power-off.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// 强度档位：弱/中/强对应 35/50/70 ms，与原厂默认档一致。
/// / Intensity steps: weak/medium/strong map to 35/50/70 ms, matching the vendor defaults.
typedef enum {
    APP_HAPTIC_WEAK = 0,   ///< 35 ms
    APP_HAPTIC_MEDIUM = 1, ///< 50 ms
    APP_HAPTIC_STRONG = 2, ///< 70 ms
} app_haptic_intensity_t;

/// 创建单次定时器并探测板级能力；设置值可在此之前写入，不依赖初始化顺序。
/// / Create the one-shot timer and probe the board; settings may be applied before this without ordering constraints.
void haptics_init(void);
/// 板上是否有马达。/ Whether the board has a motor.
bool haptics_available(void);
bool haptics_enabled(void);
uint8_t haptics_intensity(void);
void haptics_set_enabled(bool enabled);
/// 夹到 0..2；正在震动时不改变当前这一记。
/// / Clamp to 0..2; an in-flight pulse keeps its own duration.
void haptics_set_intensity(uint8_t intensity);
/// 触发一次短震；关闭、无马达或正在震动时忽略。
/// / Fire one short pulse; ignored while disabled, absent, or already pulsing.
void haptics_pulse(void);
/// 立即关断并取消待关断，睡前/断电前调用。
/// / Cut the motor now and cancel the pending stop; call before sleep or power-off.
void haptics_stop(void);
