/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "platform/s31_input.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "boot_state.h"
uint8_t app_settings_system_contrast(void) {return 100;}
void pico_boot_ready(void) {}
void app_book_cover_mode_changed(void) {}
void app_home_cover_mode_changed(void) {}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) { (void)resume; return ESP_OK; }
void pico_boot_clear_resume(void) {}
#include "wifi_transfer.h"
#include "ble_page_turner.h"
#include "ui_nav_layout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

void touch_sampler_test_enable(bool active);
void touch_sampler_test_push(int64_t t_ms, uint16_t x, uint16_t y, uint8_t count);

static void enter(app_ctx_t*);
static void leave(app_ctx_t*);
static void render(app_ctx_t*,uint8_t*);
static app_redraw_t transfer_return_tick(app_ctx_t*);
static app_redraw_t auto_update_return_tick(app_ctx_t*);
static bool auto_update_pending, auto_update_return;
static unsigned auto_update_offers, auto_update_acks;

const app_desc_t app_book = {.title="book",.render=render,.on_enter=enter,.on_exit=leave};
const app_desc_t app_transfer = {.title="transfer",.on_tick=transfer_return_tick};
const app_desc_t app_device_settings = {.title="settings",.on_tick=auto_update_return_tick};
// 快捷栏长按直达设置页只记录请求；页面本身的进入由 app_navigation_switch_to 计数。
// Quick-bar holds only record their request; entering the page is counted by app_navigation_switch_to.
static int wifi_setup_requests, bt_settings_requests;
static bool bt_return_to_origin;
void app_transfer_request_wifi_setup(void) {++wifi_setup_requests;}
void app_device_settings_request_bluetooth(bool return_to_origin) {
    ++bt_settings_requests;
    bt_return_to_origin = return_to_origin;
}
// 阅读正文返回：正文可见且恢复点可用时由主循环记下，返回时重开同一本书。
// Reader-body return: the loop captures the resume point when the body is visible
// and reopens the same book on the way back.
static bool reader_body, reader_resume_ok, reader_resume_fullscreen;
static char reader_resume_path[64];
static int reader_returns;
static char reader_return_path[64];
static bool reader_return_fullscreen;
static bool transfer_returns;
bool app_book_reader_body_visible(void) {return reader_body;}
bool app_book_resume_context(char *p,size_t c,bool *f) {
    if (!reader_resume_ok || !p || !f || strlen(reader_resume_path) >= c) return false;
    memcpy(p, reader_resume_path, strlen(reader_resume_path) + 1);
    *f = reader_resume_fullscreen;
    return true;
}
bool app_book_request_return_to_body(const char *p,bool f) {
    ++reader_returns;
    if (p) snprintf(reader_return_path,sizeof(reader_return_path),"%s",p);
    reader_return_fullscreen = f;
    return true;
}
static app_redraw_t transfer_return_tick(app_ctx_t *c) {
    if (transfer_returns) c->request_return = true;
    return APP_REDRAW_NONE;
}
static app_redraw_t auto_update_return_tick(app_ctx_t *c) {
    if (auto_update_return) c->request_return = true;
    return APP_REDRAW_NONE;
}
static int power_open_draws, power_controls_draws, power_final_draws;
static int power_trigger_step = -1, shutdowns, restarts;
static bool staged_shutdown;
int64_t esp_timer_get_time(void);
static bool power_tap_during_draw;
static bool power_drag_during_draw;
static bool power_commit_during_draw;
static EpdRect last_du_area;
EpdRect ui_power_dialog_rect(void) {return (EpdRect){0,0,684,1216};}
void ui_power_dialog_draw(uint8_t* fb,const ui_power_dialog_t *dialog) {
 (void)fb;(void)dialog;++power_open_draws;
 // 电源弹窗展开的整屏 GL16 阻塞期间，用户快速点按“重启”；两帧都在队列里。
 // While the dialog's opening full-screen GL16 blocks, the user taps Restart fast.
 if (power_tap_during_draw) {
    power_tap_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, 1);
    touch_sampler_test_push(now + 10, 0, 0, 0);
 }
 // 展开期间积压一段快速拖动：按住拇指连扫三帧后在低位松手（不足提交位，且让
 // 下一场景从抬手状态开始）。
 // A fast drag buffered while the dialog opens: three sweeping frames on the thumb, released
 // below the commit line (and leaving the next scenario starting from a lifted finger).
 if (power_drag_during_draw) {
    power_drag_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_THUMB_X, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 10, UI_POWER_THUMB_X + 30, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 20, UI_POWER_THUMB_X + 70, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 30, UI_POWER_THUMB_X + 120, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 40, UI_POWER_THUMB_X + 120, UI_POWER_THUMB_Y, 0);
 }
 // 展开期间积压"拖过提交位再松手"整段。/ A full drag-past-commit-and-release buffered while opening.
 if (power_commit_during_draw) {
    power_commit_during_draw = false;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    touch_sampler_test_push(now, UI_POWER_THUMB_X, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 10, UI_POWER_THUMB_X + 200, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 20, UI_POWER_THUMB_X + UI_POWER_SLIDE_RANGE, UI_POWER_THUMB_Y, 1);
    touch_sampler_test_push(now + 30, UI_POWER_THUMB_X + UI_POWER_SLIDE_RANGE, UI_POWER_THUMB_Y, 0);
 }
}
void ui_power_dialog_draw_controls(uint8_t* fb,const ui_power_dialog_t *dialog,EpdRect dirty) {(void)fb;(void)dialog;(void)dirty;++power_controls_draws;}
void ui_power_dialog_end(void) {}
void ui_power_final_draw(uint8_t* fb,bool restarting) {(void)fb;(void)restarting;++power_final_draws;}
// 与 ui_kit.c 同实现（桩里没有 clip_refresh_rect，宿主无刷新区裁剪）。
// Same shape as ui_kit.c (no clip_refresh_rect in the stub; the host has no refresh-area clip).
EpdRect ui_rect_union(EpdRect a,EpdRect b) {
 if(a.width<=0||a.height<=0)return b;
 if(b.width<=0||b.height<=0)return a;
 int x0=a.x<b.x?a.x:b.x,y0=a.y<b.y?a.y:b.y;
 int x1=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
 int y1=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
 return (EpdRect){x0,y0,x1-x0,y1-y0};
}

static jmp_buf done;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[16];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int long_keys;
static uint8_t idle_minutes;
static int idle_locks, before_locks, lock_feedback_calls, lock_badges, wake_clean_requests;
static bool feedback_enabled;
static bool online_busy;
static bool ble_enabled, saved_wifi, upload_busy, click_enabled, click_active;
static wifi_transfer_status_t transfer_status;
static int white_restores;
static int normalize_pushes, normalize_wave;
static int quick_draws, quick_gl, quick_du, wifi_starts, wifi_stops, ble_resets, click_presses, click_restores;

static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second, start_book;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second;
int ui_nav_hit(uint16_t x,uint16_t y){return y>=UI_NAV_TOUCH_TOP&&y<1216&&x<684?x/171:-1;}
void ui_nav_request(app_ctx_t *ctx,int index){ctx->request_app=index==0?&first:&second;}
static bool full_tick, lock_due, font_due, shake_activity;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static int quick_button_draws,quick_status_draws,quick_press_draws,quick_recent_requests,base_renders;
static bool main_fast, main_water;
int app_settings_main_refresh_mode(void) {return main_water?APP_MAIN_REFRESH_WATER:main_fast?APP_MAIN_REFRESH_FAST:APP_MAIN_REFRESH_NORMAL;}
static int shelf_exit_arms;
static int nav_arms, nav_draws, nav_full_arms, nav_area_draws;
static const void *nav_armed;
static bool main_source_visible, main_target_visible;
static bool battery_test;
static int battery_samples[32], battery_cached, battery_shown, battery_boot, battery_first_frame;
static int battery_refreshes, battery_status_draws;
bool ui_nav_status_power_changed(void) {return battery_test && battery_cached != battery_shown;}
void ui_nav_status(uint8_t *fb) {(void)fb;battery_shown=battery_cached;++battery_status_draws;}
void ui_fill_rect_fast(EpdRect area,uint8_t gray,uint8_t *fb) {
    (void)fb;assert(area.x==0&&area.y==0&&area.width==684&&area.height==76&&gray==255);
}
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32], present_steps[32];
static int media_lost, media_ready, builtin_opens, font_opens, probes, remounts, loss_step;
static bool fl_ready, fl_on;
static uint8_t fl_bri=40, fl_cct=50;
static int fl_applies, fl_control_draws, fl_sweep_draws, fl_drag_pushes, fl_white_pushes, fl_gray_pushes, fl_key_gl, quick_gl_height;
int E0470_WAVEFORM;
int E0470_FOLLOW_WAVEFORM;
int E0470_CAPSULE_DRAG_WAVEFORM;
int E0470_CAPSULE_WHITE_WAVEFORM;
int E0470_CAPSULE_GRAY_WAVEFORM;
// 主机假页面无阅读背景，关闭时应走完整归一。
// The host page has no reading background, so closing must use full normalization.
const EpdWaveform *display_normalize_waveform(bool hold_gray) {
    assert(!hold_gray); return (const EpdWaveform *)&normalize_wave;
}
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    if(a.height==108&&a.width==108){
        assert(a.width==108&&a.y==102);
        if(m==MODE_GL16)++fl_key_gl;
        else {assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU);++quick_du;}
    }
    if(a.width==604&&(a.y==UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2||a.y==UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2)){
        assert(!"binary sliders require padded complete DU");
    }
    if(a.height==76)assert(a.x==0&&a.y==0&&a.width==684&&w==&E0470_WAVEFORM&&m==MODE_GL16);
    if (a.height==UI_QUICK_HEIGHT||a.height==UI_QUICK_FRONTLIGHT_HEIGHT) {assert(a.x==0&&a.y==0&&a.width==684);++quick_du;}
    if (a.y==1120) assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.height==48&&a.width==50);
    return update_display_area_with(h,w,m,a);
}
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState*h,const void*w,int m,EpdRect a,
                                                  int pad_lines,int pad_col_bytes) {
    (void)pad_lines;(void)pad_col_bytes;
    if(a.width==604&&(a.y==UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2||a.y==UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2)){
        assert(w==&E0470_WAVEFORM&&m==MODE_DU&&pad_lines==1&&pad_col_bytes==1);
        ++fl_drag_pushes;
        return update_display_area_with(h,w,m,a);
    }
    return update_display_area_diff_with(h,w,m,a);
}
enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area) {
 const void *owner=nav_armed;nav_armed=NULL;
 enum EpdDrawError result=update_display_area_diff_with(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
 nav_armed=owner;return result;
}
static bool tap_during_present;
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    assert(a.x==0&&a.y==0&&a.width==684&&
           (a.height==UI_QUICK_HEIGHT||a.height==UI_QUICK_FRONTLIGHT_HEIGHT||a.height==1216));
    if(a.height==1216){
        assert(w==&normalize_wave&&m==MODE_GL16);++normalize_pushes;
        return update_display_area_with(h,w,m,a);
    }
    ++quick_gl; quick_gl_height=a.height;
    // 快捷层展开刷新阻塞期间，用户抬手并快速点按 Wi-Fi 圆键；三帧都在队列里。
    // While the quick layer's opening refresh blocks, the user lifts and taps Wi-Fi fast.
    if (tap_during_present) {
        tap_during_present = false;
        touch_sampler_test_push(30,0,0,0);
        touch_sampler_test_push(40,85,156,1);
        touch_sampler_test_push(50,0,0,0);
    }
    return update_display_area_with(h,w,m,a);
}
void ui_quick_menu_draw(uint8_t *fb,const ui_quick_menu_t *menu,bool wifi,bool bluetooth) {(void)fb;(void)menu;(void)wifi;(void)bluetooth;++quick_draws;}
void ui_quick_menu_end(void) {}
void ui_quick_menu_draw_status(uint8_t *fb) {
    (void)fb;++quick_status_draws;
    if(battery_test)ui_nav_status(fb);
}
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu) {
    return (EpdRect){0,0,684,menu->frontlight&&menu->fl_on?UI_QUICK_FRONTLIGHT_HEIGHT:UI_QUICK_HEIGHT};
}
bool ui_quick_menu_draw_control(uint8_t *fb,const ui_quick_menu_t *menu,
                                ui_quick_control_t control,EpdRect *dirty) {
    (void)fb;(void)menu;++fl_control_draws;
    if((control!=UI_QUICK_CTL_BRIGHTNESS&&control!=UI_QUICK_CTL_CCT)||!menu->fl_on)return false;
    *dirty=(EpdRect){UI_QUICK_FL_ROW_X, control==UI_QUICK_CTL_BRIGHTNESS
                     ?UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2:UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2,
                     UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    return true;
}
bool ui_quick_menu_draw_control_white(uint8_t *fb,const ui_quick_menu_t *menu,
                                      ui_quick_control_t control,EpdRect *dirty) {
    (void)fb;(void)menu;++fl_sweep_draws;
    if((control!=UI_QUICK_CTL_BRIGHTNESS&&control!=UI_QUICK_CTL_CCT)||!menu->fl_on)return false;
    *dirty=(EpdRect){UI_QUICK_FL_ROW_X, control==UI_QUICK_CTL_BRIGHTNESS
                     ?UI_QUICK_FL_BRI_CY-UI_QUICK_FL_ROW_H/2:UI_QUICK_FL_CCT_CY-UI_QUICK_FL_ROW_H/2,
                     UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    return true;
}
bool ui_quick_menu_draw_button(uint8_t *fb,const ui_quick_menu_t *menu,int button,bool active,bool pressed,EpdRect *dirty) {
 (void)fb;(void)menu;(void)active;assert(button>=0&&button<4);++quick_button_draws;quick_press_draws+=pressed;
 *dirty=(EpdRect){(button*2+1)*684/8-54,102,108,108};return true;
}
void ble_pt_reconnect_recent(void) {assert(ble_enabled);++quick_recent_requests;}
void ble_pt_cancel_recent(void) {assert(!ble_enabled);}
bool app_settings_ble_turner(void) {return ble_enabled;}
void app_settings_set_ble_turner(bool enabled) {ble_enabled=enabled;}
bool app_settings_frontlight_on(void) {return fl_on;}
void app_settings_set_frontlight_on(bool on) {fl_on=on;}
uint8_t app_settings_frontlight_brightness(void) {return fl_bri;}
void app_settings_set_frontlight_brightness(uint8_t percent) {fl_bri=percent;}
uint8_t app_settings_frontlight_cct(void) {return fl_cct;}
void app_settings_set_frontlight_cct(uint8_t percent) {fl_cct=percent;}
bool frontlight_ready(void) {return fl_ready;}
esp_err_t frontlight_apply(bool on,uint8_t brightness,uint8_t cct) {
    fl_on=on;fl_bri=brightness;fl_cct=cct;++fl_applies;return ESP_OK;
}
void ble_pt_reset_failure(void) {++ble_resets;}
bool ble_pt_pop_key(ble_pt_event_t *e) {(void)e;return false;}
bool ble_pt_pop_raw(ble_pt_raw_t *e) {(void)e;return false;}
void wifi_transfer_get_status(wifi_transfer_status_t *s) {*s=transfer_status;}
int wifi_transfer_get_saved_wifi(char *ssid,bool *configured) {strcpy(ssid,"saved");*configured=saved_wifi;return ESP_OK;}
int wifi_transfer_start(const wifi_transfer_cfg_t *cfg) {assert(cfg->mode==WIFI_TRANSFER_MODE_STA&&cfg->network_only);++wifi_starts;transfer_status.mode=cfg->mode;transfer_status.state=1;return ESP_OK;}
bool wifi_transfer_try_stop_if_idle(void) {if(upload_busy)return false;++wifi_stops;transfer_status.state=0;return true;}
void ui_click_feedback_reset(void) {click_active=false;}
void ui_click_feedback_begin(uint8_t *fb) {(void)fb;click_active=false;}
bool ui_click_feedback_active(void) {return click_active;}
bool ui_click_feedback_press(uint8_t *fb,int x,int y,EpdRect *r) {(void)fb;if(!click_enabled||y!=1141)return false;(void)x;*r=(EpdRect){60,1120,50,48};click_active=true;++click_presses;return true;}
bool ui_click_feedback_release(uint8_t *fb,EpdRect *r) {(void)fb;if(!click_active)return false;click_active=false;++click_restores;*r=(EpdRect){60,1120,50,48};return true;}
bool ui_click_feedback_cancel_at(int x,int y) {return x>120||y!=1141;}
static int haptic_pulses, haptic_stops;
void haptics_pulse(void) {++haptic_pulses;}
void haptics_stop(void) {++haptic_stops;}
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
void display_request_full_refresh(void) {++wake_clean_requests;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;last_du_area=a;nav_area_draws+=nav_armed!=NULL;nav_armed=NULL;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;fulls++;nav_full_arms+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*h) {(void)h;mode++;nav_draws+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
enum EpdDrawError update_display_white_restore(EpdiyHighlevelState *h,EpdRect area) {(void)h;assert(area.height<=UI_QUICK_FRONTLIGHT_HEIGHT);++white_restores;return 0;}
void display_main_transition_cancel(void) {nav_armed=NULL;}
void display_main_transition_arm(const void* owner, bool shelf_exit, bool changing_page) {(void)changing_page;display_main_transition_cancel();nav_armed=owner;++nav_arms;shelf_exit_arms+=shelf_exit;}
void display_main_transition_disarm(void) {nav_armed=NULL;}
bool device_power_ready(void) {return battery_test||lock_due||step==power_trigger_step;}
static bool key_sampling, key_sampler_started;
static unsigned key_sampler_starts, key_sampler_enables;
#ifdef POWER_KEY_SAMPLER_USB_TEST
bool power_key_test_usb_active;
#endif
bool device_power_key_sampler_start(void) {key_sampler_started=true;++key_sampler_starts;return true;}
bool device_power_key_sampler_enable(bool enabled) {bool previous=key_sampling;key_sampling=enabled&&key_sampler_started;key_sampler_enables+=key_sampling;return previous;}
esp_err_t device_power_refresh(void) {++battery_refreshes;battery_cached=battery_boot;return ESP_OK;}
device_power_key_action_t device_power_take_key_action(void) {
    if(battery_test){battery_cached=battery_samples[step];return DEVICE_POWER_KEY_NONE;}
    return step==power_trigger_step?DEVICE_POWER_KEY_LONG:DEVICE_POWER_KEY_SHORT;
}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a,void*tp,bool reader,bool light_only) {(void)tp;(void)h;(void)t;(void)a;(void)reader;(void)light_only;assert(!nav_armed);++idle_locks;lock_due=false;time_offset+=1000000;}
void app_lock_feedback(EpdiyHighlevelState*h) {(void)h;++lock_feedback_calls;if(feedback_enabled){assert(!before_locks&&!idle_locks);++lock_badges;}}
void app_lock_wait_key_idle(int ms) {(void)ms;}
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {assert(mode==APP_SLEEP_OFF);++shutdowns;longjmp(done,1);}
void app_restart_host(void) {++restarts;longjmp(done,1);}
void epd_poweroff(void) {}
const char* app_settings_font_path(void) {return "builtin";}
bool app_settings_reader_power_turn(void) {return false;}
// 上键全局兜底在宿主测试里保持不绑定：GPIO 不被轮询，既有场景行为零变化。
// / The global up-key fallback stays unbound in host tests: the GPIO is never
// polled and existing scenarios keep their exact behavior.
app_reader_key_action_t app_settings_reader_up_action(void) {return APP_READER_KEY_NONE;}
esp_err_t board_read_up_key(bool* pressed) {*pressed=false;return ESP_OK;}
app_shutdown_style_t app_settings_shutdown_style(void) {return staged_shutdown ? APP_SHUTDOWN_STAGED : APP_SHUTDOWN_FULL;}
bool ttf_font_path_is_builtin(const char*p) {(void)p;return !font_due&&!saved_sd_font;}
bool ttf_font_ready(void) {return true;}
bool ttf_font_is_builtin(void) {return !sd_font;}
const char* ttf_font_path(void) {return "other";}
int ttf_font_open(const char*p) {(void)p;font_opens++;sd_font=true;return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
int board_sd_get_info(board_sd_info_t*i) {i->present=media_test?present_steps[step]:font_due;i->mounted=media_test?mounted_steps[step]:font_due;return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
void board_sd_start_probe(void) {probes++;}
// 失效后卡回位的自动重挂：宿主用不到真实挂载，只记录调用。
// Auto-remount once a latched card returns; the host keeps the count without a real mount.
int board_sd_remount(void) {++remounts;return ESP_ERR_NOT_FINISHED;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
static void ready(app_ctx_t*c) {(void)c;media_ready++;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:NULL;}
int app_index_of(const app_desc_t* app) {return app==&first?0:app==&second?1:-1;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) {(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {
    (void)c;(void)f;
    if(battery_test){if(!base_renders)battery_first_frame=battery_cached;battery_shown=battery_cached;}
    ++base_renders;
}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const app_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
// 测试用打断判定：KEY1↔KEY2 视为反向翻页/菜单，同键重复不打断。
// Test interruption rule: KEY1↔KEY2 models reverse-turn/menu; repeats of one key never interrupt.
static bool key_interrupts_backlog(app_ctx_t* ctx, int queued_key, int newer_key) {
    (void)ctx;
    if (queued_key == newer_key) return false;
    return (queued_key == UI_KEY_1 && newer_key == UI_KEY_2) ||
           (queued_key == UI_KEY_2 && newer_key == UI_KEY_1);
}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if (shake_activity && step == 0) c->user_activity = true;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static app_redraw_t consume_power(app_ctx_t*c) {(void)c;lock_due=false;return APP_REDRAW_DONE;}
static void reset(void) {
    key_sampling=key_sampler_started=false;key_sampler_starts=key_sampler_enables=0;
#ifdef POWER_KEY_SAMPLER_USB_TEST
    power_key_test_usb_active=false;
#endif
    battery_test=false;battery_cached=battery_shown=battery_boot=battery_first_frame=-1;
    battery_refreshes=battery_status_draws=0;memset(battery_samples,0,sizeof(battery_samples));
    quick_button_draws=quick_status_draws=quick_press_draws=quick_recent_requests=base_renders=0;
    power_open_draws=power_controls_draws=power_final_draws=shutdowns=restarts=0;power_trigger_step=-1;staged_shutdown=false;
    last_du_area=(EpdRect){0};
    ble_enabled=saved_wifi=upload_busy=click_enabled=click_active=false;
    transfer_status=(wifi_transfer_status_t){0};
    white_restores=normalize_pushes=0;
    quick_draws=quick_gl=quick_du=wifi_starts=wifi_stops=ble_resets=click_presses=click_restores=0;
    wifi_setup_requests=bt_settings_requests=0;
    bt_return_to_origin=false;
    reader_body=reader_resume_ok=reader_resume_fullscreen=false;
    reader_resume_path[0]=0;
    reader_returns=0;reader_return_path[0]=0;reader_return_fullscreen=false;
    auto_update_pending=auto_update_return=false;auto_update_offers=auto_update_acks=0;
    transfer_returns=false;
    haptic_pulses=haptic_stops=0;
    fl_ready=fl_on=false;fl_bri=40;fl_cct=50;fl_applies=fl_control_draws=fl_sweep_draws=fl_drag_pushes=fl_white_pushes=fl_gray_pushes=fl_key_gl=quick_gl_height=0;
    main_fast=main_water=false;shelf_exit_arms=0;idle_minutes=0;idle_locks=before_locks=lock_feedback_calls=lock_badges=wake_clean_requests=0;feedback_enabled=false;online_busy=false;
    nav_arms=nav_draws=nav_full_arms=nav_area_draws=0;nav_armed=NULL;
    main_source_visible=main_target_visible=true;
    media_test=sd_font=saved_sd_font=false;media_lost=media_ready=builtin_opens=font_opens=probes=remounts=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));memset(present_steps,0,sizeof(present_steps));
    long_keys=0;
    home_on_touch=home_on_tick=home_on_key=start_second=start_book=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=shake_activity=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.first_app=start_book?&app_book:start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static bool product_menu_disabled(app_ctx_t *ctx) {(void)ctx;return false;}
static void before_lock(app_ctx_t *ctx) {(void)ctx;++before_locks;}
static bool main_first(app_ctx_t* ctx) {(void)ctx;return main_source_visible;}
static bool main_second(app_ctx_t* ctx) {(void)ctx;return main_target_visible;}
static void enter_subview(app_ctx_t* ctx) {enter(ctx);main_target_visible=false;}
static int root_key_taps;
static bool root_key_tap(app_ctx_t *ctx,int key,app_redraw_t *r){(void)ctx;assert(key==UI_KEY_2);++root_key_taps;*r=APP_REDRAW_NONE;return true;}
static app_redraw_t root_disappears(app_ctx_t *ctx){(void)ctx;if(step==2)main_source_visible=false;return APP_REDRAW_NONE;}
static void enable_main_tabs(void) {main_fast=true;first.main_page_visible=main_first;second.main_page_visible=main_second;}
static void enter_with_buffered_key(app_ctx_t *ctx) {
    enter(ctx);
    touch_sampler_test_push(20,80,1500,1);
    touch_sampler_test_push(30,0,0,0);
}
static app_redraw_t buffer_key_release_tick(app_ctx_t *ctx) {
    (void)ctx;
    if(step==0) {
        touch_sampler_test_push(200,240,1500,1);
        touch_sampler_test_push(250,0,0,0);
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t single_key_release_tick(app_ctx_t *ctx) {
    (void)ctx;
    if(step==0)touch_sampler_test_push(250,0,0,0);
    return APP_REDRAW_NONE;
}
static void test_panel_key_replay(void) {
    // 同一队列内的按下、保持和抬起都要路由一次，且不能漏给页面手势。
    // Route buffered presses, holds and releases once, without leaking them to page gestures.
    for(int k=0;k<3;++k) {
        for(int ending=0;ending<3;++ending) {
            reset();first.owns_keys=true;first.on_gesture=gesture;
            touch_sampler_test_enable(true);
            touch_sampler_test_push(0,k*160+80,1500,1);
            if(ending!=1)touch_sampler_test_push(5,k*160+80,1500,1);
            if(ending!=0)touch_sampler_test_push(10,0,0,0);
            for(int i=0;i<3;++i)add(0,0,0,0);
            run();touch_sampler_test_enable(false);
            assert(keys[k]==1&&!menus&&fulls==1&&!touch_calls);
            assert(!events[UI_GESTURE_PRESS]&&!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_LONG_PRESS]);
        }
        reset();first.owns_keys=true;touch_sampler_test_enable(true);
        touch_sampler_test_push(0,k*160+80,1500,1);
        touch_sampler_test_push(5,0,0,0);
        touch_sampler_test_push(10,k*160+80,1500,1);
        for(int i=0;i<3;++i)add(0,0,0,0);
        run();touch_sampler_test_enable(false);assert(keys[k]==2);
    }
    // 延后到抬手的中键用采样时间区分短长按，不能把排队耗时计入按住时长。
    // Deferred middle presses use sample time, not queueing delay, to distinguish taps and holds.
    const int durations[]={50,499,500,600};
    for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);++i) {
        reset();first.owns_keys=first.defer_middle_short=true;first.on_key_long=long_key;
        time_offset=2000000;touch_sampler_test_enable(true);
        touch_sampler_test_push(100,240,1500,1);
        touch_sampler_test_push(100+durations[i],0,0,0);
        add(0,0,0,0);run();touch_sampler_test_enable(false);
        assert(keys[1]==(durations[i]<500));
        assert(long_keys==(durations[i]>=500)&&menus==(durations[i]>=500));
    }
    // 实时与积压路径交错时仍沿用原采样时间，不能漏掉短按或凭处理延迟变成长按。
    // Mixed live/buffered paths retain sample time, without losing taps or turning processing delay into a hold.
    for(int buffered_start=0;buffered_start<2;++buffered_start) {
        reset();first.owns_keys=first.defer_middle_short=true;first.on_key_long=long_key;
        first.on_tick=buffered_start?single_key_release_tick:buffer_key_release_tick;
        time_offset=2000000;touch_sampler_test_enable(true);
        touch_sampler_test_push(100,240,1500,1);
        if(buffered_start)touch_sampler_test_push(150,240,1500,1);
        for(int i=0;i<3;++i)add(0,0,0,0);
        run();touch_sampler_test_enable(false);assert(keys[1]==1&&!long_keys&&!menus);
    }
    // 连翻打断：同向连点按序全部执行；积压里出现反向/菜单键时丢弃它之前的同向点击，
    // 最新按键立刻在当前页生效，而不是先把整队旧点击翻完。
    // Backlog interruption: a same-direction run executes fully, but a queued reverse/menu
    // press drops the stale taps ahead of it so the newest key acts on the current page.
    reset();first.owns_keys=true;first.key_interrupts_backlog=key_interrupts_backlog;
    touch_sampler_test_enable(true);
    for(int i=0;i<3;++i){touch_sampler_test_push(i*20,80,1500,1);touch_sampler_test_push(i*20+10,0,0,0);}
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(keys[0]==3&&!keys[1]&&!keys[2]);
    reset();first.owns_keys=true;first.key_interrupts_backlog=key_interrupts_backlog;
    touch_sampler_test_enable(true);
    for(int i=0;i<3;++i){touch_sampler_test_push(i*20,80,1500,1);touch_sampler_test_push(i*20+10,0,0,0);}
    touch_sampler_test_push(100,240,1500,1);touch_sampler_test_push(110,0,0,0);
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(!keys[0]&&keys[1]==1&&!keys[2]);
    // 未提供打断钩子的页面保持严格回放顺序，旧点击一记都不丢。
    // Pages without the hook keep strict replay order and never lose an older tap.
    reset();first.owns_keys=true;
    touch_sampler_test_enable(true);
    for(int i=0;i<3;++i){touch_sampler_test_push(i*20,80,1500,1);touch_sampler_test_push(i*20+10,0,0,0);}
    touch_sampler_test_push(100,240,1500,1);touch_sampler_test_push(110,0,0,0);
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(keys[0]==3&&keys[1]==1&&!keys[2]);
    // 同向连点即使带打断钩子也不收敛：10 次上一页仍然翻 10 页。
    // Same-direction runs are never collapsed by the hook either: ten "previous" taps stay ten turns.
    reset();first.owns_keys=true;first.key_interrupts_backlog=key_interrupts_backlog;
    touch_sampler_test_enable(true);
    for(int i=0;i<10;++i){touch_sampler_test_push(i*20,80,1500,1);touch_sampler_test_push(i*20+10,0,0,0);}
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(keys[0]==10&&!keys[1]&&!keys[2]);
    // 连翻震动：回放里每个单指按下沿都要给一记短震，否则连续翻页全程无触觉反馈。
    // Continuous-turn haptics: every single-finger press edge in replay gets one short pulse,
    // or a run of page turns has no touch feedback at all.
    reset();first.owns_keys=true;touch_sampler_test_enable(true);
    for(int i=0;i<3;++i){touch_sampler_test_push(i*200,80,1500,1);touch_sampler_test_push(i*200+100,0,0,0);}
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(keys[0]==3&&haptic_pulses==3);
    // 多指、读错与 USB 读卡模式的回放帧不震。
    // Multitouch, read errors and USB-storage frames stay silent during replay.
    reset();first.owns_keys=true;touch_sampler_test_enable(true);
    touch_sampler_test_push(0,80,1500,2);touch_sampler_test_push(100,0,0,0);
    touch_sampler_test_push(200,80,1500,1);touch_sampler_test_push(300,0,0,0);
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(haptic_pulses==1);
    reset();first.owns_keys=first.defer_middle_short=true;first.on_key_long=long_key;
    touch_sampler_test_enable(true);
    touch_sampler_test_push(100,240,1500,1);
    touch_sampler_test_push(600,240,1500,1);
    touch_sampler_test_push(700,240,1500,1);
    touch_sampler_test_push(800,0,0,0);
    for(int i=0;i<3;++i)add(0,0,0,0);
    run();touch_sampler_test_enable(false);assert(!keys[1]&&long_keys==1&&menus==1);
    // 滑出或多指会取消，重新滑回不能复活长按或抬手短按。
    // Leaving the key or adding a finger cancels both holds and deferred taps, even after returning.
    for(int multi=0;multi<2;++multi) {
        reset();first.owns_keys=first.defer_middle_short=true;first.on_key_long=long_key;
        touch_sampler_test_enable(true);
        touch_sampler_test_push(100,240,1500,1);
        touch_sampler_test_push(200,multi?240:80,1500,multi?2:1);
        touch_sampler_test_push(700,240,1500,1);
        touch_sampler_test_push(800,0,0,0);
        add(0,0,0,0);run();touch_sampler_test_enable(false);
        assert(!keys[1]&&!long_keys&&!menus);
    }
    // 主页面中键仍是抬手短按/按住全刷；左右键只提交一次切页。
    // Root middle taps still defer and holds refresh fully; side keys request a single page switch.
    for(int hold=0;hold<2;++hold) {
        reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;
        touch_sampler_test_enable(true);
        touch_sampler_test_push(100,240,1500,1);
        touch_sampler_test_push(hold?600:150,0,0,0);
        add(0,0,0,0);run();touch_sampler_test_enable(false);
        assert(root_key_taps==!hold&&fulls==1+hold&&!keys[1]);
    }
    for(int k=UI_KEY_1;k<=UI_KEY_3;k+=2) {
        reset();enable_main_tabs();first.owns_keys=true;touch_sampler_test_enable(true);
        touch_sampler_test_push(0,k*160+80,1500,1);
        touch_sampler_test_push(5,k*160+80,1500,1);
        touch_sampler_test_push(10,0,0,0);
        for(int i=0;i<3;++i)add(0,0,0,0);
        run();touch_sampler_test_enable(false);assert(enters==2&&!keys[k]);
    }
    reset();touch_sampler_test_enable(true);
    touch_sampler_test_push(0,240,1500,1);touch_sampler_test_push(5,0,0,0);
    touch_sampler_test_push(10,400,1500,1);touch_sampler_test_push(15,0,0,0);
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==1);
    // 新页刷新期间积压的返回键属于新页，不能被上一轮切页清理抹掉。
    // A Back key buffered while the new page draws belongs to that page, not stale transition cleanup.
    reset();request_on_tick=true;first.on_tick=origin_tick;home_on_key=true;
    second=first;second.on_tick=NULL;second.owns_keys=true;second.on_enter=enter_with_buffered_key;
    touch_sampler_test_enable(true);
    for(int i=0;i<3;++i)add(0,0,0,0);
    run();touch_sampler_test_enable(false);assert(keys[0]==1&&enters==3&&exits==2);
    reset();first.owns_keys=true;touch_sampler_test_enable(true);
    touch_sampler_test_push(0,300,20,1);touch_sampler_test_push(5,300,110,1);
    touch_sampler_test_push(10,0,0,0);touch_sampler_test_push(15,240,1500,1);
    touch_sampler_test_push(20,0,0,0);
    add(0,0,0,0);run();touch_sampler_test_enable(false);
    assert(!keys[1]&&quick_draws==1);
    puts("PASS: panel-key replay preserves taps, holds, cancellations, root/global actions, navigation and modal ownership");
}
static app_redraw_t nav_return_tick(app_ctx_t* ctx) {
    if(step==0)ctx->request_app=&second;
    else if(step==1)ctx->request_return=true;
    return APP_REDRAW_NONE;
}
static void enter_with_pending_tab(app_ctx_t* ctx){enter(ctx);ctx->request_app=&second;}
static app_redraw_t main_late_tick(app_ctx_t* ctx) {(void)ctx;return step == 1 ? response : APP_REDRAW_NONE;}
#ifdef POWER_KEY_SAMPLER_USB_TEST
static app_redraw_t key_sampling_tick(app_ctx_t *ctx) {
    (void)ctx;
    assert(key_sampling == (step == 0 || step >= 3));
    if(step==0)power_key_test_usb_active=true;
    if(step==2)power_key_test_usb_active=false;
    return APP_REDRAW_NONE;
}
#endif
int main(void) {
    test_panel_key_replay();
    // 自动提示沿真实导航保留正文恢复点；手势和已有导航请求优先。
    // Automatic offers retain the body resume point through real navigation; touch and existing requests win.
    reset();start_book=reader_body=reader_resume_ok=reader_resume_fullscreen=true;
    strcpy(reader_resume_path,"/sdcard/books/OTA.txt");auto_update_pending=auto_update_return=true;
    for(int i=0;i<3;++i)add(0,0,0,0);
    run();assert(auto_update_offers==1&&auto_update_acks==1&&reader_returns==1);
    assert(!strcmp(reader_return_path,reader_resume_path)&&reader_return_fullscreen);
    reset();start_book=reader_body=true;auto_update_pending=true;
    add(200,400,1,0);run();assert(!auto_update_offers&&auto_update_pending);
    reset();enable_main_tabs();auto_update_pending=true;request_on_tick=true;
    add(0,0,0,0);run();assert(!auto_update_offers&&auto_update_pending);
    puts("PASS: automatic OTA offer restores reading/fullscreen once and never overrides touch or an existing route");
#ifdef POWER_KEY_SAMPLER_USB_TEST
    // USB独占期间暂停，退出后恢复；设备独占页保持直读归属。/ Pause for USB ownership, resume afterwards, and leave exclusive pages in control.
    reset();battery_test=true;first.on_tick=key_sampling_tick;
    for(int i=0;i<4;++i)add(0,0,0,0);
    run();assert(key_sampler_starts==1&&key_sampler_enables==2);
    reset();battery_test=true;first.holds_pmu=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(key_sampler_starts==1&&!key_sampler_enables&&!key_sampling);
    puts("PASS: actual loop pauses fast power-key sampling for USB/exclusive use and resumes after USB");
#endif
    // 无触摸也补齐未知电量、插线与拔线；采样不变时不重复刷状态栏。
    // Repair unknown battery data and cable edges without touch, and never refresh unchanged samples.
    reset();enable_main_tabs();battery_test=true;time_step=200000;
    battery_boot=-1;battery_samples[0]=-1;battery_samples[1]=50;battery_samples[2]=151;battery_samples[3]=50;
    for(int i=0;i<5;++i){if(i==4)battery_samples[i]=50;add(0,0,0,0);}run();
    assert(battery_first_frame==-1&&battery_refreshes==1&&battery_status_draws==3&&gl_areas==3&&base_renders==1);
    reset();enable_main_tabs();battery_test=true;time_step=200000;battery_boot=50;
    for(int i=0;i<3;++i){battery_samples[i]=50;add(0,0,0,0);}run();
    assert(battery_first_frame==50&&battery_refreshes==1&&!battery_status_draws&&!gl_areas);
    // 状态栏隐藏或菜单占用时不盖正文；触摸结束后再补一次局刷。
    // Never cover a hidden status bar or an owned menu; defer one local update until touch ends.
    reset();enable_main_tabs();battery_test=true;time_step=200000;main_source_visible=false;
    battery_samples[0]=50;add(0,0,0,0);run();assert(!battery_status_draws);
    reset();enable_main_tabs();battery_test=true;time_step=200000;menu_on_tick=true;
    battery_samples[0]=50;battery_samples[1]=50;add(0,0,0,0);add(0,0,0,0);run();assert(!battery_status_draws);
    reset();enable_main_tabs();battery_test=true;time_step=200000;
    battery_samples[0]=battery_samples[1]=50;add(100,400,1,0);add(0,0,0,0);run();assert(battery_status_draws==1);
    reset();battery_test=true;first.holds_pmu=true;add(0,0,0,0);run();assert(!battery_refreshes&&!battery_status_draws);
    // 快捷层空闲时仍占有输入，但电量变化必须更新其自有状态栏。
    // An idle quick layer still owns input, but power changes must update its own status strip.
    reset();battery_test=true;time_step=200000;battery_boot=50;
    for(int i=0;i<6;++i)battery_samples[i]=i<4?50:151;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(quick_draws==1&&quick_status_draws==1&&battery_shown==151&&base_renders==2);
    puts("PASS: boot sampling, late valid SOC, idle cable edges, unchanged-state silence, input/menu/hidden guards and quick-layer power updates");
    // 第一帧报点立即切页，尚未抬手也能响应；同次停留和释放不重复。
    // The first contact switches before lift; its subsequent hold and release never repeat navigation.
    for(int policy=0;policy<3;++policy) {
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);run();assert(nav_draws==1&&enters==2&&!touch_calls&&haptic_pulses==1);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);add(232,UI_NAV_TOUCH_TOP+12,1,0);add(232,UI_NAV_TOUCH_TOP+12,1,0);add(0,0,0,0);run();
        assert(nav_draws==1&&enters==2&&!touch_calls);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);add(0,0,0,0);add(85,UI_NAV_TOUCH_TOP,1,0);add(0,0,0,0);run();
        assert(nav_draws==2&&enters==3&&!touch_calls);
    }
    reset();enable_main_tabs();add(85,UI_NAV_TOUCH_TOP,1,0);add(0,0,0,0);run();assert(!nav_draws&&enters==1);
    reset();enable_main_tabs();first.on_gesture=gesture;add(200,UI_NAV_TOUCH_TOP,2,0);add(0,0,0,0);run();assert(!nav_draws&&haptic_pulses==0);
    reset();enable_main_tabs();add(200,UI_NAV_TOUCH_TOP,1,-1);add(0,0,0,0);run();assert(!nav_draws&&haptic_pulses==0);
    reset();enable_main_tabs();main_source_visible=false;first.on_gesture=gesture;
    add(200,UI_NAV_TOUCH_TOP,1,0);run();assert(!nav_draws&&enters==1);
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(85,UI_NAV_TOUCH_TOP,1,0);add(0,0,0,0);add(85,UI_NAV_TOUCH_TOP,1,0);add(0,0,0,0);run();
        assert(!nav_draws&&enters==1);
    }
    // 待切另一页时点击当前页必须取消旧待切请求，底栏图标保持静止。
    // Tapping the active page while another tab is pending must drop that pending request, leaving the footer still.
    for(int policy=0;policy<3;++policy){
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;first.on_enter=enter_with_pending_tab;
        add(85,UI_NAV_TOUCH_TOP,1,0);run();assert(enters==1&&!nav_draws);
    }
    puts("PASS: root tabs act on first contact in all modes, enlarged top edge, one-shot holds, rapid taps, active-tab no-op, subview/multitouch/read-error guards");
    reset();add(10,1141,1,0);
    app_ctx_t loading={.read_touch=s31_input_read_touch,.touch_context=(void*)1};
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1};
    assert(app_poll_main_navigation(&loading)&&loading.request_app==&first&&loading.user_activity);
    loading.request_app=NULL;samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)); // current shelf, not a cancellation
    samples[0]=(sample_t){.x=10,.y=1141,.count=2};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1,.error=-1};assert(!app_poll_main_navigation(&loading));

    // 冷封面加载的轮询中，点当前书架不取消；同一接触不重复。
    // During cold-cover polling the current shelf is not cancelled and one contact never repeats.
    uint8_t loading_fb=0;EpdiyHighlevelState loading_hl={0};
    loading.fb=&loading_fb;loading.hl=&loading_hl;loading.request_app=NULL;
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading));
    assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){0};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading));

    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    // 开机首帧前已加载保存字体，因此首个手势无需再被延迟字体切换取消。
    // The saved font is ready before the first frame, so the first gesture is no longer cancelled.
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&font_opens==1);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[2]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 首次无卡时后插卡应触发探测，挂载完成只通知页面一次。
    // First insertion after an empty boot probes, then notifies the active page once.
    reset();media_test=true;time_step=600000;first.on_media_ready=ready;
    present_steps[1]=present_steps[2]=present_steps[3]=true;
    mounted_steps[2]=mounted_steps[3]=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(probes>=1&&media_ready==1&&!media_lost);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&!font_opens);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==1);
    // 失效后卡回到位自动重挂一次，挂载完成后只通知页面一次。
    // A latched card that returns remounts once; completion notifies the page exactly once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;present_steps[2]=present_steps[3]=true;
    mounted_steps[3]=true;time_step=1000000;first.on_media_lost=lost;first.on_media_ready=ready;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&remounts==1&&media_ready==1);
    reset();idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&wake_clean_requests==1); // Real scheduler flushes before automatic lock.
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&wake_clean_requests==1); /* Activity is consumed once, not latched. */
    reset();idle_minutes=0;time_step=600000000;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;online_busy=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;first.holds_pmu=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=30000000;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();first.owns_keys=true;first.on_gesture=gesture;first.menu_handle_enabled=product_menu_disabled;
    add(636,1150,1,0);add(0,0,0,0);run();
    assert(!menus&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    // 首帧保持全刷；切页一次完成，按住、松开、空闲都不安排第二次刷新。
    // Retain the initial full update; finish navigation once without another draw on hold, release or idle.
    reset();enable_main_tabs();add(0,0,0,0);run();assert(!nav_arms&&!nav_draws&&!nav_full_arms);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;add(0,0,0,0);add(0,0,0,0);second.on_tick=return_tick;add(0,0,0,0);run();
    assert(nav_arms==2&&nav_draws==2&&!nav_armed&&shelf_exit_arms==1);
    // 目录延后完成重绘和同页局部更新也获得固定底栏许可，没有额外 idle 推屏。
    // Deferred directory completion and local repaint receive frozen-navigation permission without an idle redraw.
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_PAGE;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==2&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    // 子视图、非主页面保留普通出口，显式全刷优先。
    // Subviews and non-main pages retain ordinary output; explicit full refresh has priority.
    reset();enable_main_tabs();main_source_visible=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.on_enter=enter_subview;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.main_page_visible=NULL;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.enter_full=true;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(nav_arms==1&&nav_full_arms==1&&!nav_draws);
    reset();enable_main_tabs();main_fast=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!shelf_exit_arms&&!nav_full_arms);
    reset();enable_main_tabs();main_fast=false;first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_full_arms);
    reset();enable_main_tabs();lock_due=true;time_step=3000000;add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&!nav_arms&&!nav_draws);
    puts("main-tab scheduler: single immediate draw, no hold/release/idle redraw, rapid return, root/subview boundaries and full-output priority passed");
    // 跑真实边缘识别和主循环，验证不会漏给底层页面或按住重复动作。
    // Run the real edge recognizer and loop to reject underlay leakage and repeated hold actions.
    reset();first.on_gesture=gesture;
    add(300,20,1,0);add(300,110,1,0);add(300,120,1,0);add(0,0,0,0);
    add(300,500,1,0);add(0,0,0,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(quick_draws==1&&quick_gl==1&&!quick_du&&fulls==1&&!white_restores&&normalize_pushes==1);
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    reset();main_fast=true;saved_wifi=true;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(85,156,1,0);add(85,156,1,0);add(0,0,0,0);
    add(85,156,1,0);add(0,0,0,0);run();
    assert(wifi_starts==1&&wifi_stops==1&&quick_gl==1&&quick_du==4&&quick_draws==1&&base_renders==2&&quick_press_draws==2&&!touch_calls);
    reset();upload_busy=true;transfer_status=(wifi_transfer_status_t){.state=1,.mode=WIFI_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&transfer_status.state==1);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&!touch_calls);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(256,156,1,0);add(0,0,0,0);add(256,156,1,0);add(0,0,0,0);run();
    assert(!ble_enabled&&quick_recent_requests==1&&!ble_resets&&quick_gl==1&&quick_draws==1&&quick_status_draws==2&&quick_press_draws==2&&base_renders==2&&!touch_calls);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(427,156,1,0);add(0,0,0,0);run();
    assert(fulls==2&&base_renders==2&&quick_draws==1&&quick_du==1&&quick_press_draws==1);
    reset();first.on_before_lock=before_lock;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(427,156,1,0);add(0,0,0,0);add(598,156,1,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&fulls>=2&&!touch_calls);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;first.on_gesture=gesture;click_enabled=true;
        add(85,1141,1,0);add(85,1141,1,0);add(0,0,0,0);run();
        assert(click_presses==1&&click_restores==1&&!click_active&&ticks==1&&du_areas==2&&!gl_areas);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;click_enabled=true;request_on_touch=true;
        add(200,1141,1,0);add(0,0,0,0);run();
        assert(!click_presses&&!click_restores&&!click_active&&!du_areas&&!gl_areas&&nav_draws==1);
    }
    reset();click_enabled=true;add(85,1141,1,0);add(85,1141,2,0);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    reset();click_enabled=true;add(85,1141,1,0);add(0,0,0,1);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;saved_wifi=true;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);run();
        assert(quick_press_draws==1&&quick_du==1&&quick_gl==1&&quick_draws==1&&base_renders==2&&!wifi_starts);
        reset();first.on_gesture=gesture;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);
        add(85,156,2,0);add(0,0,0,0);run();
        assert(quick_press_draws==1&&quick_du==2&&quick_gl==1&&quick_draws==1&&!wifi_starts&&!touch_calls);
        reset();first.on_gesture=gesture;
        add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);
        add(85,156,0,1);add(0,0,0,0);run();
        assert(quick_press_draws==1&&quick_du==2&&quick_gl==1&&!wifi_starts&&!touch_calls);
    }
    puts("quick layer/click scheduler: modal ownership, local BW/gray, immediate local presses without underlay re-render/glass regeneration, one saved-network connect/disconnect, upload/no-credential guards, BLE on/recent request and off before connection, full/lock and release/multitouch/read-error restoration passed");
    // 长按 WiFi / 蓝牙圆键 0.5 秒直达设置页：只请求页面，不切换开关，抬手也不补点按。
    // A 0.5s hold on the WiFi / Bluetooth circle opens its settings page without
    // toggling the switch, and the release never replays the tap.
    reset();saved_wifi=true;time_step=1000000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(85,156,1,0);add(85,156,1,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(wifi_setup_requests==1&&!wifi_starts&&!wifi_stops&&exits==1);
    reset();time_step=1000000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(256,156,1,0);add(256,156,1,0);add(256,156,1,0);add(0,0,0,0);run();
    assert(bt_settings_requests==1&&bt_return_to_origin&&!ble_enabled&&exits==1);
    puts("quick layer holds: 0.5s WiFi/Bluetooth presses open their settings pages without toggling switches or replaying the tap");
    // 从阅读正文长按离开：返回时按离开时的书与全屏状态重开正文，而不是落到书架。
    // Leaving the reading body via a quick-bar hold: the return reopens that book
    // and full-screen state instead of landing on the shelf.
    reset();start_book=true;reader_body=reader_resume_ok=true;reader_resume_fullscreen=true;
    snprintf(reader_resume_path,sizeof(reader_resume_path),"%s","/sdcard/books/a.epub");
    transfer_returns=true;time_step=1000000;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(85,156,1,0);add(85,156,1,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(wifi_setup_requests==1&&reader_returns==1&&!strcmp(reader_return_path,"/sdcard/books/a.epub")&&
           reader_return_fullscreen&&enters==2);
    puts("quick layer return: leaving the reading body restores the same book and full-screen state");
    // 第四枚圆钮是前光开关：开灯才展开胶囊滑杆并整层重排到 480，拖动帧与松手
    // 提交都走完整 DU 差分（不追加两段定稿，白表/灰表零推送），熄灯收回 264，收层
    // 整屏归一；开关点按不再单独重画圆钮。
    // The fourth circle is the frontlight switch: the capsules expand with a
    // whole-layer re-composite to 480, drag frames and the release commit both
    // use complete DU (no two-stage settle; zero white/gray pushes),
    // switching off returns to 264 and closing normalizes the whole screen; the switch tap
    // no longer repaints its circle alone.
    reset();fl_ready=true;fl_on=false;fl_bri=40;fl_cct=50;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(598,156,1,0);add(0,0,0,0);
    add(399,300,1,0);add(512,300,1,0);add(0,0,0,0);
    add(399,398,1,0);add(0,0,0,0);
    add(300,500,1,0);add(300,300,1,0);add(0,0,0,0);run();
    assert(fl_on&&fl_bri==78&&fl_cct==59);
    assert(quick_gl_height==UI_QUICK_FRONTLIGHT_HEIGHT&&fulls==1&&!white_restores&&normalize_pushes==1);
    assert(!fl_key_gl&&fl_control_draws==5&&fl_sweep_draws==0&&fl_drag_pushes==5&&fl_white_pushes==0&&fl_gray_pushes==0);
    // 展开与开关点按各重排一次并重画一层；收起时层已关闭，只重排底层页面。
    // Opening and the switch tap each re-composite and redraw the layer; on close the
    // layer is already gone, so only the underlay page is re-rendered.
    assert(quick_draws==2&&quick_gl==2&&base_renders==4);
    puts("frontlight: fourth-circle switch expands the capsule card, slider rows, DU drag and release commit and whole-screen normalization on close passed");
    // 采样回放：快捷层展开的整页刷新期间快速点按，按下沿不能丢；一次点按只切换一次。
    // Sampler replay: a fast tap during the quick layer's opening refresh keeps its
    // press edge and toggles exactly once.
    reset();saved_wifi=true;first.on_gesture=gesture;
    touch_sampler_test_enable(true);
    touch_sampler_test_push(10,300,20,1);
    touch_sampler_test_push(20,300,110,1);
    tap_during_present=true;
    run();
    assert(wifi_starts==1&&!wifi_stops&&!touch_calls&&quick_gl==1&&quick_draws==1);
    touch_sampler_test_enable(false);tap_during_present=false;
    puts("quick layer replay: a tap buffered during the opening refresh keeps its press edge");
    // 回放帧时间戳必须按全宽毫秒写回输入时刻：32 位回绕曾在连续开机 49.7 天后把一次
    // 翻页判成空闲超时并立刻锁屏。
    // Replayed frame timestamps must feed the full-width ms value back into the input
    // time: a 32-bit wrap used to read as an expired idle timeout after 49.7 days.
    reset();idle_minutes=1;time_offset=(int64_t)4294967296LL*1000;time_step=10000;
    touch_sampler_test_enable(true);
    touch_sampler_test_push((int64_t)4294967296LL,300,110,1);
    touch_sampler_test_push((int64_t)4294967296LL+5,300,110,1);
    touch_sampler_test_push((int64_t)4294967296LL+10,0,0,0);
    run();assert(!idle_locks);
    touch_sampler_test_enable(false);
    puts("touch replay: the timestamp stays full width across the 49.7-day wrap and never fakes an idle timeout");
    // 反馈必须早于页面保存/后台收尾；下一页与自动休眠不显示主动锁屏徽标。
    // Acknowledgement precedes saves/joins; consumed page turns and idle sleep omit it.
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;feedback_enabled=true;
        first.on_before_lock=before_lock;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();
        assert(lock_badges==1&&lock_feedback_calls==1&&before_locks==1&&idle_locks==1);
        reset();feedback_enabled=true;first.on_power_short=consume_power;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();assert(!lock_badges&&!lock_feedback_calls&&!before_locks&&!idle_locks);
        reset();feedback_enabled=true;idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
        add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&before_locks==1&&!lock_feedback_calls&&!lock_badges);
    }
    puts("PASS: manual lock acknowledgement precedes page save/reader joins in all three modes; consumed power turns and auto sleep omit the badge");
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+112,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&power_controls_draws==2&&shutdowns==1&&!restarts&&power_final_draws==1&&!touch_calls&&ticks==1&&du_areas==2&&gl_areas==1);
    /* 「彻底关机」档不再改变「关机」按钮：任何档位都立即断电。/ The full-off style no longer rebinds 关机: every style powers off immediately. */
    reset();power_trigger_step=1;time_step=3000000;staged_shutdown=true;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&shutdowns==1&&power_final_draws==1&&!restarts&&!idle_locks&&!wake_clean_requests);
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_RESTART_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1);
    reset();enable_main_tabs();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(UI_POWER_CANCEL_X,UI_POWER_BUTTON_Y,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!shutdowns&&!restarts&&!power_final_draws&&nav_arms==0&&mode==1);
    for(int fault=0;fault<2;++fault){
        reset();power_trigger_step=1;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,fault?2:0,fault?0:1);add(0,0,0,0);run();
        assert(!shutdowns&&!restarts&&power_controls_draws==2&&!touch_calls);
    }
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(UI_POWER_THUMB_X,UI_POWER_THUMB_Y,1,0);add(UI_POWER_THUMB_X+UI_POWER_SLIDE_RANGE,UI_POWER_THUMB_Y,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&!power_controls_draws&&!shutdowns&&!restarts);
    puts("PASS: real loop and power recognizer: DU-only contact, release commits off/restart once in every shutdown style, cancel restores full navigation, touch/multitouch errors and entry-held suppression");
    // 采样回放：电源弹窗展开的整屏刷新期间快速点按“重启”，按下沿不能丢。
    // Sampler replay: a fast Restart tap during the dialog's opening refresh keeps its press edge.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_tap_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1&&!touch_calls);
    touch_sampler_test_enable(false);power_tap_during_draw=false;
    puts("power dialog replay: a tap buffered during the opening refresh keeps its press edge");
    // 采样回放：拖动期间积压的多帧必须合并成一次控件重绘和一次 DU 推屏，合并区覆盖整段
    // 轨迹；逐帧推屏（旧行为）会在 5ms 采样对 55–90ms 扫描下积压，滑块越拖越落后。
    // Sampler replay: frames buffered during a drag must merge into one controls redraw and one
    // DU push whose area spans the whole sweep; per-frame pushes (old behavior) backlog at 5 ms
    // sampling against 55-90 ms scans and the slider falls further behind.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_drag_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    touch_sampler_test_enable(false);
    assert(power_open_draws==1&&gl_areas==1&&power_controls_draws==1&&du_areas==1&&
           !shutdowns&&!restarts&&power_final_draws==0&&
           last_du_area.x==UI_POWER_THUMB_X-UI_POWER_THUMB_R-1&&
           last_du_area.width==120+2*UI_POWER_THUMB_R+3);
    puts("power dialog replay: buffered drag frames merge into one push covering the sweep");
    // 拖动到提交位后松手也积压在回放里：动作延迟执行一次，不再为中途轨迹浪费推屏。
    // A release past the commit line buffered in the same replay: the action runs once and no
    // intermediate trajectory push is wasted.
    reset();power_trigger_step=1;time_step=3000000;
    touch_sampler_test_enable(true);power_commit_during_draw=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    touch_sampler_test_enable(false);
    assert(shutdowns==1&&power_final_draws==1&&!restarts&&du_areas==0&&gl_areas==1&&power_controls_draws==0);
    puts("power dialog replay: buffered release past the commit line still powers off once");
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==1&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;root_key_taps=0;time_step=300000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&fulls==2&&!keys[1]);
    reset();enable_main_tabs();first.owns_keys=true;first.on_main_key=root_key_tap;first.on_tick=root_disappears;root_key_taps=0;time_step=100000;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(root_key_taps==0&&!keys[1]);
    puts("app_loop: 50 scheduler scenarios passed, including automatic lock, save callback, input reset and busy guards");
    return 0;
}

/* 中文：硬件 BLE 与联网任务替身。/ English: Hardware BLE and online worker shims. */
void ble_pt_sync(bool enabled) { (void)enabled; }
void ble_pt_poll(void) {}
bool pico_online_busy(void) { return online_busy; }

uint8_t app_settings_auto_lock_minutes(void){return idle_minutes;}
uint32_t ble_pt_input_serial(void){return 0;}


void crash_note_log(void){}

void app_time_service_poll(bool idle) { (void)idle; }
void app_time_service_cancel(void) {}
void pico_ota_auto_poll(bool idle) { (void)idle; }
void pico_ota_auto_cancel(void) {}
bool pico_ota_auto_pending(void) { return auto_update_pending; }
void pico_ota_auto_acknowledge(void) { auto_update_pending=false; ++auto_update_acks; }
bool app_device_settings_offer_update(bool already_in_settings) { (void)already_in_settings; ++auto_update_offers; return true; }

// 开机分段计时只写崩溃日志，替身忽略；本页不验证它。/ Boot-phase marks only feed the crash log; the shim ignores them.
void rp_boot_mark(const char *name) { (void)name; }

void ui_avatar_invalidate(void) {}
bool book_cover_jobs_quiesce(unsigned timeout){(void)timeout;return true;}
void book_lock_collage_invalidate(void) {}

bool ble_pt_connected(void) { return false; }

// 调度回归链接真实生命周期服务；失败组合由独立生命周期测试控制。
// Scheduler regressions link the actual lifecycle service; dedicated lifecycle tests control failure combinations.
bool wifi_transfer_pause_for_sleep(void) { return true; }
bool ui_avatar_quiesce(unsigned timeout) { (void)timeout; return true; }
esp_err_t ble_pt_stop(uint32_t timeout) { (void)timeout; return ESP_OK; }
esp_err_t ble_pt_stop_for_sleep(uint32_t timeout) { (void)timeout; return ESP_OK; }
