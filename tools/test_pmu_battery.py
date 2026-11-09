#!/usr/bin/env python3
"""中文：执行 Metalio 电池采样，验证读数有效性、充电来源和公共快照。
English: Exercise Metalio battery polling, sample validity, charging sources and public snapshots."""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
backend = root / 'components/device_power/device_power_metalio.c'
source = backend.read_text()
# 提取真实滤波参数与类型；采样流程直接执行产品实现。/ Extract real filter parameters/types and execute the product polling implementation.
defines = '\n'.join(line for line in source.splitlines()
                    if re.match(r'#define (?:BATT_|BQ_REG_|CX_REG_STATUS1\b|CX_CHG_STAT_TOPOFF\b)', line))
curve = re.search(r'^static const int32_t s_soc_curve\[.*?\};', source, re.M | re.S).group(0)
types = '\n'.join(re.search(r'typedef struct \{[^}]*\} ' + name + ';', source, re.S).group(0)
                  for name in ('batt_filter_t',))
code = r'''
#include "device_power.h"
#include "battery_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define ESP_FAIL -1
#define ESP_OK 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
'''
code += defines + '\n' + curve + '\n' + types + '\n'
code += r'''
static void *s_bq, *s_cx;
static pmu_snapshot_t s_snap;
static batt_filter_t s_batt_filter;
static batt_log_state_t s_batt_log;
static int s_batt_log_mux;
static int32_t s_batt_saved_permille;
static int32_t s_batt_persisted_permille;
static bool s_batt_charging_last;
static int64_t s_batt_last_save_us, s_batt_ic_us, s_batt_full_since_us;
static int64_t s_batt_poll_us;
static bool s_batt_poll_reusable;
static int8_t s_batt_ic_soc;
static int64_t now_us;
static int64_t batt_log_wall(void) { return 1793415600 + now_us / 1000000; }
static void batt_log_notify(void) {}
static uint16_t raw_mv;
static int16_t raw_ma;
static uint8_t charger_state;
static bool voltage_fail, current_fail, charger_fail;
static unsigned saves, soc_reads;
static void cx_charger_poll(int64_t now, bool have_v, uint16_t mv, bool have_i, int16_t ma,
                            bool have_status, uint8_t status) {
    (void)now; (void)have_v; (void)mv; (void)have_i; (void)ma; (void)have_status; (void)status;
}
static int64_t esp_timer_get_time(void) { return now_us; }
static void batt_save(int32_t value) { assert(value >= 0 && value <= 1000); ++saves; s_batt_persisted_permille=value; }
static int bq_read_soc(void) { ++soc_reads; return 63; }
static esp_err_t dev_read(void *dev, uint8_t reg, uint8_t *out, size_t len) {
    if (dev == s_cx) {
        assert(reg == CX_REG_STATUS1 && len == 1);
        if (charger_fail) return ESP_FAIL;
        *out = charger_state << 3; return ESP_OK;
    }
    assert(dev == s_bq && len == 2);
    uint16_t value;
    if (reg == BQ_REG_VOLTAGE) {
        if (voltage_fail) return ESP_FAIL;
        value = raw_mv;
    } else {
        assert(reg == BQ_REG_CURRENT);
        if (current_fail) return ESP_FAIL;
        value = (uint16_t)raw_ma;
    }
    out[0] = value & 0xff; out[1] = value >> 8; return ESP_OK;
}
'''
for name in ('bq_read_voltage', 'bq_read_current', 'batt_compensated_mv', 'batt_curve_soc', 'batt_filter_step', 'batt_full_now', 'batt_log_enabled', 'poll_battery'):
    code += function(name, backend) + '\n'
code += r'''
static void reset(void) {
    memset(&s_snap, 0, sizeof(s_snap)); s_snap.present = true;
    memset(&s_batt_filter, 0, sizeof(s_batt_filter));
    s_bq = (void *)1; s_cx = (void *)2;
    s_batt_saved_permille = s_batt_persisted_permille = -1; s_batt_full_since_us = -1;
    s_batt_poll_us = 0; s_batt_poll_reusable = false;
    s_batt_last_save_us = now_us = 0; s_batt_ic_us = -1; s_batt_ic_soc = -1;
    memset(&s_batt_log, 0, sizeof(s_batt_log));
    s_batt_charging_last = false;
    voltage_fail = current_fail = charger_fail = false;
    raw_mv = 4000; raw_ma = 0; charger_state = saves = soc_reads = 0;
}
int main(void) {
    reset(); assert(pmu_battery_percent(&s_snap) == -1);
    poll_battery();
    assert(s_snap.status_ok && !s_snap.quick_ok && pmu_battery_percent(&s_snap) == 70);
    assert(s_snap.battery_raw == 4000 && !pmu_battery_charging(&s_snap));
    // 无效电压和 I2C 失败清有效位，不能继续显示上次电量。
    // Invalid voltage or failed I2C clears validity instead of presenting the last percent.
    const uint16_t invalid[] = {0, 2499, 5001, 65535};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset(); poll_battery(); raw_mv = invalid[i]; poll_battery();
        assert(pmu_battery_percent(&s_snap) == -1);
    }
    reset(); poll_battery(); voltage_fail = true; poll_battery(); assert(pmu_battery_percent(&s_snap) == -1);
    reset(); s_bq = NULL; poll_battery(); assert(pmu_battery_percent(&s_snap) == -1);
    // CX 状态优先；读取失败或缺芯片时才用有符号电流判断充电。
    // CX status wins; signed current is the charging fallback when CX is absent or unreadable.
    reset(); raw_ma = 200; poll_battery(); assert(!pmu_battery_charging(&s_snap));
    reset(); charger_state = 1; raw_ma = -100; poll_battery(); assert(pmu_battery_charging(&s_snap));
    assert(s_snap.battery_mv == 4018);
    reset(); raw_mv = 4174; raw_ma = 868; poll_battery();
    assert(!pmu_battery_charging(&s_snap) && s_snap.battery_mv == 4024);
    reset(); charger_fail = true; raw_ma = 21; poll_battery(); assert(pmu_battery_charging(&s_snap));
    reset(); s_cx = NULL; raw_ma = -200; poll_battery(); assert(!pmu_battery_charging(&s_snap));
    assert(s_snap.battery_mv == 4036);
    reset(); charger_fail = current_fail = true; poll_battery();
    assert(!pmu_battery_charging(&s_snap) && pmu_battery_percent(&s_snap) == 70);
    // 充满对齐、恢复值只消费一次，日志关闭时不增加库仑计读数。
    // Full-charge alignment, one-time saved value consumption and no coulomb reads while logging is off.
    reset(); charger_state = CX_CHG_STAT_TOPOFF; raw_mv = 4400; poll_battery();
    assert(pmu_battery_percent(&s_snap) == 100 && saves == 1);
    reset(); raw_mv = 3900; s_batt_saved_permille = 540; poll_battery();
    assert(pmu_battery_percent(&s_snap) == 54 && s_batt_saved_permille == -1 && !soc_reads);
    batt_log_set_enabled(&s_batt_log, true, now_us, batt_log_wall());
    now_us = (int64_t)BATT_LOG_INTERVAL_MS * 1000; poll_battery();
    assert(soc_reads == 1 && s_batt_log.latest.ic_soc == 63);
    assert(s_batt_log.latest.up_us == now_us && s_batt_log.latest.ic_us == now_us);
    // 失败的采样也必须替换日志快照；不能保留旧电压或把无效电流当零电流。
    // Failed attempts must replace the log sample rather than retaining old voltage or treating invalid current as zero.
    now_us += 1000000; voltage_fail = true; current_fail = true; poll_battery();
    assert(!s_batt_log.latest.have_v && !s_batt_log.latest.have_i);
    assert(s_batt_log.latest.up_us == now_us && s_batt_log.latest.ocv_mv == -1);
    voltage_fail = current_fail = false; raw_ma = -200; now_us += 1000000; poll_battery();
    assert(s_batt_log.latest.have_v && s_batt_log.latest.have_i && s_batt_log.latest.ma == -200);
    assert(s_batt_log.latest.comp_mv == 3936 && s_batt_log.latest.curve_permille >= 0);
    assert(soc_reads == 1 && s_batt_log.latest.ic_us == now_us - 2000000);
    batt_log_set_enabled(&s_batt_log, false, now_us, batt_log_wall());
    now_us += BATT_LOG_PERIOD_US; poll_battery(); assert(soc_reads == 1);
    // 判满必须有连续有效采样；丢读数、长空白和持续放电均重新确认。
    // Full confirmation requires continuous valid samples; failures, gaps and discharge restart it.
    reset(); raw_mv=4370; raw_ma=10; charger_state=2; poll_battery();
    for (int i=1;i<=300;i++) {now_us=(int64_t)i*1000000;poll_battery();}
    assert(pmu_battery_percent(&s_snap)==100);
    reset(); raw_mv=4370; raw_ma=10; charger_state=2; poll_battery();
    now_us=300000000;poll_battery();assert(pmu_battery_percent(&s_snap)<100);
    for(int i=1;i<=299;i++){now_us+=1000000;poll_battery();}
    assert(pmu_battery_percent(&s_snap)<100);
    now_us+=1000000;poll_battery();assert(pmu_battery_percent(&s_snap)==100);
    reset(); raw_mv=4370; raw_ma=10; charger_state=2; poll_battery();
    now_us=1000000;current_fail=true;poll_battery();assert(s_batt_full_since_us==-1);
    current_fail=false;raw_ma=-100;now_us+=1000000;poll_battery();assert(s_batt_full_since_us==-1);
    // 累计变化提前保存，但不突破一分钟写入门限。
    // Save accumulated changes without crossing the one-minute write gate.
    reset();s_batt_persisted_permille=650;poll_battery();assert(!saves);
    now_us=59999999;poll_battery();assert(!saves);
    now_us=60000000;poll_battery();assert(saves==1 && s_batt_persisted_permille==695);
    now_us+=200000;poll_battery();assert(saves==1);
    // 公共快照的兼容有效位与边界仍保留，不再依赖旧 CW32 解析器。
    // Keep public snapshot validity/boundary compatibility without the old CW32 parser.
    s_snap.quick_ok = true; s_snap.qb_flags = 3; s_snap.qb_soc = 0; assert(pmu_battery_percent(&s_snap) == 0);
    s_snap.qb_soc = 1000; assert(pmu_battery_percent(&s_snap) == 100);
    s_snap.qb_soc = 1001; assert(pmu_battery_percent(&s_snap) == -1);
    s_snap.qb_soc = 500; s_snap.qb_flags = 1; assert(pmu_battery_percent(&s_snap) == -1);
    s_snap.qb_flags = 7; assert(pmu_battery_charging(&s_snap));
    assert(pmu_battery_percent(NULL) == -1 && !pmu_battery_charging(NULL));
    puts("PASS: actual Metalio battery polling, invalid samples, signed BQ current, CX precedence/fallback, full charge, saved state and snapshot compatibility");
}
'''
persist = r'''
#include "device_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED -2
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
static pmu_snapshot_t s_snap;
static int32_t s_batt_persisted_permille=-1, stored_soc=600;
static uint32_t stored_ver;
static unsigned opens, closes, commits, writes, restarts;
static int fault;
static int nvs_open(const char *name,int mode,nvs_handle_t *out) {
    assert(!strcmp(name,"pmu") && (mode==0 || mode==1));++opens;*out=1;return fault==1?ESP_FAIL:ESP_OK;
}
static int nvs_set_i32(nvs_handle_t h,const char *key,int32_t v) {
    assert(h==1 && !strcmp(key,"soc"));++writes;if(fault==2)return ESP_FAIL;stored_soc=v;return ESP_OK;
}
static int nvs_set_u32(nvs_handle_t h,const char *key,uint32_t v) {
    assert(h==1 && !strcmp(key,"soc_ver"));if(fault==3)return ESP_FAIL;stored_ver=v;return ESP_OK;
}
static int nvs_get_i32(nvs_handle_t h,const char *key,int32_t *v) {
    assert(h==1 && !strcmp(key,"soc"));*v=stored_soc;return fault==5?ESP_FAIL:ESP_OK;
}
static int nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *v) {
    assert(h==1 && !strcmp(key,"soc_ver"));*v=stored_ver;return fault==6?ESP_FAIL:ESP_OK;
}
static int nvs_commit(nvs_handle_t h) {assert(h==1);++commits;return fault==4?ESP_FAIL:ESP_OK;}
static void nvs_close(nvs_handle_t h) {assert(h==1);++closes;}
static void device_power_batt_log_event_stub(device_power_batt_log_event_t e) {assert(e==DEVICE_POWER_BATT_LOG_RESTART);}
static bool device_power_batt_log_flush_stub(uint32_t ms) {assert(ms==250);return true;}
#define device_power_batt_log_event device_power_batt_log_event_stub
#define device_power_batt_log_flush device_power_batt_log_flush_stub
esp_err_t device_power_power_off(void) {return ESP_OK;}
static void esp_restart(void) {++restarts;}
'''
persist += defines + '\n'
for name in ('batt_save', 'batt_load', 'batt_save_current', 'device_power_action'):
    persist += function(name, backend) + '\n'
persist += r'''
int main(void) {
    assert(batt_load()==-1); // 不恢复旧曲线 / do not restore the old curve
    stored_ver=BATT_STATE_VERSION+1;assert(batt_load()==-1);
    stored_ver=BATT_STATE_VERSION;assert(batt_load()==600);
    stored_soc=-1;assert(batt_load()==-1);stored_soc=1001;assert(batt_load()==-1);
    stored_soc=600;
    for(int f=1;f<=6;f++){if(f!=1 && f!=5 && f!=6)continue;fault=f;assert(batt_load()==-1);}
    fault=0;batt_save(650);assert(s_batt_persisted_permille==650 && batt_load()==650);
    unsigned before=opens;batt_save(650);batt_save(-1);batt_save(1001);assert(opens==before);
    for(int f=1;f<=4;f++) {
        fault=f;s_batt_persisted_permille=-1;batt_save(700);
        assert(s_batt_persisted_permille==-1);
        fault=0;batt_save(700);assert(s_batt_persisted_permille==700 && batt_load()==700);
    }
    s_snap.soc_permille=720;before=opens;batt_save_current();assert(opens==before);
    s_snap.flags=PMU_STATUS_BATTERY_VALID;batt_save_current();assert(batt_load()==720);
    s_snap.soc_permille=730;
    assert(device_power_action(PMU_ACTION_HOST_NORMAL_RESTART,0,0)==ESP_OK);
    assert(restarts==1 && batt_load()==730);
    s_snap.soc_permille=740;
    assert(device_power_action(PMU_ACTION_HOST_HARD_RESET,0,0)==ESP_OK);
    assert(restarts==2 && batt_load()==740);
    assert(device_power_action(255,0,0)==ESP_ERR_NOT_SUPPORTED && restarts==2);
    assert(writes && commits && closes<opens);
    puts("PASS: battery persistence, curve revision, invalid data, write failures/retries, write deduplication and restart checkpoint");
}
'''
with tempfile.TemporaryDirectory() as folder:
    out = Path(folder)
    (out / 'driver').mkdir()
    (out / 'driver/i2c_master.h').write_text('#pragma once\ntypedef void *i2c_master_bus_handle_t;\n')
    (out / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    c, binary = out / 'test.c', out / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + folder, '-I' + str(root / 'components/device_power/include'),
                    '-I' + str(root / 'components/device_power'),
                    str(c), str(root / 'components/device_power/battery_log.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    c.write_text(persist)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + folder, '-I' + str(root / 'components/device_power/include'),
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
