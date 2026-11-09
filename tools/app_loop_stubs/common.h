/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
const char* esp_err_to_name(int err);
typedef void* cst836u_handle_t;
typedef void* sc7a20h_handle_t;
#define CST836U_MAX_POINTS 2
#define CST836U_RAW_LEN 15
#define ESP_ERR_INVALID_ARG 3
typedef struct { bool active; uint8_t id,event; uint16_t x,y; } cst836u_point_t;
typedef struct { bool touched; uint8_t count; uint16_t x,y; cst836u_point_t points[2]; uint8_t raw[15]; } cst836u_touch_t;
typedef struct { int unused; } cst836u_info_t;
int cst836u_read(void*, cst836u_touch_t*);
int cst836u_get_info(void*, cst836u_info_t*);
typedef struct EpdiyHighlevelState { int unused; } EpdiyHighlevelState;
typedef struct { int x,y,width,height; } EpdRect;
enum EpdDrawError { EPD_DRAW_SUCCESS, EPD_DRAW_NO_PHASES_AVAILABLE = 4, EPD_DRAW_INVALID_CROP = 64 };
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
#define MODE_DU 1
#define MODE_GL16 2
#define MODE_GC16 3
extern int E0470_WAVEFORM;
extern int E0470_FOLLOW_WAVEFORM;
extern int E0470_CAPSULE_DRAG_WAVEFORM;
extern int E0470_CAPSULE_WHITE_WAVEFORM;
extern int E0470_CAPSULE_GRAY_WAVEFORM;
int64_t esp_timer_get_time(void);
void vTaskDelay(int);
#define pdMS_TO_TICKS(x) (x)
// 日志桩必须消费参数，否则只用于日志的计时变量会被判成未使用。
// The log stub must consume its arguments or timing locals used only in logs read as unused.
static inline void app_loop_log_sink(const char *fmt, ...) { (void)fmt; }
#define ESP_LOGI(...) app_loop_log_sink(__VA_ARGS__)
#define ESP_LOGE(...) app_loop_log_sink(__VA_ARGS__)
void guard_draw_result(EpdiyHighlevelState*, enum EpdDrawError);
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState*, const void*, int, EpdRect, int, int);
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_with(EpdiyHighlevelState*, const void*, int);
enum EpdDrawError update_display_mode_diff_with(EpdiyHighlevelState*, const void*, int);
enum EpdDrawError update_display_full(EpdiyHighlevelState*);
enum EpdDrawError update_display_mode(EpdiyHighlevelState*, int);
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*, int);
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*);
enum EpdDrawError update_display_white(EpdiyHighlevelState*);
bool display_take_white_exit(void);
void display_main_transition_arm(const void*, bool, bool);
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER};
int app_settings_main_refresh_mode(void);
void display_main_transition_disarm(void);
void display_main_transition_cancel(void);
bool device_power_ready(void);
bool device_power_key_sampler_start(void);
bool device_power_key_sampler_enable(bool);
typedef enum { DEVICE_POWER_KEY_NONE, DEVICE_POWER_KEY_SHORT, DEVICE_POWER_KEY_LONG } device_power_key_action_t;
device_power_key_action_t device_power_take_key_action(void);
void enter_lock_and_sleep(EpdiyHighlevelState*, int64_t*, void*, void*, bool, bool);
void app_lock_feedback(EpdiyHighlevelState*);
void app_lock_wait_key_idle(int);
typedef enum { APP_SLEEP_SOFT, APP_SLEEP_OFF } app_sleep_mode_t;
esp_err_t app_enter_host_sleep(app_sleep_mode_t);
void app_restart_host(void);
void epd_poweroff(void);
const char* app_settings_font_path(void);
bool app_settings_reader_power_turn(void);
// app_loop 只消费上键动作做全局前光兜底；完整动作枚举在 main/settings/settings.h。
// / app_loop only consumes the up-key action for the global frontlight fallback;
// the full action enum lives in main/settings/settings.h.
typedef enum { APP_READER_KEY_NONE, APP_READER_KEY_FRONTLIGHT } app_reader_key_action_t;
app_reader_key_action_t app_settings_reader_up_action(void);
esp_err_t board_read_up_key(bool* pressed);
typedef enum { APP_SHUTDOWN_FULL, APP_SHUTDOWN_STAGED, APP_SHUTDOWN_LIGHT } app_shutdown_style_t;
app_shutdown_style_t app_settings_shutdown_style(void);
bool ttf_font_path_is_builtin(const char*);
bool ttf_font_ready(void);
bool ttf_font_is_builtin(void);
const char* ttf_font_path(void);
int ttf_font_open(const char*);
int ttf_font_open_builtin(void);
typedef struct { bool present, mounted; } board_sd_info_t;
int board_sd_get_info(board_sd_info_t*);
void board_sd_start_probe(void);
int board_sd_remount(void);

int epd_rotated_display_width(void);
void epd_fill_triangle(int,int,int,int,int,int,uint8_t,uint8_t*);
