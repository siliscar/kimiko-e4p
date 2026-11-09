#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行 S31 板级收口与应用下电路径，验证外设恢复和失败边界。
# English: Exercise S31 board parking and application power-off, including peripheral restore and failure boundaries.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
board = ROOT / 'components/board_support/board_metalio_e4p.c'
sleep = ROOT / 'main/power/sleep.c'
text = board.read_text()
defines = '\n'.join(line for line in text.replace('\\\n', '').splitlines()
                    if re.match(r'#define (?:IOE_|IO_|P0_|P1_)', line))
unit = r'''
static int board_i2c_lock(int timeout) {(void)timeout; return 0;}
static void board_i2c_unlock(void) {}
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
typedef void *cst836u_handle_t;
typedef void *sc7a20h_handle_t;
typedef int app_sleep_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_TIMEOUT -4
#define ESP_ERR_NOT_SUPPORTED -5
#define APP_SLEEP_LIGHT 0
#define APP_SLEEP_DEEP 1
#define APP_SLEEP_OFF 2
#define CST836U_MODE_NORMAL 0
#define CST836U_MODE_DEEPSLEEP 3
#define HOST_EN_DROP_WAIT_MS 5000
#define CONFIG_PM_ESP_SLEEP_POWER_DOWN_CPU 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static void *s_ioe = (void *)1;
static bool s_ioe_ok, s_pwrup, s_vcom_ctrl, s_wakeup;
static uint8_t s_p0_extra, s_p1_out;
static bool rails_on;
static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;
static uint8_t config[2], output[2];
static int step, fault, clears;
static bool buzzer, acc_on, transfer_ok = true, ota;
static bool cover_ok = true, avatar_ok = true;
static bool book_cover_jobs_quiesce(unsigned ms) { assert(ms == 2000); return cover_ok; }
static bool ui_avatar_quiesce(unsigned ms) { assert(ms == 2000); return avatar_ok; }
static int tp_mode, touch_wakes, acc_ups, acc_downs, cpu_requests;
#define DEVICE_POWER_BATT_LOG_OFF 512
static void device_power_batt_log_event(int event) { assert(event == DEVICE_POWER_BATT_LOG_OFF); }
static bool device_power_batt_log_flush(unsigned ms) { assert(ms == 250); return true; }
static int off_calls, deep_calls, pauses, resumes, syncs, prepared, radio_stops;
static int error_result, waits;
static bool soft_supported;
static bool key_sampling=true;
static bool device_power_key_sampler_enable(bool enabled){bool previous=key_sampling;key_sampling=enabled;return previous;}
static esp_err_t operation(void) { return ++step == fault ? ESP_FAIL : ESP_OK; }
static esp_err_t fca9555_set_output(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) output[p] = v; return e;
}
static esp_err_t fca9555_set_config(void *h, int p, uint8_t v) {
    assert(h == s_ioe); esp_err_t e = operation(); if (!e) config[p] = v; return e;
}
static esp_err_t fca9555_set_inversion(void *h, int p, uint8_t v) {
    (void)h; (void)p; (void)v; return operation();
}
static esp_err_t fca9555_clear_int(void *h) { assert(h == s_ioe); ++clears; return ESP_OK; }
static bool board_buzzer_score_busy(void) { return buzzer; }
static int board_buzzer_score_stop(void) { buzzer = false; return ESP_OK; }
static bool sc7a20h_powered(void *h) { return h && acc_on; }
static int sc7a20h_power_down(void *h) { assert(h == s_acc); acc_on = false; ++acc_downs; return ESP_OK; }
static int sc7a20h_power_up(void *h) { assert(h == s_acc); acc_on = true; ++acc_ups; return ESP_OK; }
static int cst836u_get_mode(void *h) { assert(h == s_tp); return tp_mode; }
static int cst836u_set_mode(void *h, int m) { assert(h == s_tp); tp_mode = m; return ESP_OK; }
static int cst836u_wake(void *h);
static int sleep_cpu_configure(bool enabled) { assert(enabled); ++cpu_requests; return ESP_OK; }
static void app_s31_sleep_init(void) {}
static int trace_replays;
static void sleep_trace_replay(void) { ++trace_replays; }
static bool pico_online_busy(void) { return ota; }
static bool wifi_transfer_pause_for_sleep(void) { ++pauses; return transfer_ok; }
static void wifi_transfer_resume_after_sleep(void) { ++resumes; }
static int ble_pt_stop(int timeout) { assert(timeout == 2000); ++radio_stops; return ESP_OK; }
static int fl_pauses, fl_resumes;
static void frontlight_suspend(void) { ++fl_pauses; }
static void frontlight_resume(void) { ++fl_resumes; }
static int app_sync_time_to_pmu(void) { ++syncs; return ESP_OK; }
static void app_weather_cancel_join(void){}
static void app_time_service_cancel(void) {}
static void pico_ota_auto_cancel(void) {}
static void pmu_selftest_prepare_powerdown(void) { ++prepared; rails_on = false; }
static int pmu_result(void);
static int device_power_power_off(void) { ++off_calls; return pmu_result(); }
static int device_power_report_sleep(void) {
    ++deep_calls;
    return soft_supported ? pmu_result() : ESP_ERR_NOT_SUPPORTED;
}
static void vTaskDelay(int ms) { waits += ms; }
'''
unit += defines + '\n'
for name in ('ioe_write', 'p0_value', 'p0_write', 'p1_write', 'board_ioe_sleep', 'board_ioe_wake'):
    unit += function(name, board) + '\n'
unit += r'''
static int cst836u_wake(void *h) {
    assert(h == s_tp && config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    assert((output[1] & P1_RAIL_MASK) == P1_RAIL_MASK);
    tp_mode = CST836U_MODE_NORMAL; ++touch_wakes; return ESP_OK;
}
static int pmu_result(void) {
    assert(!key_sampling && prepared && !rails_on && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
    assert(config[1] == P1_INPUT_MASK && !(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
    assert(!(output[1] & IOE_BIT(IO_PWR_PULSE)));
    return error_result;
}
'''
unit += function('storage_jobs_quiesce', ROOT / 'main/services/storage_jobs.c') + '\n'
unit += function('app_lifecycle_prepare_poweroff', ROOT / 'main/app/app_lifecycle.c') + '\n'
for name in ('app_sleep_bind', 'periph_suspend', 'periph_resume', 'app_enter_host_sleep'):
    unit += function(name, sleep) + '\n'
unit += r'''
static void reset(void) {
    key_sampling=true;
    s_ioe = (void *)1; s_ioe_ok = true; step = fault = clears = 0;
    config[0] = P0_INPUT_MASK; config[1] = P1_INPUT_MASK;
    s_pwrup = s_vcom_ctrl = s_wakeup = true;
    s_p0_extra = IOE_BIT(IO_MOTOR) | IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN);
    output[0] = p0_value();
    s_p1_out = output[1] = P1_RAIL_MASK | IOE_BIT(IO_PWR_PULSE);
    rails_on = false; buzzer = acc_on = true; s_acc_was_on = false;
    tp_mode = CST836U_MODE_NORMAL;
    touch_wakes = acc_ups = acc_downs = off_calls = deep_calls = pauses = resumes = 0;
    syncs = prepared = radio_stops = waits = 0; error_result = ESP_FAIL;
    transfer_ok = true; ota = false; soft_supported = false;
    fl_pauses = fl_resumes = 0;
}
int main(void) {
    reset(); app_sleep_bind((void *)2, (void *)3); assert(cpu_requests == 1 && trace_replays == 1);
    // 浅睡与下电均关闭 P14、马达和关机脉冲；P12 触摸复位保持高。
    // Both light sleep and power-off cut P14, motor and power pulse while keeping touch reset P12 high.
    for (int off = 0; off < 2; ++off) {
        reset(); periph_suspend(off);
        assert(!buzzer && !acc_on && tp_mode == CST836U_MODE_DEEPSLEEP);
        assert(config[1] == P1_INPUT_MASK && (output[1] & IOE_BIT(IO_TP_RST)));
        assert(!(output[1] & (IOE_BIT(IO_BT_PA_PWR) | IOE_BIT(IO_PWR_PULSE))));
        assert(!(output[0] & (P0_TPS_MASK | IOE_BIT(IO_MOTOR))));
        assert((output[0] & (IOE_BIT(IO_CAM_SCR) | IOE_BIT(IO_PA_EN))) != 0);
        periph_resume();
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
        assert(acc_on && acc_ups == 1 && touch_wakes == 1 && tp_mode == CST836U_MODE_NORMAL);
        assert(fl_pauses == 1 && fl_resumes == 1);
    }
    reset(); acc_on = false; periph_suspend(false); periph_resume(); assert(!acc_on && !acc_ups);
    reset(); s_ioe_ok = false;
    assert(board_ioe_sleep(true) == ESP_ERR_INVALID_STATE);
    assert(board_ioe_wake() == ESP_ERR_INVALID_STATE && !step);
    // TCA 唤醒配置失败返回错误；故障消失后能够重新恢复方向和供电锁存。
    // TCA wake configuration faults return errors; clearing the fault permits direction and rail restoration.
    reset(); fault = 1; assert(board_ioe_wake() == ESP_FAIL);
    fault = 0; assert(board_ioe_wake() == ESP_OK);
    assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    // S31 不支持 PMU 软睡：只尝试一次，再转入关机路径；失败后恢复外设。
    // S31 has no PMU soft-sleep handoff: try once, escalate to power-off, and restore peripherals on failure.
    for (int mode = APP_SLEEP_DEEP; mode <= APP_SLEEP_OFF; ++mode) for (int ack = 0; ack < 2; ++ack) {
        reset(); rails_on = true; error_result = ack ? ESP_OK : ESP_FAIL;
        assert(app_enter_host_sleep(mode) == (ack ? ESP_ERR_TIMEOUT : ESP_FAIL));
        assert(key_sampling);
        assert(prepared == 1 && syncs == 1 && radio_stops == 1 && resumes == 1 && pauses == 1);
        assert(off_calls == 1 && deep_calls == (mode == APP_SLEEP_DEEP));
        assert(waits == (ack ? 5000 : 0));
        assert(acc_on && tp_mode == CST836U_MODE_NORMAL);
        assert(config[0] == P0_INPUT_MASK && config[1] == P1_INPUT_MASK);
    }
    // 通用应用交接仍保留有界重试，成功确认却未掉电时按超时恢复。
    // The shared application handoff keeps bounded retries and restores on an acknowledged but stuck power-off.
    reset(); soft_supported = true;
    assert(app_enter_host_sleep(APP_SLEEP_DEEP) == ESP_FAIL && deep_calls == 3 && !off_calls && waits == 400);
    reset(); transfer_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !prepared && !step);
    reset(); ota = true; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared);
    reset(); cover_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared); cover_ok = true;
    reset(); avatar_ok = false; assert(app_enter_host_sleep(APP_SLEEP_OFF) == ESP_ERR_INVALID_STATE && !pauses && !prepared); avatar_ok = true;
    reset(); assert(app_enter_host_sleep(APP_SLEEP_LIGHT) == ESP_ERR_INVALID_ARG && !pauses && !prepared);
    assert(key_sampling);
    reset();key_sampling=false;assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_FAIL&&!key_sampling);
    puts("PASS: actual S31 peripheral parking/restore, motor isolation, touch/PA rails, TCA faults, soft-sleep escalation, bounded handoff and storage/OTA guards");
}
'''
# 单独执行实际 Metalio 关机脉冲，避免应用层替身掩盖底层失败或无限重试。
# Execute the real Metalio pulse train separately so application doubles cannot hide driver failures or unbounded retries.
power_backend = ROOT / 'components/device_power/device_power_metalio.c'
power_source = power_backend.read_text()
shutdown_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED -5
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGW(...) ((void)0)
#define DEVICE_POWER_BATT_LOG_OFF 512
static void device_power_batt_log_event(int event) { assert(event == DEVICE_POWER_BATT_LOG_OFF); }
static bool device_power_batt_log_flush(unsigned ms) { assert(ms == 250); return true; }
static unsigned edges, fail_edge, delays, elapsed, deep_sleeps;
static unsigned battery_saves;
static void batt_save_current(void) { assert(!edges); ++battery_saves; }
static bool charger_fault;
static esp_err_t cx_charger_park(void){return charger_fault ? ESP_FAIL : ESP_OK;}
static bool level;
static esp_err_t board_pwr_pulse(bool high) {
    ++edges;
    assert(high == ((edges & 1) != 0));
    if (edges == fail_edge) return ESP_FAIL;
    level = high;
    return ESP_OK;
}
static void vTaskDelay(unsigned ms) { assert(ms == 100); ++delays; elapsed += ms; }
static void esp_deep_sleep_start(void) { ++deep_sleeps; }
'''
shutdown_unit += '\n'.join(re.findall(r'^#define PWR_.*$', power_source, re.M)) + '\n'
for name in ('device_power_report_sleep', 'device_power_power_off'):
    shutdown_unit += function(name, power_backend) + '\n'
shutdown_unit += r'''
int main(void) {
    assert(device_power_report_sleep() == ESP_ERR_NOT_SUPPORTED && !edges);
    charger_fault=true;assert(device_power_power_off()==ESP_FAIL && !edges && !deep_sleeps);
    assert(!battery_saves);
    charger_fault=false;
    for (unsigned fault = 1; fault <= 9; ++fault) {
        edges = delays = elapsed = deep_sleeps = battery_saves = 0; level = false; fail_edge = fault;
        assert(device_power_power_off() == ESP_FAIL);
        assert(edges == fault && delays == fault - 1 && !deep_sleeps);
        assert(battery_saves==1);
    }
    edges = delays = elapsed = deep_sleeps = fail_edge = 0; level = false;
    assert(device_power_power_off() == ESP_OK);
    assert(edges == 300 && delays == 300 && elapsed == 30000 && deep_sleeps == 1 && !level);
    puts("PASS: actual Metalio unsupported soft sleep, pulse ordering, immediate error return, bounded trains and deep-sleep fallback");
}
'''
settings_header = (ROOT / 'main/settings/settings.h').read_text()
style_start = settings_header.index('typedef enum {', settings_header.index('/// 锁屏睡眠方式'))
style_enum = settings_header[style_start:settings_header.index('} app_shutdown_style_t;', style_start) + len('} app_shutdown_style_t;')]
timed_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
typedef void *sc7a20h_handle_t;
typedef struct {int unused;} EpdiyHighlevelState;
typedef enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TIMEOUT} app_wake_source_t;
#define APP_SLEEP_OFF 2
static int64_t now_us, seen_deadline;
static unsigned waits, saves, marks, offs;
static bool save_ok, off_success;
static app_wake_source_t next_wake;
static jmp_buf powered_off;
static uint8_t framebuffer[1];
static int64_t esp_timer_get_time(void){return now_us;}
static uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *hl){assert(hl);return framebuffer;}
static bool lock_screen_save(const uint8_t *fb){assert(fb==framebuffer&&waits==1);++saves;return save_ok;}
static int pico_boot_mark_frame_cached(void){assert(save_ok&&saves==1);++marks;return 0;}
static int app_enter_host_sleep(int mode){assert(mode==APP_SLEEP_OFF&&saves==1);++offs;if(off_success)longjmp(powered_off,1);return -1;}
static app_wake_source_t light_sleep_wait_until(sc7a20h_handle_t acc,int64_t deadline){
    (void)acc;++waits;seen_deadline=deadline;
    if(waits==1)return next_wake;
    assert(offs==1&&!deadline);
    return APP_WAKE_KEY;
}
'''
# 驱动模型已执行真实等待循环；此处执行真实策略层，隔离关机成功与失败的边界。
# The driver model runs the real waiting loop; run the actual policy here to isolate shutdown success/failure boundaries.
timed_unit += style_enum + '\nstatic app_shutdown_style_t s_shutdown_style;\n'
timed_unit += function('app_settings_shutdown_minutes', ROOT / 'main/settings/settings.c') + '\n'
for name in ('app_light_sleep_wait', 'app_lock_sleep_wait'):
    timed_unit += function(name, sleep) + '\n'
timed_unit += r'''
int main(void){
    EpdiyHighlevelState hl={0};
    const uint8_t durations[]={0,0,0,10,30,60};
    for(unsigned style=1;style<=5;++style)for(unsigned wake=APP_WAKE_KEY;wake<=APP_WAKE_PICKUP;++wake){
        s_shutdown_style=(app_shutdown_style_t)style;next_wake=(app_wake_source_t)wake;
        waits=saves=marks=offs=0;now_us=123456789;
        assert(app_lock_sleep_wait(&hl,NULL)==next_wake&&waits==1&&!offs&&!saves);
        assert(seen_deadline==(durations[style]?now_us+(int64_t)durations[style]*60000000:0));
        now_us+=60000000;waits=0;
        assert(app_lock_sleep_wait(&hl,NULL)==next_wake);
        assert(seen_deadline==(durations[style]?now_us+(int64_t)durations[style]*60000000:0));
    }
    for(unsigned style=3;style<=5;++style)for(unsigned cache=0;cache<2;++cache)for(unsigned success=0;success<2;++success){
        s_shutdown_style=(app_shutdown_style_t)style;next_wake=APP_WAKE_TIMEOUT;
        save_ok=cache;off_success=success;waits=saves=marks=offs=0;
        if(!setjmp(powered_off)){
            assert(app_lock_sleep_wait(&hl,NULL)==APP_WAKE_KEY&&!success);
            assert(waits==2&&seen_deadline==0);
        }else assert(success&&waits==1);
        assert(saves==1&&offs==1&&marks==cache);
    }
    puts("PASS: actual 10/30/60 shutdown policy, early wake/reset, lock cache, full-off success and failed-off locked wait");
}
'''
# 直接执行设置页实际绘制/命中分支，避免仅靠文本存在性断言。
# Execute the actual settings draw/hit branches rather than only checking that labels exist.
settings_page = ROOT / 'main/apps/app_device_settings.c'
def standby_branch(name):
    text = function(name, settings_page)
    start = text.index('    if (s_page == SETTINGS_POWER_SLEEP) {')
    return text[start:text.index('\n    if (s_page == ', start + 1)]

ui_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int x0,y0;} ui_gesture_event_t;
#define SETTINGS_POWER_SLEEP 1
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define EPD_DRAW_ALIGN_LEFT 0
#define APP_REDRAW_NONE 0
#define APP_REDRAW_PAGE 1
static int s_page=SETTINGS_POWER_SLEEP;
static unsigned cards,circles,changes,texts,buttons,selected_buttons;
static EpdRect boxes[3],button_boxes[3];
'''
ui_unit += style_enum + r'''
static app_shutdown_style_t s_shutdown_style;
#define chosen s_shutdown_style
static app_shutdown_style_t app_settings_shutdown_style(void){return chosen;}
'''
ui_unit += function('app_settings_shutdown_minutes', ROOT / 'main/settings/settings.c') + '\n'
ui_unit += function('lock_light_only', ROOT / 'main/app/app_loop.c') + '\n'
for name in ('standby_mode_rect', 'standby_duration_rect'):
    ui_unit += function(name, settings_page) + '\n'
ui_unit += r'''
static void app_settings_set_shutdown_style(app_shutdown_style_t style){chosen=style;++changes;}
static bool ui_rect_hit(EpdRect box,int x,int y){return x>=box.x&&x<box.x+box.width&&y>=box.y&&y<box.y+box.height;}
static void back_header(uint8_t *fb,const char *title){(void)fb;assert(!strcmp(title,"待机方式"));}
static void section(uint8_t *fb,int y,const char *title){(void)fb;(void)y;(void)title;}
static void settings_card(uint8_t *fb,EpdRect box,int radius,int gray,int border){
    (void)fb;(void)radius;(void)border;assert(cards<3);boxes[cards]=box;
    bool timed=chosen>=APP_SHUTDOWN_LIGHT_10;
    bool active=cards==2?timed:chosen==(app_shutdown_style_t)(cards+1);
    assert(box.x==36&&box.width==612&&box.y==300+(int)cards*154);
    assert(box.height==(cards==2&&timed?222:132)&&box.y+box.height<1112);
    assert(gray==(active?0xd0:UI_GRAY_WHITE));++cards;
}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){
    (void)fb;(void)x;(void)y;(void)align;(void)inv;assert(text&&text[0]);++texts;
    if(px==29){const char *labels[]={"彻底关机","仅浅睡","浅睡后关机"};assert(!strcmp(text,labels[cards-1]));}
}
static void epd_fill_circle(int x,int y,int r,int color,uint8_t *fb){(void)x;(void)r;(void)color;(void)fb;assert(y==300+(chosen>=APP_SHUTDOWN_LIGHT_10?2:(int)chosen-1)*154+66);++circles;}
static void ui_draw_button(uint8_t *fb,EpdRect box,const char *label,bool active){
    (void)fb;assert(chosen>=APP_SHUTDOWN_LIGHT_10&&buttons<3);
    const char *labels[]={"10 分钟","30 分钟","60 分钟"};
    assert(!strcmp(label,labels[buttons]));
    assert(box.x==64+(int)buttons*188&&box.y==738&&box.width==172&&box.height==66);
    assert(box.x>boxes[2].x&&box.x+box.width<boxes[2].x+boxes[2].width);
    assert(box.y>boxes[2].y&&box.y+box.height<boxes[2].y+boxes[2].height);
    assert(active==(chosen==(app_shutdown_style_t)(APP_SHUTDOWN_LIGHT_10+buttons)));
    button_boxes[buttons++]=box;if(active)++selected_buttons;
}
static void ui_nav_draw(uint8_t *fb,int tab){(void)fb;assert(tab==3);}
static void draw_standby(uint8_t *fb){
'''
ui_unit += standby_branch('render') + '\n}\nstatic int hit_standby(const ui_gesture_event_t *ev){int y=ev->y0;\n'
ui_unit += standby_branch('on_gesture') + '\nreturn APP_REDRAW_NONE;\n}\n'
ui_unit += r'''
int main(void){
    for(unsigned style=1;style<=5;++style){
        chosen=(app_shutdown_style_t)style;cards=circles=texts=buttons=selected_buttons=0;draw_standby(NULL);
        assert(lock_light_only()==(style>=APP_SHUTDOWN_LIGHT));
        assert(cards==3&&circles==1&&texts==6);
        assert(buttons==(style>=3?3u:0u)&&selected_buttons==(style>=3?1u:0u));
        for(unsigned i=0;i<3;++i){
            chosen=(app_shutdown_style_t)style;
            EpdRect b=boxes[i];ui_gesture_event_t e={b.x,b.y};changes=0;
            assert(hit_standby(&e)==APP_REDRAW_PAGE);
            assert(chosen==(app_shutdown_style_t)(i<2?i+1:style>=3?style:APP_SHUTDOWN_LIGHT_10));
            assert(changes==(i==2&&style>=3?0u:1u));
            chosen=(app_shutdown_style_t)style;
            e=(ui_gesture_event_t){b.x+b.width-1,b.y+b.height-1};assert(hit_standby(&e)==APP_REDRAW_PAGE);
            const ui_gesture_event_t misses[]={{b.x-1,b.y},{b.x+b.width,b.y},{b.x,b.y-1},{b.x,b.y+b.height}};
            for(unsigned j=0;j<4;++j){chosen=(app_shutdown_style_t)style;changes=0;assert(hit_standby(&misses[j])==APP_REDRAW_NONE&&!changes);}
        }
        for(unsigned i=0;i<3;++i){
            chosen=(app_shutdown_style_t)style;
            EpdRect b=standby_duration_rect(i);
            ui_gesture_event_t e={b.x+b.width/2,b.y+b.height/2};changes=0;
            if(style<3){assert(hit_standby(&e)==APP_REDRAW_NONE&&!changes);continue;}
            const ui_gesture_event_t hits[]={{b.x,b.y},{b.x+b.width-1,b.y+b.height-1},e};
            for(unsigned j=0;j<3;++j){
                chosen=(app_shutdown_style_t)style;changes=0;
                assert(hit_standby(&hits[j])==APP_REDRAW_PAGE&&changes==1&&chosen==(app_shutdown_style_t)(APP_SHUTDOWN_LIGHT_10+i));
            }
            const ui_gesture_event_t gaps[]={{b.x-1,b.y},{b.x+b.width,b.y},{b.x,b.y-1},{b.x,b.y+b.height}};
            for(unsigned j=0;j<4;++j){
                chosen=(app_shutdown_style_t)style;changes=0;(void)hit_standby(&gaps[j]);
                assert(!changes&&chosen==(app_shutdown_style_t)style);
            }
        }
    }
    puts("PASS: actual three standby modes, conditional inline durations, persisted selection, default entry and visible/hidden control hit boundaries");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    env, _ = toolchain(Path(folder))
    for name, source in (('handoff', unit), ('shutdown', shutdown_unit), ('timed', timed_unit), ('standby-ui', ui_unit)):
        c, exe = Path(folder) / (name + '.c'), Path(folder) / name
        c.write_text(source)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True, env=env)
