#!/usr/bin/env python3
"""中文：执行实际 S31 校准包装器及 CMake 补丁，验证作用域、顺序和 SDK 漂移拒绝。
English: Execute the S31 calibration wrapper and CMake backport, checking scope, ordering and SDK drift rejection.
"""
from pathlib import Path
import os
import subprocess
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
COMPAT = ROOT / "main/power/s31_sleep_compat.c"
BACKPORT = ROOT / "main/power/s31_sleep_compat.cmake"

STUBS = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define IRAM_ATTR
#define DRAM_ATTR
#define ESP_OK 0
#define I2C_BIAS 6
#define I2C_BIAS_DREG_1P1 10
#define I2C_BIAS_DREG_1P1_PVT 11
#define LP_AONCLKRST_MSPI_DIV_REG 20
#define LP_AONCLKRST_MSPI_FB_DIV 21
#define REGI2C_WRITE_MASK(block, reg, value) init_write(block, reg, value)
#define REG_SET_FIELD(reg, field, value) set_field(reg, field, value)
bool regi2c_ctrl_ll_master_is_clock_enabled(void);
void _regi2c_ctrl_ll_master_enable_clock(bool enabled);
void _regi2c_impl_write_mask(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
void esp_rom_delay_us(unsigned);
void init_write(unsigned, unsigned, unsigned);
void set_field(unsigned, unsigned, unsigned);
esp_err_t esp_light_sleep_start(void);
'''

TEST = r'''
#include <assert.h>
#include <stdio.h>
static bool gate, expect_patch, deep_sleep;
static unsigned events, calibrations, init_writes, pmu_calls;
static int scenario;
static bool owned_argument;
bool regi2c_ctrl_ll_master_is_clock_enabled(void) { return gate; }
void _regi2c_ctrl_ll_master_enable_clock(bool enabled) {
    if (!events) assert(enabled);
    else assert(events == 10);
    ++events;
    gate = enabled;
}
void _regi2c_impl_write_mask(unsigned block, unsigned host, unsigned reg,
                           unsigned high, unsigned low, unsigned value) {
    assert(gate && !host && expect_patch);
    switch (++events) {
        case 2: assert(block==I2C_BIAS && reg==10 && high==7 && low==4 && value==12); break;
        case 3: assert(block==I2C_BIAS && reg==11 && high==3 && low==0 && value==12); break;
        case 5: assert(block==11 && reg==1 && high==1 && low==0 && value==3); break;
        case 6: assert(block==11 && reg==1 && high==2 && low==2 && value==1); break;
        case 8: assert(block==11 && reg==3 && high==5 && low==4 && value==3); break;
        case 9: assert(block==11 && reg==1 && high==2 && low==2 && value==0); break;
        default: assert(false);
    }
}
void set_field(unsigned reg, unsigned field, unsigned value) {
    assert(++events==7 && reg==20 && field==21 && value==9);
}
void esp_rom_delay_us(unsigned us) { assert(++events==4 && us==10); }
void init_write(unsigned block, unsigned reg, unsigned value) {
    assert(block==I2C_BIAS && value==12 && reg==10+(init_writes++ % 2));
}
void __real_rtc_clk_mpll_configure(uint32_t xtal, uint32_t frequency, bool owned) {
    assert(xtal==40 && frequency==400);
    if (expect_patch) { assert(++events==10 && owned); }
    else assert(!events && owned==owned_argument);
    ++calibrations;
}
uint32_t __real_pmu_sleep_start(uint32_t wake, uint32_t reject, uint32_t mem, bool deep) {
    assert(wake==1 && reject==2 && mem==3 && deep==deep_sleep);
    ++pmu_calls;
    __wrap_rtc_clk_mpll_configure(40,400,false);
    return 37;
}
esp_err_t esp_light_sleep_start(void) {
    if (scenario==1) return -5;
    if (scenario==2) {
        // 模拟 retention 从 PMU 包装器外续跑；外层返回仍必须清理标记。
        // Simulate retention resuming outside the PMU wrapper; the outer return must still clear flags.
        s_pmu_light_sleep=true;
        return -6;
    }
    assert(__wrap_pmu_sleep_start(1,2,3,deep_sleep)==37);
    return scenario==3 ? -7 : ESP_OK;
}
static void outside_calibration(bool owned) {
    expect_patch=false; owned_argument=owned; events=0;
    __wrap_rtc_clk_mpll_configure(40,400,owned);
    assert(!events);
}
int main(void) {
    app_s31_sleep_init(); assert(init_writes==2);
    outside_calibration(false); outside_calibration(true);
    deep_sleep=false; expect_patch=false; owned_argument=false;
    assert(__wrap_pmu_sleep_start(1,2,3,false)==37 && !events);
    for (int initial_gate=0; initial_gate<2; ++initial_gate) {
        for (int next=0; next<4; ++next) {
            scenario=next; gate=initial_gate; events=0; expect_patch=next==0||next==3;
            unsigned before=pmu_calls;
            int result=app_s31_light_sleep_start();
            assert(result==(next==1?-5:next==2?-6:next==3?-7:0));
            assert(!s_explicit_sleep && !s_pmu_light_sleep && gate==(bool)initial_gate);
            assert(events==(expect_patch?11:0) && pmu_calls==before+(expect_patch?1:0));
            outside_calibration(false);
        }
    }
    deep_sleep=true; scenario=0; expect_patch=false; owned_argument=false; events=0;
    assert(app_s31_light_sleep_start()==ESP_OK && !events);
    outside_calibration(true);
    assert(calibrations>0);
    puts("PASS: actual S31 wrapper calibration order, gate preservation, normal/deep bypass, rejected sleep and retention-scope cleanup");
}
'''


def test_runtime(work):
    headers = ["esp_err.h", "esp_attr.h", "esp_sleep.h", "esp_rom_sys.h",
               "esp_private/regi2c_ctrl.h", "hal/regi2c_ctrl_ll.h", "hal/regi2c_impl.h",
               "soc/lp_clkrst_reg.h", "soc/regi2c_bias.h"]
    (work / "compat_stubs.h").write_text(STUBS)
    for name in headers:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "compat_stubs.h"\n')
    unit = work / "runtime.c"
    unit.write_text(f'#include "{COMPAT}"\n' + TEST)
    env, compiler = toolchain(work)
    binary = work / "runtime"
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                              "-fsanitize=address,undefined", "-I"+str(work),
                              str(unit), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)


def test_backport(work):
    entry = "s_mpll_freq_mhz_before_sleep = rtc_clk_mpll_get_freq();"
    exit_ = "rtc_clk_mpll_configure(clk_hal_xtal_get_freq_mhz(), s_mpll_freq_mhz_before_sleep, false);"
    legacy = entry + "\nwait_for_sleep();\n" + exit_ + "\n"
    sdk = work / "sdk/components/esp_hw_support/port/esp32s31/pmu_sleep.c"
    sdk.parent.mkdir(parents=True)
    harness = work / "backport.cmake"
    harness.write_text(f'''cmake_minimum_required(VERSION 3.22)
set(COMPONENT_LIB test)
function(target_sources)
endfunction()
function(target_link_options)
endfunction()
function(target_compile_definitions)
endfunction()
function(set_property)
endfunction()
function(get_target_property result)
    set(${{result}} "port/esp32s31/pmu_sleep.c;other.c" PARENT_SCOPE)
endfunction()
include("{BACKPORT}")
''')
    generated = work / "s31_pmu_sleep_compat.c"
    env = dict(os.environ, IDF_PATH=str(work / "sdk"))
    def run(source, success=True):
        sdk.write_text(source)
        generated.unlink(missing_ok=True)
        result = subprocess.run(["cmake", "-P", str(harness)], cwd=work,
                                env=env, capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        assert sdk.read_text() == source
        return generated.read_text() if generated.exists() else None
    patched = run(legacy)
    assert patched.count("rtc_clk_mpll_disable();") == 1
    assert patched.index("PSRAM_CLK_SRC_XTAL") < patched.index("rtc_clk_mpll_disable();") < patched.index("wait_for_sleep();")
    assert patched.index(exit_) < patched.index("PSRAM_CLK_SRC_MPLL")
    assert "if (!s_pmu_sleep_regdma_backup_enabled)" in patched
    assert run(patched) is None
    for source in (exit_, entry, legacy+entry, legacy+exit_):
        assert run(source, False) is None
    print("PASS: executable CMake legacy backport, clock ordering, idempotence, immutable SDK, missing/repeated anchor rejection")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="s31-sleep-") as folder:
        work = Path(folder)
        test_runtime(work)
        test_backport(work)
