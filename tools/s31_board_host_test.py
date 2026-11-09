#!/usr/bin/env python3
"""中文：执行实际 S31 后端，验证显示、电源与睡眠的资源边界。
English: Exercise the actual S31 backend across display, power and sleep boundaries.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "components/board_support/board_metalio_e4p.c"
source = BOARD.read_text()
# 保留实际引脚和寄存器定义，合并多行宏后提取。/ Keep real pins/registers and join continued macros before extraction.
defines = "\n".join(line for line in source.replace("\\\n", "").splitlines()
                    if re.match(r"#define (?:IOE_|IO_|P0_|P1_|TPS_REG_|BOARD_TCA_ADDR\b|BOARD_IOE_INT\b|EPD_PIN_XOE\b|EPD_PIN_MODE\b)", line))
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef struct { bool ep_mode, ep_output_enable, ep_stv; } epd_ctrl_state_t;
typedef struct { int i2c_addr, scl_speed_hz, int_gpio; } fca9555_config_t;
#define FCA9555_CONFIG_DEFAULT() ((fca9555_config_t){0})
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define TAG "test"
#define GPIO_NUM_18 18
#define GPIO_NUM_19 19
#define GPIO_NUM_2 2
#define pdMS_TO_TICKS(n) (n)
static const char *esp_err_to_name(int value){(void)value;return "test";}
static void test_log(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
static int64_t s_diag_power_on_us, s_diag_power_off_us;
static void *s_ioe = (void *)1, *s_tps = (void *)2;
static void *s_i2c_bus = (void *)3;
static bool s_ioe_ok;
static atomic_bool s_tps_ok, s_rails_on, s_pwrup, s_vcom_ctrl, s_wakeup;
static atomic_uchar s_p0_extra;
static uint8_t s_p1_out, output[2], config[2], inversion[2];
static int s_vcom_mv = 1200;
static unsigned writes, reads, clears, waits, probes, tps_writes;
static int fail_config = -1;
static bool failed_output, pgood, failed_probe, chip_pgood;
static int fail_tps_read = -1, fail_tps_write = -1;
static unsigned restarts, xoe_on, order_count, transient_probes;
static uint8_t order[32];
static int init_fault_reg = -1, init_failures;
static unsigned init_allocations, bus_resets;
static bool allocation_failure;
static bool init_write_fails(int reg) {
    if (reg != init_fault_reg || !init_failures) return false;
    --init_failures; return true;
}
static int fca9555_init(void *bus, const fca9555_config_t *cfg, void **out) {
    assert(bus == s_i2c_bus && !*out && cfg->i2c_addr == 0x20 && cfg->scl_speed_hz == 100000 && cfg->int_gpio == 2);
    ++init_allocations; if (allocation_failure) return ESP_FAIL;
    *out = (void *)1; return ESP_OK;
}
static int i2c_master_bus_reset(void *bus) {
    assert(bus == s_i2c_bus && !s_ioe_ok); ++bus_resets; return ESP_OK;
}
static int pins[64];
static unsigned shutdown_us;
static bool checking_shutdown;
static void esp_rom_delay_us(unsigned us) { if (checking_shutdown) { assert(!pins[18] && !pins[19]); assert(s_pwrup && s_vcom_ctrl); shutdown_us += us; } }
static epd_ctrl_state_t state;
static epd_ctrl_state_t *epd_ctrl_state(void) { return &state; }
static int fca9555_set_output(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2); ++writes;
    if (failed_output || init_write_fails(2 + port)) return ESP_FAIL;
    if (checking_shutdown && port == 0 && !(value & 2)) assert(shutdown_us >= 12);
    output[port] = value; return ESP_OK;
}
static int fca9555_set_config(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2);
    if (port == fail_config || init_write_fails(6 + port)) return ESP_FAIL;
    config[port] = value; return ESP_OK;
}
static int fca9555_set_inversion(void *h, int port, uint8_t value) {
    assert(h == s_ioe && port >= 0 && port < 2);
    if (init_write_fails(4 + port)) return ESP_FAIL;
    inversion[port] = value; return ESP_OK;
}
static int fca9555_read_reg(void *h, uint8_t reg, uint8_t *value) {
    assert(h == s_ioe && (reg == 0 || reg == 2) && value); if(reg==0)++reads;
    *value = reg==2 ? output[0] : (pgood ? 0x80 : 0); return ESP_OK;
}
static int fca9555_clear_int(void *h) { assert(h == s_ioe); ++clears; return ESP_OK; }
static void gpio_set_level(int pin, int level) { assert(pin >= 0 && pin < 64); pins[pin] = level; if (pin == 18 && level) ++xoe_on; }
static void vTaskDelay(unsigned ms) { waits += ms; }
static int64_t esp_timer_get_time(void){return (int64_t)waits*1000;}
static int tps_probe(void) {
    ++probes; if(transient_probes){--transient_probes;return ESP_FAIL;}
    return failed_probe ? ESP_FAIL : ESP_OK;
}
static int tps_read(uint8_t reg, uint8_t *value) {
    if (reg == fail_tps_read) return ESP_FAIL;
    *value = chip_pgood ? 0xfa : 0; return ESP_OK;
}
static int tps_write(uint8_t reg, uint8_t value) {
    (void)value; ++tps_writes; if (order_count < sizeof(order)) order[order_count++]=reg;
    return reg == fail_tps_write ? ESP_FAIL : ESP_OK;
}
static void esp_restart(void) { ++restarts; }
static int board_i2c_lock(int timeout) {(void)timeout; return ESP_OK;}
static void board_i2c_unlock(void) {}
static void board_poweroff(epd_ctrl_state_t*);
'''
unit += defines + "\n"
for name in ("ioe_write", "ioe_read", "p0_value", "p0_write", "p1_write", "ioe_init",
             "power_diag_failed", "board_set_ctrl", "tps_set_vcom_mv", "board_poweron_once", "board_poweron", "board_poweroff_checked", "board_poweroff", "board_set_vcom",
             "board_epd_release_drive", "board_sd_detect",
             "board_sd_present", "board_sd_has_cd", "board_ioe_sleep",
             "board_ioe_wake", "board_tp_rst", "board_touch_reset",
             "board_pwr_pulse", "board_has_motor", "board_motor"):
    unit += function(name, BOARD) + "\n"
unit += r'''
static void reset(void) {
    s_ioe_ok = true; s_tps_ok = s_rails_on = false;
    s_pwrup = s_vcom_ctrl = s_wakeup = false;
    s_p0_extra = IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN);
    s_p1_out = P1_RAIL_MASK;
    memset(output, 0, sizeof(output)); memset(config, 0, sizeof(config));
    memset(inversion, 0, sizeof(inversion)); memset(pins, 0, sizeof(pins));
    writes = reads = clears = waits = probes = tps_writes = 0;
    fail_config = fail_tps_read = fail_tps_write = -1;
    failed_output = failed_probe = false; pgood = chip_pgood = true;
    restarts = xoe_on = order_count = transient_probes = 0; s_diag_power_on_us = s_diag_power_off_us = 0; s_vcom_mv = 1200;
    init_fault_reg = -1; init_failures = 0;
    init_allocations = bus_resets = 0; allocation_failure = false;
    s_ioe = (void *)1;
    s_tps=(void*)2; state = (epd_ctrl_state_t){0};
}
int main(void) {
    (void)i2c_master_bus_reset;
    // 每个配置寄存器及最后的 OUT0 都必须成功；暂时故障重试但只分配一个句柄。
    // Every configuration register and final OUT0 must succeed; transient retries allocate one handle only.
    for (int reg = 2; reg <= 7; ++reg) for (int failures = 0; failures <= 3; ++failures) {
        reset(); s_ioe = NULL; s_ioe_ok = false;
        init_fault_reg = reg; init_failures = failures;
        int result = ioe_init();
        assert(init_allocations == 1 && bus_resets == (unsigned)(failures < 3 ? failures : 2));
        assert(waits == bus_resets * 10);
        if (failures < 3) {
            assert(result == ESP_OK && s_ioe_ok && clears == 1);
            assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
            assert(!inversion[0] && !inversion[1]);
            assert(output[0] == IOE_BIT(IO_CAM_SCR) && output[1] == P1_RAIL_MASK);
            assert(!s_pwrup && !s_vcom_ctrl && !s_wakeup);
        } else {
            assert(result == ESP_FAIL && !s_ioe_ok && !clears);
            // 持续故障后仍保留单一所有权，下次初始化可恢复且不重复分配。
            // Retain one owned handle after persistent failure; a later initialization recovers without reallocating.
            assert(ioe_init() == ESP_OK && s_ioe_ok && init_allocations == 1);
        }
    }
    reset(); s_ioe = NULL; s_ioe_ok = false; allocation_failure = true;
    assert(ioe_init() == ESP_FAIL && !s_ioe_ok && !s_ioe && init_allocations == 1 && !bus_resets);
    reset(); assert(board_epd_release_drive() && !writes && !tps_writes);
    board_poweron(&state);
    assert(s_rails_on && s_tps_ok && s_pwrup && s_vcom_ctrl && s_wakeup);
    assert((output[0] & P0_TPS_MASK) == P0_TPS_MASK && pins[EPD_PIN_XOE]);
    assert(probes == 1 && waits == 100 && !restarts && xoe_on == 1);
    assert(order_count == 5 && order[0] == TPS_REG_UPSEQ0 && order[1] == TPS_REG_UPSEQ1 &&
           order[2] == TPS_REG_ENABLE && order[3] == TPS_REG_VCOM2 && order[4] == TPS_REG_VCOM1);
    unsigned before = writes, tps_before = tps_writes;
    board_set_vcom(-1300); assert(s_vcom_mv == 1300 && tps_writes == tps_before);
    board_poweron(&state); assert(writes == before && tps_writes == tps_before);
    // S31 扫描收尾只撤 MODE/XOE，VCOM 与其他锁存保持到真正关电。
    // S31 output completion clears MODE/XOE while VCOM and other latches remain until power-off.
    state.ep_mode = true;
    board_set_ctrl(&state, &(epd_ctrl_state_t){.ep_mode = true});
    uint8_t keep = output[0];
    for (int i = 0; i < 100; ++i) {
        assert(board_epd_release_drive());
        assert(!state.ep_mode && !state.ep_output_enable);
        assert(!pins[EPD_PIN_MODE] && !pins[EPD_PIN_XOE]);
        assert(s_rails_on && s_vcom_ctrl && output[0] == keep && tps_writes == tps_before);
    }
    state.ep_mode = state.ep_output_enable = true;
    board_set_ctrl(&state, &(epd_ctrl_state_t){.ep_mode = true});
    assert(pins[EPD_PIN_MODE] && !pins[EPD_PIN_XOE]);
    checking_shutdown = true; shutdown_us = 0;
    board_poweroff(&state);
    checking_shutdown = false; assert(shutdown_us >= 12);
    assert(!s_rails_on && !s_tps_ok && !(output[0] & P0_TPS_MASK));
    assert((output[0] & P0_OUTPUT_MASK) == s_p0_extra && !pins[EPD_PIN_XOE] && !pins[EPD_PIN_MODE]);
    // 关断失败不能谎报关闭；后续马达写入也只能提交最新的关断位。
    // Failed shutdown cannot claim power-off; later motor writes must use the latest cleared power bits.
    reset(); board_poweron(&state); failed_output = true;
    assert(!board_poweroff_checked(&state) && s_rails_on && !s_tps_ok);
    failed_output = false;
    assert(board_motor(true) == ESP_OK && !(output[0] & P0_TPS_MASK));
    assert(board_poweroff_checked(&state) && !s_rails_on);
    // PGOOD 失效的等待有上界；不上报轨已就绪，且关电仍能撤掉请求。
    // Bound failed PGOOD waits; never publish ready rails, and power-off still clears requests.
    for (int fault=0; fault<12; ++fault) {
        reset();
        switch(fault) {
            case 0: failed_probe=true; break;
            case 1: pgood=false; break;
            case 2: chip_pgood=false; break;
            case 3: fail_tps_read=TPS_REG_REVID; break;
            case 4: fail_tps_read=TPS_REG_PG; break;
            case 5: fail_tps_write=TPS_REG_UPSEQ0; break;
            case 6: fail_tps_write=TPS_REG_UPSEQ1; break;
            case 7: fail_tps_write=TPS_REG_ENABLE; break;
            case 8: fail_tps_write=TPS_REG_VCOM2; break;
            case 9: fail_tps_write=TPS_REG_VCOM1; break;
            case 10: failed_output=true; break;
            case 11: s_tps=NULL; break;
        }
        board_poweron(&state);
        assert(s_rails_on == (fault==10) && !s_tps_ok && !pins[EPD_PIN_XOE] && !xoe_on && restarts==1);
        assert(!state.ep_output_enable && !state.ep_mode && !s_pwrup && !s_wakeup && !s_vcom_ctrl);
        if (fault==0 || fault==3 || (fault>=5 && fault<=9)) assert(probes==4);
        if (fault==1) assert(reads==800+4*CONFIG_DISPLAY_POWER_DIAG && waits==1508);
        if (fault==2 || fault==4) assert(waits==1508);
        s_tps=(void*)2;
    }
    reset();transient_probes=2;board_poweron(&state);
    assert(s_rails_on && s_tps_ok && probes==3 && !restarts && xoe_on==1 && waits==404);
    for (int host_off = 0; host_off < 2; ++host_off) {
        reset(); board_poweron(&state); assert(board_motor(true) == ESP_OK);
        s_p1_out |= IOE_BIT(IO_PWR_PULSE);
        assert(board_ioe_sleep(host_off) == ESP_OK);
        assert(!(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
        assert((output[0] & (IOE_BIT(IO_PA_EN) | IOE_BIT(IO_CAM_SCR))) == s_p0_extra);
        assert(!(output[1] & (IOE_BIT(IO_PWR_PULSE) | IOE_BIT(IO_BT_PA_PWR))));
        assert(output[1] & IOE_BIT(IO_TP_RST));
        assert(config[1] == P1_INPUT_MASK);
        // 重复休眠及马达关断不得重新打开 P14；唤醒才恢复。
        // Repeated parking and motor-off writes must leave P14 off until wake.
        assert(board_ioe_sleep(host_off) == ESP_OK && board_motor(false) == ESP_OK);
        assert(!(output[1] & IOE_BIT(IO_BT_PA_PWR)) && (output[1] & IOE_BIT(IO_TP_RST)));
        assert(board_ioe_wake() == ESP_OK);
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
        assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK && clears == 3);
    }
    // 缺失扩展器与方向写入失败不假报唤醒成功。
    // Missing expanders and failed direction writes must not report a successful wake.
    reset(); s_ioe_ok = false;
    assert(board_ioe_sleep(false) == ESP_ERR_INVALID_STATE);
    assert(board_ioe_wake() == ESP_ERR_INVALID_STATE);
    assert(board_motor(true) == ESP_ERR_INVALID_STATE && !writes);
    for (int port = 0; port < 2; ++port) {
        reset(); fail_config = port; assert(board_ioe_wake() == ESP_FAIL);
        fail_config = -1; assert(board_ioe_wake() == ESP_OK);
    }
    reset(); board_poweron(&state); keep = output[0];
    assert(board_has_motor() && board_motor(true) == ESP_OK);
    assert(output[0] == (uint8_t)(keep | IOE_BIT(IO_MOTOR)));
    assert(board_motor(false) == ESP_OK && output[0] == keep);
    failed_output = true; assert(board_motor(true) == ESP_FAIL); failed_output = false;
    unsigned delay_before = waits;
    assert(board_touch_reset() == ESP_OK && waits - delay_before == 310);
    assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
    assert(board_pwr_pulse(true) == ESP_OK && (output[1] & IOE_BIT(IO_PWR_PULSE)));
    assert(board_pwr_pulse(false) == ESP_OK && !(output[1] & IOE_BIT(IO_PWR_PULSE)));
    // S31 没有 CD，存在接口不代表物理卡在线；存储层另测 CMD13 拔卡。
    // S31 has no CD; presence hooks do not prove physical media. Storage tests cover CMD13 removal.
    bool present = false;
    assert(board_sd_detect(NULL) == ESP_ERR_INVALID_ARG);
    assert(board_sd_detect(&present) == ESP_OK && present);
    assert(board_sd_present() && !board_sd_has_cd());
    puts("PASS: actual S31 VCOM retention, masked GPIO, power timeout, motor isolation, sleep/wake rails, touch reset, pulse and no-CD contract");
}
'''
with tempfile.TemporaryDirectory(prefix="s31-board-") as folder:
    c, binary = Path(folder) / "test.c", Path(folder) / "test"
    c.write_text(unit)
    for diag in (0,1):
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-Wno-unused-variable", f"-DCONFIG_DISPLAY_POWER_DIAG={diag}",
                        "-fsanitize=address,undefined", str(c), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
