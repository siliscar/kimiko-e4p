/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 板级：I2C、IOE、EPD 总线与电源轨。
 *
 * Metalio E-INK4 Plus board: I2C, IO expander, EPD bus and rails.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "epd_board.h"
#include "esp_err.h"
#include "fca9555.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_PORT ((i2c_port_t)CONFIG_BOARD_I2C_PORT)
#define BOARD_I2C_FREQ_HZ CONFIG_BOARD_I2C_FREQ_HZ
/// TCA9555 INT#，S31 产品默认 GPIO2，用于浅睡唤醒。
/// / TCA9555 INT#, GPIO2 by default on the S31 product, used for light-sleep wake.
#define BOARD_IOE_INT_GPIO CONFIG_BOARD_IOE_INT_GPIO
/// FT6336U INT，S31 产品默认 GPIO5，低有效。
/// / FT6336U INT, GPIO5 by default on the S31 product, active low.
#define BOARD_TP_INT_GPIO CONFIG_BOARD_TP_INT_GPIO

#define BOARD_PRODUCT_NAME "mogo"
#define BOARD_DEVICE_NAME "mogo"
/// 芯片平放实测 -5,-119,-1005；转到设备坐标后屏幕朝上约为 +Z。
/// / Chip at rest on the bench: -5,-119,-1005. Device frame is about +Z face-up.
#define BOARD_ACCEL_ZERO_X_MG 119
#define BOARD_ACCEL_ZERO_Y_MG 5
#define BOARD_ACCEL_ZERO_Z_MG 1005

extern const EpdBoardDefinition epd_board_reader;

i2c_master_bus_handle_t board_i2c_bus(void);
fca9555_handle_t board_fca9555(void);

/// 前光冷/暖 PWM 引脚；没有前光的板返回 GPIO_NUM_NC。
/// / Frontlight cool/warm PWM pins; boards without a frontlight return GPIO_NUM_NC.
void board_frontlight_pins(gpio_num_t* cool, gpio_num_t* warm);

typedef struct {
    uint16_t ioe_input;
    uint16_t ioe_output;
    uint16_t ioe_invert;
    uint16_t ioe_config;
    int ioe_int_level;
    bool rails_on;
} board_status_t;

#define BOARD_I2C_DEV_N 5

typedef struct {
    const char* name;
    uint8_t addr;
    bool online;
} board_i2c_dev_t;

typedef struct {
    board_i2c_dev_t dev[BOARD_I2C_DEV_N];
    int offline_n;
    bool all_online;
} board_i2c_census_t;

/// 板级 I2C 设备在线快照，供出厂检查使用。
/// / Board I2C device census for factory checks.
void board_i2c_census_take(void);
const board_i2c_census_t* board_i2c_census(void);

esp_err_t board_get_status(board_status_t* status);
/// 只读 IN/OUT + INT#，CFG/INV 走缓存。
/// / Read IN/OUT + INT# only; CFG/INV come from cache.
esp_err_t board_get_ioe_status(board_status_t* status);
/// 连读 8 寄存器，刷新 CFG/INV 缓存。底栏「读取」和进页用。
/// / Sequential 8-register read; refreshes CFG/INV cache. Footer Read and page enter.
esp_err_t board_get_ioe_status_full(board_status_t* status);
int board_ioe_int_level(void);
/// 侧键音量键：bit0 音量+，bit1 音量-，1 为按下。原板没有侧键，返回不支持。
/// / Side volume keys: bit0 up, bit1 down, 1 = pressed. The original board has none.
#define BOARD_VOLUME_UP 0x01
#define BOARD_VOLUME_DOWN 0x02
esp_err_t board_read_volume_keys(uint8_t* pressed);
/// 机身顶部上键：true 为按下。原板没有这颗键，返回不支持。
/// / Top-edge up key: true while pressed. The original board has none.
esp_err_t board_read_up_key(bool* pressed);
/// 读取低有效卡座 CD；通信失败时不改输出，调用方不得将失败解释为拔卡。
/// / Read active-low slot CD; leave output untouched on failure, which is not card removal.
esp_err_t board_sd_detect(bool* present);
/// 简化检测；通信失败返回 false。挂载与拔卡判断应使用 checked detect。
/// / Convenience detection returns false on I/O failure; mounting and removal use checked detect.
bool board_sd_present(void);
/// 板子是否真的接了卡座 CD 脚；没有时由存储层用 CMD13 软件检测拔卡。
/// / Whether the board wires a slot CD pin; without one the storage layer soft-detects removal with CMD13.
bool board_sd_has_cd(void);
/// 供电可能仍开启；关断命令失败时不能当作已关闭。/ Power may still be on; a failed shutdown command cannot count as off.
bool board_rails_on(void);
/// 底层持供电锁后通过GPIO撤MODE/XOE，保留VCOM和高压，不访问I2C或扫描。
/// With the low-level power lock held, lower MODE/XOE through GPIO while retaining VCOM and HV, without I2C or scanning.
bool board_epd_release_drive(void);

/// 翻转 VCOMCTL：1=外部 VCOM_EN，0=自动时序。
/// / Toggle VCOMCTL: 1=external VCOM_EN, 0=auto timing.
esp_err_t board_toggle_vcomctl(void);

/// 拉低再放开触摸芯片的 RST（挂在 FCA9555 上）。触摸进了深度睡眠后不再应答 I2C，
/// 只有这条路能把它叫回来。返回后调用方还要等芯片启动完成。
/// / Pulse the touch RST on the FCA9555. After deep sleep the chip ignores I2C;
/// this is the only wake. Caller must wait for boot after return.
esp_err_t board_touch_reset(void);
/// high=false 拉低复位，high=true 放开。扩展口页要分两帧画脉冲时用。
/// / high=false asserts RST, high=true releases. IOE page draws the pulse in two frames.
esp_err_t board_tp_rst(bool high);
/// Metalio：TCA P13 关机脉冲。S3 原板无此脚。
/// / Metalio: TCA P13 power-off pulse. The S3 board has no such pin.
esp_err_t board_pwr_pulse(bool high);

/// 板上有无震动马达；没有时设置页隐藏入口，驱动静默空操作。
/// / Whether the board wires a vibration motor; without one the settings row hides and the driver no-ops.
bool board_has_motor(void);
/// 震动马达开关（Metalio：TCA9555 P03，高有效）。调用方只做短脉冲，睡前必须关断。
/// / Vibration motor switch (Metalio: TCA9555 P03, active high). Callers pulse briefly and must turn it off before sleep.
esp_err_t board_motor(bool on);

/// 读一遍 9555 输入。INT# 不锁存，输入稳定后会自行松开；浅睡前/醒后都要调。
/// / Read 9555 inputs. INT# is not latched and releases once inputs are stable;
/// call before light sleep and after wake.
void board_clear_ioe_int(void);

/// 睡前收口 9555：悬空的 Port1 推高；host_off 时 CW_INT 也改输出低，关机期间 INT# 不再被 PMU 事件拉低。浅睡不能用 host_off，按键唤醒要靠 CW_INT。
/// Park the 9555 before sleep: drive unconnected Port1 high; with host_off, CW_INT turns output-low so PMU events cannot pull INT# while off. Never use host_off for light sleep; key wake needs CW_INT.
esp_err_t board_ioe_sleep(bool host_off);
/// 恢复运行时的方向配置并清 INT#。/ Restore running directions and clear INT#.
esp_err_t board_ioe_wake(void);

#ifdef __cplusplus
}
#endif
