/*
 * SPDX-FileCopyrightText: 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 * SPDX-FileCopyrightText: 2025 Project Contributors
 * SPDX-License-Identifier: MIT
 *
 * 中文：移植 Metalio 2c1e5ef 的 S31 浅睡 MPLL 校准兼容处理，许可见
 * components/board_i2c/LICENSE.Metalio。PSRAM 时钟交接由同目录 CMake 补丁负责。
 * English: Adapt the S31 light-sleep MPLL calibration workaround from Metalio
 * 2c1e5ef; see components/board_i2c/LICENSE.Metalio. The adjacent CMake backport
 * owns the PSRAM clock handoff.
 *
 * 冻结：仅显式浅睡唤醒时补校准，不停用看门狗，不改运行频率或 PSRAM 速度。
 * Frozen: patch calibration only during explicit light-sleep wake; retain watchdogs,
 * operating frequency and PSRAM speed.
 */
#include "s31_sleep_compat.h"

#include <stdbool.h>
#include <stdint.h>
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"
#include "esp_private/regi2c_ctrl.h"
#include "hal/regi2c_ctrl_ll.h"
#include "hal/regi2c_impl.h"
#include "soc/lp_clkrst_reg.h"
#include "soc/regi2c_bias.h"

static DRAM_ATTR bool s_explicit_sleep;
static DRAM_ATTR bool s_pmu_light_sleep;

uint32_t __real_pmu_sleep_start(uint32_t wake, uint32_t reject, uint32_t mem, bool deep);
void __real_rtc_clk_mpll_configure(uint32_t xtal, uint32_t frequency, bool clock_owned);

uint32_t IRAM_ATTR __wrap_pmu_sleep_start(uint32_t wake, uint32_t reject, uint32_t mem, bool deep) {
    s_pmu_light_sleep = !deep;
    uint32_t result = __real_pmu_sleep_start(wake, reject, mem, deep);
    s_pmu_light_sleep = false;
    return result;
}

void IRAM_ATTR __wrap_rtc_clk_mpll_configure(uint32_t xtal, uint32_t frequency, bool clock_owned) {
    if (!s_explicit_sleep || !s_pmu_light_sleep) {
        __real_rtc_clk_mpll_configure(xtal, frequency, clock_owned);
        return;
    }
    // PMU 已屏蔽中断并暂停另一核；这里只能用内部内存和无锁寄存器访问。
    // PMU masks interrupts and stalls the other CPU; use internal memory and lock-free register access here.
    // 校准次序来自 Espressif 9726078b55430ef8d2ce4a460ebf21d889bb5c12。
    // Calibration ordering follows Espressif 9726078b55430ef8d2ce4a460ebf21d889bb5c12.
    const bool gate_enabled = regi2c_ctrl_ll_master_is_clock_enabled();
    _regi2c_ctrl_ll_master_enable_clock(true);
    _regi2c_impl_write_mask(I2C_BIAS, 0, I2C_BIAS_DREG_1P1, 7, 4, 12);
    _regi2c_impl_write_mask(I2C_BIAS, 0, I2C_BIAS_DREG_1P1_PVT, 3, 0, 12);
    esp_rom_delay_us(10);
    const uint8_t mpll = 0x0b;
    _regi2c_impl_write_mask(mpll, 0, 1, 1, 0, 3);
    _regi2c_impl_write_mask(mpll, 0, 1, 2, 2, 1);
    REG_SET_FIELD(LP_AONCLKRST_MSPI_DIV_REG, LP_AONCLKRST_MSPI_FB_DIV, 9);
    _regi2c_impl_write_mask(mpll, 0, 3, 5, 4, 3);
    _regi2c_impl_write_mask(mpll, 0, 1, 2, 2, 0);
    __real_rtc_clk_mpll_configure(xtal, frequency, true);
    _regi2c_ctrl_ll_master_enable_clock(gate_enabled);
}

void app_s31_sleep_init(void) {
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 12);
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 12);
}

esp_err_t app_s31_light_sleep_start(void) {
    s_explicit_sleep = true;
    const esp_err_t result = esp_light_sleep_start();
    // CPU retention 可能从包装器外恢复；正常运行或入睡失败后均不得残留作用域。
    // CPU retention may resume outside the wrapper; leave no scope behind after wake or rejected sleep.
    s_pmu_light_sleep = false;
    s_explicit_sleep = false;
    return result;
}
