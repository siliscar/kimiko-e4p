/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 读写。打开失败就用深睡默认值，不擦除整个分区。
 *
 * 用户修订：底部上滑是固定工具栏入口，短按和长按可自由设置；启动与恢复不再强制改成工具栏，提交失败仍保留原映射。
 * User revision: bottom-edge swipes provide a permanent toolbar entry, freeing all short/hold mappings. Boot/restore no longer force a toolbar key; failed commits retain previous mappings.
 * 用户修订：三键短按映射单独持久化；备份v11追加长按映射，旧备份缺少长按映射时恢复返回首页。
 * User revision: persist short key mappings; backup v11 adds a checked hold action; older backups without a hold mapping default to home. Five reading stroke levels are sealed in backup v13.
 * 用户修订：首行缩进微调-20..20px单独持久化，备份v12追加校验块；旧备份恢复0，不更改密码凭据。
 * User revision: persist a -20..20px indent adjustment; v12 adds a checked block, legacy backups default to zero and leave PIN credentials intact.
 * 用户修订：备份v27追加书内字体、竖排正文与完整关机三态；旧备份回落开启/横排/扩展位。
 * User revision: backup v27 appends book fonts, vertical text and the full shutdown states; older backups fall back to on/horizontal/the extension bit.
 * 用户修订：备份v28追加触摸震动开关与强度；旧备份回落开启、弱档。
 * User revision: backup v28 appends the touch-vibration switch and intensity; older backups fall back to on and weak.
 * 用户修订：备份v33追加首页上下组件；旧备份不改变当前组件布局。
 * User revision: backup v33 appends Home regions; legacy backups preserve the current layout.
 * NVS load/store. A failed open keeps the deep-sleep default; the
 * partition is not erased.
 */

#include "settings.h"
#include "home_components.h"
#include "weather_service.h"
#include "../display/display_refresh_config.h"
#include "book/book_font_size.h"

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "epdiy.h"
#include "haptics.h"
#include "device_power.h"
#include "board_sd.h"
#include "wifi_transfer.h"
#include "book/book_history_backup.h"

#define TAG "settings"
#define NVS_NS "read_pico"
// NVS open也可能创建命名空间；保护覆盖整个写事务，失败不进入NVS。
// NVS open may create a namespace, so protect the whole write transaction and skip NVS after failed shutdown.
static esp_err_t settings_write_open(nvs_handle_t *handle) {
    if (!epd_flash_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, handle);
    if (err != ESP_OK) epd_flash_end();
    return err;
}
static void settings_write_close(nvs_handle_t handle) {
    nvs_close(handle);
    epd_flash_end();
}

#define NVS_KEY_SLEEP "sleep"
#define NVS_KEY_SHUTDOWN_MODE "shutdown"
#define NVS_KEY_HOME_FULL "home_full"
#define NVS_KEY_HOME_COMPONENT "home_widget"
#define NVS_KEY_HOME_GOAL "home_goal"
#define NVS_KEY_MAIN_REFRESH "main_mode"
#define NVS_KEY_AUTO_LOCK "idle_lock"
#define NVS_KEY_AUTO_TIME "auto_time"
#define NVS_KEY_AUTO_OTA "auto_ota"
#define NVS_KEY_TSYNC_AT "tsync_at"
#define NVS_KEY_TSYNC_TRY "tsync_try"
#define NVS_KEY_DEVICE_NAME "dev_name"
#define DEFAULT_DEVICE_NAME "kimiko"
#define NVS_KEY_AVATAR "dev_avatar"
#define NVS_KEY_STATUS_SIGNATURE "status_sig"
#define NVS_KEY_TOUCH_HAPTIC "touch_haptic"
#define NVS_KEY_BATT_LOG "batt_log"
#define NVS_KEY_HAPTIC_LEVEL "haptic_lvl"
#define NVS_KEY_FONT "font"
#define NVS_KEY_SYS_FONT "sys_font"
#define NVS_KEY_SYS_SIZE "sys_size"
#define NVS_KEY_SYS_CONTRAST "sys_contrast"
#define NVS_KEY_LOCK_STYLE "lock_ui"
#define NVS_KEY_WALLPAPER "wallpaper"
#define NVS_KEY_BOOKMARK "lock_png"
#define NVS_KEY_WAKE "lwake"
#define NVS_KEY_BOOT "lboot"
#define NVS_KEY_PICKUP "pickup"
#define NVS_KEY_BOOK_PX "bk_px"
#define NVS_KEY_BOOK_WEIGHT "bk_weight"
#define NVS_KEY_BOOK_WEIGHT_FINE "bk_wfine"
#define NVS_KEY_BOOK_AA "bk_aa"
#define NVS_KEY_BOOK_BACKGROUND "bk_bg"
#define NVS_KEY_READER_FULL "rd_gc16"
#define NVS_KEY_READER_TURN "rd_turn"
#define NVS_KEY_POWER_TURN "rd_power"
#define NVS_KEY_VOLUME_TURN "rd_volume"
#define NVS_KEY_SHAKE_TURN "rd_shake"
// 状态栏显隐新键；rd_immersive 是旧「全屏沉浸」位，仅作老固件升级迁移与降级回写。/ New
// status-bar key; rd_immersive is the retired immersive bit kept for upgrades and downgrade writes.
#define NVS_KEY_STATUS "rd_status"
#define NVS_KEY_IMMERSIVE "rd_immersive"
#define NVS_KEY_OPEN_FULL "rd_open_full"
#define NVS_KEY_HIDE_IMAGES "rd_no_image"
#define NVS_KEY_HOLD_REFRESH "rd_hold_full"
#define NVS_KEY_HOLD_ACTION "rd_key_hold"
#define NVS_KEY_DOUBLE_ACTION "rd_key_dbl"
#define NVS_KEY_UP_ACTION "rd_key_up"
#define NVS_KEY_READER_KEYS_V2 "rd_keys_v2"
// 页眉页脚三段槽位：页眉左/中/右、页脚左/中/右各存一个内容枚举。
// Header/footer three-slot chrome: one content enum per slot across both bars.
#define NVS_KEY_CHROME_HEADER_L "rd_hdr_l"
#define NVS_KEY_CHROME_HEADER_M "rd_hdr_m"
#define NVS_KEY_CHROME_HEADER_R "rd_hdr_r"
#define NVS_KEY_CHROME_FOOTER_L "rd_ftr_l"
#define NVS_KEY_CHROME_FOOTER_M "rd_ftr_m"
#define NVS_KEY_CHROME_FOOTER_R "rd_ftr_r"
#define NVS_KEY_CHROME_HEADER_H "rd_hdr_h"
#define NVS_KEY_CHROME_FOOTER_H "rd_ftr_h"
#define NVS_KEY_READER_PROGRESS "rd_progress"
#define NVS_KEY_VERTICAL_TURN "rd_vertical"
#define NVS_KEY_BLE_TURNER "ble_turn"
#define NVS_KEY_SHELF_RECENT "shelf_recent"
#define NVS_KEY_BOOK_LINE "bk_line"
#define NVS_KEY_BOOK_PARA "bk_para"
#define NVS_KEY_BOOK_STYLES "bk_styles"
// 旧单一边距键只作左右边距的升级回退与降级回写；新配置按四向各存一键。
// The old single-margin key only backs the left/right upgrade fallback and downgrade writes;
// the new configuration stores one key per side.
#define NVS_KEY_BOOK_MARGIN "bk_margin"
#define NVS_KEY_BOOK_MARGIN_L "bk_marg_l"
#define NVS_KEY_BOOK_MARGIN_R "bk_marg_r"
#define NVS_KEY_BOOK_MARGIN_T "bk_marg_t"
#define NVS_KEY_BOOK_MARGIN_B "bk_marg_b"
#define NVS_KEY_BOOK_TRACK "bk_track"
#define NVS_KEY_BOOK_INDENT "bk_indent"
#define NVS_KEY_BOOK_INDENT_ADJUST "bk_indent_adj"
#define NVS_KEY_BOOK_RULE "bk_rule"
#define NVS_KEY_BOOK_RULE_OFFSET "bk_rule_y"
#define NVS_KEY_SHELF_STYLE "shelf_ui"
#define NVS_KEY_SHELF_V22 "shelf_v22"
#define NVS_KEY_BOOKS_DIR "books_dir"
#define NVS_KEY_FONTS_DIR "fonts_dir"
#define NVS_KEY_BOOK_FONTS "bk_fonts"
#define NVS_KEY_BOOK_VERTICAL "bk_vert"
#define NVS_KEY_BOOK_TITLE_STYLE "bk_title"
#define NVS_KEY_BOOK_TITLE_SIZE "bk_tsize"
#define NVS_KEY_FRONTLIGHT_ON "fl_on"
#define NVS_KEY_FRONTLIGHT_BRI "fl_bri"
#define NVS_KEY_FRONTLIGHT_CCT "fl_cct"
#define FONT_PATH_MAX 160
#define MEDIA_DIR_MAX 96

static app_sleep_mode_t s_sleep = APP_SLEEP_DEEP;
// 关机默认仅浅睡：锁屏画面保留、不断电，按键/拿起返回关机前页面。
// Shutdown defaults to light-only: the lock face stays on and power is kept, so a key or
// pickup returns to the page shown before shutdown.
static app_shutdown_style_t s_shutdown_style = APP_SHUTDOWN_LIGHT;
static bool s_home_full_refresh;
static app_home_component_t s_home_component = APP_HOME_FOLDER;
static uint8_t s_home_goal_minutes = 30;
static app_main_refresh_mode_t s_main_refresh = APP_MAIN_REFRESH_WATER;
static char s_device_name[64] = DEFAULT_DEVICE_NAME;
static char s_avatar[288];
static char s_status_signature[96];
// 触摸震动默认开启、弱档；与原厂默认一致。/ Touch vibration defaults to on and weak, matching the vendor.
static bool s_touch_haptics = true;
static uint8_t s_haptic_intensity = APP_HAPTIC_WEAK;
// 电量校准日志是调试开关，默认关闭：不建 batt.log、不写卡。
// The battery calibration log is a debug switch, off by default: no batt.log and no card writes.
static bool s_batt_log;
static char s_font[FONT_PATH_MAX];
static char s_system_font[FONT_PATH_MAX];
static uint8_t s_system_size = 120;
static uint8_t s_system_contrast = 100;
static uint8_t s_lock_style;
static char s_wallpaper[288];
static char s_bookmark[288];
static uint8_t s_last_wake;
static uint8_t s_last_boot;
static bool s_pickup_wake;
static uint8_t s_book_px = 48;
static uint8_t s_book_weight = 2;
static int8_t s_book_weight_fine;
static bool s_book_antialias = true;
static char s_book_background[288];
static uint8_t s_reader_full_pages = DISPLAY_REFRESH_READER_CLEANUP_DEFAULT;
static uint8_t s_reader_turn_effect;
static uint8_t s_book_line = 130, s_book_para = 50;
// 四向边距默认左右 24、上下 0，与旧单一边距的横排观感一致。
// Four margins default to 24 px left/right and 0 top/bottom, matching the old horizontal look.
static uint8_t s_book_margin_l = 24, s_book_margin_r = 24;
static uint8_t s_book_margin_t, s_book_margin_b;
static bool s_reader_power_turn;
static bool s_reader_volume_turn;
static bool s_reader_shake_turn;
static bool s_reader_statusbar = true;
// 打开图书的全屏偏好：默认全屏（沿用历史行为），用户切换后记忆。/ Full-screen preference when
// opening a book: full screen by default (historical behavior), remembered once toggled.
static bool s_reader_open_fullscreen = true;
static bool s_reader_hide_images;
static bool s_reader_hold_refresh;
static const char *s_reader_key_names[] = {"rd_key_l", "rd_key_m", "rd_key_r"};
static uint8_t s_reader_hold_action = APP_READER_KEY_HOME;
static uint8_t s_reader_double_action = APP_READER_KEY_NONE;
static uint8_t s_reader_up_action = APP_READER_KEY_NONE;
static uint8_t s_reader_keys[3] = {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT};
// 页眉页脚默认布局沿用历史画面：页眉 书名/时间/电量图标，页脚 章节名/空/页码·进度。
// Default chrome keeps the historical picture: header title/clock/battery icon,
// footer chapter/blank/page·progress.
static const char *s_reader_chrome_names[] = {
    NVS_KEY_CHROME_HEADER_L, NVS_KEY_CHROME_HEADER_M, NVS_KEY_CHROME_HEADER_R,
    NVS_KEY_CHROME_FOOTER_L, NVS_KEY_CHROME_FOOTER_M, NVS_KEY_CHROME_FOOTER_R,
};
static const uint8_t s_reader_chrome_defaults[6] = {
    APP_READER_SLOT_TITLE, APP_READER_SLOT_CLOCK, APP_READER_SLOT_ICON_BATTERY,
    APP_READER_SLOT_CHAPTER, APP_READER_SLOT_BLANK, APP_READER_SLOT_PAGE_PROGRESS,
};
static uint8_t s_reader_chrome[6] = {
    APP_READER_SLOT_TITLE, APP_READER_SLOT_CLOCK, APP_READER_SLOT_ICON_BATTERY,
    APP_READER_SLOT_CHAPTER, APP_READER_SLOT_BLANK, APP_READER_SLOT_PAGE_PROGRESS,
};
static bool s_reader_vertical_turn;
static bool s_reader_progress_bar = true;
static uint8_t s_reader_chrome_heights[2] = {APP_READER_HEADER_HEIGHT_DEFAULT, APP_READER_FOOTER_HEIGHT_DEFAULT};
static bool reader_chrome_heights_valid(uint8_t header, uint8_t footer) {
    return header >= APP_READER_HEADER_HEIGHT_MIN && header <= APP_READER_HEADER_HEIGHT_MAX &&
        (header - APP_READER_HEADER_HEIGHT_MIN) % APP_READER_CHROME_HEIGHT_STEP == 0 &&
        footer >= APP_READER_FOOTER_HEIGHT_MIN && footer <= APP_READER_FOOTER_HEIGHT_MAX &&
        (footer - APP_READER_FOOTER_HEIGHT_MIN) % APP_READER_CHROME_HEIGHT_STEP == 0;
}
static bool s_ble_turner;
// 前光默认关、亮度 40%、中性色温；升级后行为和没有该功能时一致。
// Frontlight defaults to off, 40% brightness and neutral CCT, so upgrading behaves as before.
static bool s_frontlight_on;
static uint8_t s_frontlight_brightness = 40;
static uint8_t s_frontlight_cct = 50;
static bool s_shelf_recent_sort;
// 书内自带字体默认开启：多数带字体的书排版就是靠它才对得上版。
// Embedded book faces are on by default: most books that ship a face only typeset correctly
// with it.
static bool s_book_fonts = true;
// 竖排默认关：只有台湾、日本等竖排读者会主动打开，横排排版不受影响。
// Vertical setting defaults off: only readers who want it turn it on, and horizontal type is
// untouched.
static bool s_book_vertical;
static bool s_book_styles = true;
// 题头样式默认「静页余白」：与历史渲染一致，升级后画面不变。
// The heading style defaults to plain, the historical rendering, so upgrading changes nothing.
static uint8_t s_book_title_style = APP_BOOK_TITLE_STYLE_DEFAULT;
// 题头字号默认基准档：48px 就是历史尺寸，升级后题头只跟着带高居中。
// The heading size defaults to the reference level: 48 px is the historical size, so upgrading only
// centres the heading in the taller band.
static uint8_t s_book_title_size = APP_BOOK_TITLE_SIZE_DEFAULT;

// 题头字号只认合法档位：低于下界的夹到最近一档（0 视为未设置，回到基准档），高于上界的夹到上界，
// 中间值取最近的档。NVS、备份与设置写入都过这里。
// The heading size only takes legal levels: anything below the range snaps to the nearest level (zero
// counts as unset and returns to the reference), anything above clamps to the top, and values in
// between snap to the nearest step. NVS, restore and the setter all come through here.
static uint8_t book_title_size_snap(unsigned px) {
    if (px == 0) return APP_BOOK_TITLE_SIZE_DEFAULT;
    if (px <= APP_BOOK_TITLE_SIZE_MIN) return APP_BOOK_TITLE_SIZE_MIN;
    if (px >= APP_BOOK_TITLE_SIZE_MAX) return APP_BOOK_TITLE_SIZE_MAX;
    unsigned step = (px - APP_BOOK_TITLE_SIZE_MIN + APP_BOOK_TITLE_SIZE_STEP / 2) /
                    APP_BOOK_TITLE_SIZE_STEP;
    return (uint8_t)(APP_BOOK_TITLE_SIZE_MIN + step * APP_BOOK_TITLE_SIZE_STEP);
}
static uint8_t s_book_tracking = -APP_BOOK_TRACK_MIN, s_book_reading_line, s_book_rule_offset = 4;
static uint8_t s_book_indent = 2;
static uint8_t s_book_indent_adjust = 20;
// 默认无操作 5 分钟自动锁屏。
// Default idle lock is five minutes.
static uint8_t s_auto_lock_minutes = 5;
// 自动对时默认开：与设置页开关、time_sync 服务共用同一份运行状态。
// Auto sync defaults on and shares one runtime copy with the settings page and time_sync.
static bool s_auto_time_sync = true;
static bool s_auto_ota = true;
static uint32_t s_time_sync_last;
static uint32_t s_time_sync_try;
static uint8_t s_shelf_style = 2;
static const char s_books_dir[MEDIA_DIR_MAX] = "/sdcard/books";
static const char s_fonts_dir[MEDIA_DIR_MAX] = "/sdcard/fonts";
static void nvs_put_u8(const char* key, uint8_t value);
static void nvs_put_u32(const char* key, uint32_t value);

static bool valid_media_dir(const char* path) {
    if (!path || strncmp(path, "/sdcard/", 8) || !path[8] ||
        strlen(path) >= MEDIA_DIR_MAX || path[strlen(path) - 1] == '/') return false;
    const char *segment = path + 8;
    for (const unsigned char *p = (const unsigned char *)segment; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '\\' || *p == ':') return false;
        if (*p == '/' && (p == (const unsigned char *)segment || p[-1] == '/')) return false;
    }
    for (const char *p = segment; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
        if (!end) break;
        p = end + 1;
    }
    return true;
}

static uint8_t valid_book_px(uint8_t px) {
    return px >= BOOK_READER_PX_MIN && px <= BOOK_READER_PX_MAX ? px : 48;
}

// 仅迁移历史默认名；启动和备份恢复共用规则，不覆盖自定义名称。
// Migrate historical defaults on boot and restore without overwriting custom names.
static void migrate_device_name(char *name, size_t capacity) {
    if (!strcmp(name, "Pico") || !strcmp(name, "kiikoread"))
        strlcpy(name, DEFAULT_DEVICE_NAME, capacity);
}

void app_settings_init(void) {
    esp_err_t err = nvs_flash_init();
    // 设置分区异常时保留原数据供恢复，绝不因空间不足自动擦除用户资料。
    // Preserve settings on NVS errors; a full partition must never silently erase profile data.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init %s, use deep sleep", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t raw = APP_SLEEP_DEEP;
    if (nvs_get_u8(h, NVS_KEY_SLEEP, &raw) == ESP_OK && raw <= APP_SLEEP_OFF) {
        s_sleep = (app_sleep_mode_t)raw;
    }
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_SHUTDOWN_MODE, &raw) == ESP_OK)
        // 保留仅浅睡和定时档，旧值与未知值仍并入彻底关机。/ Keep light-only and timed styles; legacy/unknown values still merge into immediate off.
        s_shutdown_style = raw >= APP_SHUTDOWN_LIGHT && raw <= APP_SHUTDOWN_LIGHT_60
            ? (app_shutdown_style_t)raw : APP_SHUTDOWN_STAGED;
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_HOME_FULL, &raw) == ESP_OK) s_home_full_refresh = raw == 1;
    raw = 0;
    s_home_component = nvs_get_u8(h, NVS_KEY_HOME_COMPONENT, &raw) == ESP_OK && raw < APP_HOME_COMPONENT_COUNT
        ? (raw == 3 ? APP_HOME_CLOCK : (app_home_component_t)raw) : APP_HOME_FOLDER;
    raw = 30;
    s_home_goal_minutes = nvs_get_u8(h, NVS_KEY_HOME_GOAL, &raw) == ESP_OK && raw >= 5 && raw <= 180 && raw % 5 == 0 ? raw : 30;
    raw = 0;
    // 主页刷新默认水波纹；快刷已撤下，旧存档里的 1 也回落到默认，避免没有入口却仍停在快刷。
    // Main pages default to water; fast refresh is retired, so a stored 1 also falls back to
    // the default instead of running with no entry.
    s_main_refresh = nvs_get_u8(h, NVS_KEY_MAIN_REFRESH, &raw) == ESP_OK &&
            (raw == APP_MAIN_REFRESH_NORMAL || raw == APP_MAIN_REFRESH_WATER)
        ? (app_main_refresh_mode_t)raw : APP_MAIN_REFRESH_WATER;
    raw = 0;

    raw = 0;
    // 0 是用户明确选的“关闭”，必须和“没存过”区分开，否则会被默认 5 分钟覆盖。
    // A stored 0 is the user's explicit "off" and must be honored, or the five-minute default
    // would override it.
    if (nvs_get_u8(h, NVS_KEY_AUTO_LOCK, &raw) == ESP_OK && (raw == 0 || raw == 1 || raw == 5 || raw == 10))
        s_auto_lock_minutes = raw;
    if (nvs_get_u8(h, NVS_KEY_AUTO_TIME, &raw) == ESP_OK) s_auto_time_sync = raw != 0;
    else s_auto_time_sync = true;
    s_auto_ota = nvs_get_u8(h, NVS_KEY_AUTO_OTA, &raw) != ESP_OK || raw != 0;
    if (nvs_get_u32(h, NVS_KEY_TSYNC_AT, &s_time_sync_last) != ESP_OK) s_time_sync_last = 0;
    if (nvs_get_u32(h, NVS_KEY_TSYNC_TRY, &s_time_sync_try) != ESP_OK) s_time_sync_try = 0;
    size_t value_len = sizeof(s_device_name);
    if (nvs_get_str(h, NVS_KEY_DEVICE_NAME, s_device_name, &value_len) != ESP_OK || !s_device_name[0])
        strlcpy(s_device_name, DEFAULT_DEVICE_NAME, sizeof(s_device_name));
    migrate_device_name(s_device_name, sizeof(s_device_name));
    value_len = sizeof(s_avatar);
    if (nvs_get_str(h, NVS_KEY_AVATAR, s_avatar, &value_len) != ESP_OK ||
        (s_avatar[0] && strncmp(s_avatar, "/sdcard/", 8))) s_avatar[0] = 0;
    value_len = sizeof(s_status_signature);
    if (nvs_get_str(h, NVS_KEY_STATUS_SIGNATURE, s_status_signature, &value_len) != ESP_OK)
        s_status_signature[0] = 0;
    raw = 1;
    if (nvs_get_u8(h, NVS_KEY_TOUCH_HAPTIC, &raw) == ESP_OK) s_touch_haptics = raw != 0;
    raw = APP_HAPTIC_WEAK;
    if (nvs_get_u8(h, NVS_KEY_HAPTIC_LEVEL, &raw) == ESP_OK && raw <= APP_HAPTIC_STRONG)
        s_haptic_intensity = raw;
    haptics_set_enabled(s_touch_haptics);
    haptics_set_intensity(s_haptic_intensity);
    // 日志开关只在这里读入；PMU 随后初始化，由 app_main 统一下发，避免早于总线就绪。
    // The log switch is only read here; the PMU initialises later and app_main applies it once,
    // so no call runs before the bus is ready.
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_BATT_LOG, &raw) == ESP_OK) s_batt_log = raw != 0;
    size_t font_len = sizeof(s_font);
    if (nvs_get_str(h, NVS_KEY_FONT, s_font, &font_len) != ESP_OK) {
        s_font[0] = '\0';
    }
    font_len = sizeof(s_system_font);
    if (nvs_get_str(h, NVS_KEY_SYS_FONT, s_system_font, &font_len) != ESP_OK)
        s_system_font[0] = 0;
    uint8_t system_size = 120;
    if (nvs_get_u8(h, NVS_KEY_SYS_SIZE, &system_size) == ESP_OK &&
        system_size >= 100 && system_size <= 200 && system_size % 10 == 0)
        s_system_size = system_size;
    uint8_t system_contrast = 100;
    if (nvs_get_u8(h, NVS_KEY_SYS_CONTRAST, &system_contrast) == ESP_OK &&
        system_contrast >= 100 && system_contrast <= 140 && system_contrast % 10 == 0)
        s_system_contrast = system_contrast;
    uint8_t lock_style = 0;
    if (nvs_get_u8(h, NVS_KEY_LOCK_STYLE, &lock_style) == ESP_OK && lock_style <= 3)
        s_lock_style = lock_style;
    size_t wallpaper_len = sizeof(s_wallpaper);
    if (nvs_get_str(h, NVS_KEY_WALLPAPER, s_wallpaper, &wallpaper_len) != ESP_OK ||
        strncmp(s_wallpaper, "/sdcard/", 8)) s_wallpaper[0] = 0;
    size_t bookmark_len = sizeof(s_bookmark);
    if (nvs_get_str(h,NVS_KEY_BOOKMARK,s_bookmark,&bookmark_len)!=ESP_OK || strncmp(s_bookmark,"/sdcard/",8)) s_bookmark[0]=0;
    uint8_t wake = 0;
    if (nvs_get_u8(h, NVS_KEY_WAKE, &wake) == ESP_OK) s_last_wake = wake;
    uint8_t boot = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOT, &boot) == ESP_OK) s_last_boot = boot;
    uint8_t pickup = 0;
    if (nvs_get_u8(h, NVS_KEY_PICKUP, &pickup) == ESP_OK) s_pickup_wake = pickup != 0;
    uint8_t weight = 2;
    s_book_weight = nvs_get_u8(h, NVS_KEY_BOOK_WEIGHT, &weight) == ESP_OK && weight <= 4 ? weight : 2;
    s_book_weight_fine = ((int)s_book_weight-2)*APP_BOOK_WEIGHT_SUBSTEPS;
    uint8_t fine = 32;
    if(nvs_get_u8(h,NVS_KEY_BOOK_WEIGHT_FINE,&fine)==ESP_OK && fine<=64)
        s_book_weight_fine=(int)fine-APP_BOOK_WEIGHT_FINE_MAX;
    uint8_t antialias = 1;
    s_book_antialias = nvs_get_u8(h, NVS_KEY_BOOK_AA, &antialias) != ESP_OK || antialias != 0;
    uint8_t book_px = 48;
    if (nvs_get_u8(h, NVS_KEY_BOOK_PX, &book_px) == ESP_OK) s_book_px = valid_book_px(book_px);
    size_t background_len = sizeof(s_book_background);
    if (nvs_get_str(h, NVS_KEY_BOOK_BACKGROUND, s_book_background, &background_len) != ESP_OK ||
        (s_book_background[0] && strncmp(s_book_background, "/sdcard/pictures/", 17))) s_book_background[0] = 0;
    uint8_t reader_full_pages = DISPLAY_REFRESH_READER_CLEANUP_DEFAULT;
    if (nvs_get_u8(h, NVS_KEY_READER_FULL, &reader_full_pages) == ESP_OK &&
        (reader_full_pages == 0 || reader_full_pages == 5 || reader_full_pages == 10 ||
         reader_full_pages == 15 || reader_full_pages == 30))
        s_reader_full_pages = reader_full_pages;
    uint8_t reader_turn_effect = 0;
    if (nvs_get_u8(h, NVS_KEY_READER_TURN, &reader_turn_effect) == ESP_OK && reader_turn_effect <= 1)
        s_reader_turn_effect = reader_turn_effect;
    uint8_t power_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_POWER_TURN, &power_turn) == ESP_OK) s_reader_power_turn = power_turn == 1;
    uint8_t volume_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_VOLUME_TURN, &volume_turn) == ESP_OK) s_reader_volume_turn = volume_turn == 1;
    uint8_t shake_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_SHAKE_TURN, &shake_turn) == ESP_OK) s_reader_shake_turn = shake_turn == 1;
    // 状态栏显隐：新键优先；老固件只有沉浸位，沉浸=1 迁移为隐藏，两键皆无则默认显示。
    // Status visibility prefers the new key; firmwares before it only had the immersive bit,
    // where 1 meant hidden. With neither key present the bar defaults to shown.
    uint8_t statusbar = 1;
    if (nvs_get_u8(h, NVS_KEY_STATUS, &statusbar) != ESP_OK || statusbar > 1) {
        uint8_t legacy = 0;
        statusbar = nvs_get_u8(h, NVS_KEY_IMMERSIVE, &legacy) == ESP_OK && legacy == 1 ? 0 : 1;
    }
    s_reader_statusbar = statusbar == 1;
    uint8_t open_full = 1;
    if (nvs_get_u8(h, NVS_KEY_OPEN_FULL, &open_full) == ESP_OK && open_full > 1) open_full = 1;
    s_reader_open_fullscreen = open_full == 1;
    uint8_t hold_refresh = 0;
    if (nvs_get_u8(h, NVS_KEY_HOLD_REFRESH, &hold_refresh) == ESP_OK) s_reader_hold_refresh = hold_refresh == 1;
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t value;
        // 短按仍不接受「不操作」；前光开关是有效动作，单独放行。
        // Short presses still reject "no action"; the frontlight toggle is valid and passes on its own.
        if (nvs_get_u8(h, s_reader_key_names[i], &value) == ESP_OK &&
            (value < APP_READER_KEY_NONE || value == APP_READER_KEY_FRONTLIGHT))
            s_reader_keys[i] = value;
    }
    uint8_t hold_action = APP_READER_KEY_HOME;
    s_reader_hold_action = nvs_get_u8(h, NVS_KEY_HOLD_ACTION, &hold_action) == ESP_OK &&
            hold_action < APP_READER_KEY_COUNT && hold_action != APP_READER_KEY_LEGACY_BACK
        ? hold_action : APP_READER_KEY_HOME;
    uint8_t double_action = APP_READER_KEY_NONE;
    s_reader_double_action = nvs_get_u8(h, NVS_KEY_DOUBLE_ACTION, &double_action) == ESP_OK &&
            double_action < APP_READER_KEY_COUNT && double_action != APP_READER_KEY_LEGACY_BACK
        ? double_action : APP_READER_KEY_NONE;
    uint8_t up_action = APP_READER_KEY_NONE;
    s_reader_up_action = nvs_get_u8(h, NVS_KEY_UP_ACTION, &up_action) == ESP_OK &&
            up_action < APP_READER_KEY_COUNT && up_action != APP_READER_KEY_LEGACY_BACK
        ? up_action : APP_READER_KEY_NONE;
    // 槽位内容逐格校验；缺键或越界都保留默认布局，无需迁移标记。
    // Validate slots one by one; missing or out-of-range keys keep the default layout, no migration marker.
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t value;
        if (nvs_get_u8(h, s_reader_chrome_names[i], &value) == ESP_OK && value < APP_READER_SLOT_COUNT)
            s_reader_chrome[i] = value;
    }
    uint8_t keys_v2 = 0;
    uint8_t header = APP_READER_HEADER_HEIGHT_DEFAULT, footer = APP_READER_FOOTER_HEIGHT_DEFAULT;
    nvs_get_u8(h, NVS_KEY_CHROME_HEADER_H, &header);
    nvs_get_u8(h, NVS_KEY_CHROME_FOOTER_H, &footer);
    s_reader_chrome_heights[0] = reader_chrome_heights_valid(header, APP_READER_FOOTER_HEIGHT_DEFAULT)
        ? header : APP_READER_HEADER_HEIGHT_DEFAULT;
    s_reader_chrome_heights[1] = reader_chrome_heights_valid(APP_READER_HEADER_HEIGHT_DEFAULT, footer)
        ? footer : APP_READER_FOOTER_HEIGHT_DEFAULT;
    uint8_t progress_bar = 1;
    nvs_get_u8(h, NVS_KEY_READER_PROGRESS, &progress_bar);
    s_reader_progress_bar = progress_bar != 0;
    bool migrate_reader_defaults = nvs_get_u8(h, NVS_KEY_READER_KEYS_V2, &keys_v2) != ESP_OK || keys_v2 != 1;
    bool old_default_hold = migrate_reader_defaults && s_reader_hold_action == APP_READER_KEY_REFRESH &&
        s_reader_keys[0] == APP_READER_KEY_PREV && s_reader_keys[1] == APP_READER_KEY_TOOLS &&
        s_reader_keys[2] == APP_READER_KEY_NEXT;
    uint8_t vertical_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_VERTICAL_TURN, &vertical_turn) == ESP_OK) s_reader_vertical_turn = vertical_turn == 1;
    uint8_t hide_images = 0, recent_sort = 0, ble_turner = 0, book_fonts = 1, book_vertical = 0;
    if (nvs_get_u8(h, NVS_KEY_HIDE_IMAGES, &hide_images) == ESP_OK) s_reader_hide_images = hide_images == 1;
    if (nvs_get_u8(h, NVS_KEY_BLE_TURNER, &ble_turner) == ESP_OK) s_ble_turner = ble_turner == 1;
    if (nvs_get_u8(h, NVS_KEY_SHELF_RECENT, &recent_sort) == ESP_OK) s_shelf_recent_sort = recent_sort == 1;
    if (nvs_get_u8(h, NVS_KEY_BOOK_FONTS, &book_fonts) == ESP_OK) s_book_fonts = book_fonts == 1;
    if (nvs_get_u8(h, NVS_KEY_BOOK_VERTICAL, &book_vertical) == ESP_OK) s_book_vertical = book_vertical == 1;
    uint8_t book_styles = 1;
    nvs_get_u8(h, NVS_KEY_BOOK_STYLES, &book_styles);
    s_book_styles = book_styles != 0;
    uint8_t title_style = APP_BOOK_TITLE_STYLE_DEFAULT;
    if (nvs_get_u8(h, NVS_KEY_BOOK_TITLE_STYLE, &title_style) == ESP_OK &&
        title_style < APP_BOOK_TITLE_STYLE_COUNT) s_book_title_style = title_style;
    uint8_t title_size = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOK_TITLE_SIZE, &title_size) == ESP_OK)
        s_book_title_size = book_title_size_snap(title_size);
    uint8_t fl_on = 0, fl_bri = 40, fl_cct = 50;
    if (nvs_get_u8(h, NVS_KEY_FRONTLIGHT_ON, &fl_on) == ESP_OK) s_frontlight_on = fl_on == 1;
    if (nvs_get_u8(h, NVS_KEY_FRONTLIGHT_BRI, &fl_bri) == ESP_OK) {
        if (fl_bri < 1) fl_bri = 1;
        else if (fl_bri > 100) fl_bri = 100;
        s_frontlight_brightness = fl_bri;
    }
    if (nvs_get_u8(h, NVS_KEY_FRONTLIGHT_CCT, &fl_cct) == ESP_OK) {
        s_frontlight_cct = fl_cct > 100 ? 100 : fl_cct;
    }
    s_book_tracking = -APP_BOOK_TRACK_MIN;
    uint8_t tracking = 2, reading_line = 0, rule_offset = 4, indent = 2, indent_adjust = 20;
    if (nvs_get_u8(h, NVS_KEY_BOOK_TRACK, &tracking) == ESP_OK) {
        if(tracking<=4)s_book_tracking=(uint8_t)((int)tracking*2-4-APP_BOOK_TRACK_MIN);
        else if(tracking>=128 && tracking<=148) {
            int px=(int)tracking-128-4;
            if(px>APP_BOOK_TRACK_MAX)px=APP_BOOK_TRACK_MAX;
            s_book_tracking=(uint8_t)(px-APP_BOOK_TRACK_MIN);
        } else if(tracking>=192 && tracking<192+APP_BOOK_TRACK_COUNT)s_book_tracking=tracking-192;
    }
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE, &reading_line) == ESP_OK && reading_line <= 2)
        s_book_reading_line = reading_line;
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE_OFFSET, &rule_offset) == ESP_OK && rule_offset <= 8)
        s_book_rule_offset = rule_offset;
    if (nvs_get_u8(h, NVS_KEY_BOOK_INDENT, &indent) == ESP_OK && indent <= 3)
        s_book_indent = indent;
    s_book_indent_adjust = nvs_get_u8(h, NVS_KEY_BOOK_INDENT_ADJUST, &indent_adjust) == ESP_OK && indent_adjust <= 200
        ? indent_adjust : 20;
    uint8_t line = 130, para = 50;
    if (nvs_get_u8(h, NVS_KEY_BOOK_LINE, &line) == ESP_OK) {
        if (line >= 110 && line <= 150) s_book_line = line;
        else if (line == 180) s_book_line = 150;
    }
    if (nvs_get_u8(h, NVS_KEY_BOOK_PARA, &para) == ESP_OK && app_settings_book_paragraph_value(app_settings_book_paragraph_level(para)) == para) s_book_para = para;
    // 旧单一边距只在左右两个新键都缺失时整体接管；只写过一边时，另一边回到默认值，
    // 不能被左边距同步写回的旧键带跑。上下缺省为 0。
    // 旧本地 49..60 px 的边距直接收敛到 48，不回到默认 24。
    // The legacy single margin takes over both sides only when neither new key exists; once one
    // side has been written, the other falls back to its default instead of following the legacy
    // mirror. Top/bottom default to zero. Old local 49..60 px margins converge to 48.
    uint8_t legacy_margin = 24, margin_l = 0, margin_r = 0, margin_t = 0, margin_b = 0;
    const bool has_legacy = nvs_get_u8(h, NVS_KEY_BOOK_MARGIN, &legacy_margin) == ESP_OK;
    if (has_legacy && legacy_margin > 48) legacy_margin = 48;
    const bool has_margin_l = nvs_get_u8(h, NVS_KEY_BOOK_MARGIN_L, &margin_l) == ESP_OK && margin_l <= 96;
    const bool has_margin_r = nvs_get_u8(h, NVS_KEY_BOOK_MARGIN_R, &margin_r) == ESP_OK && margin_r <= 96;
    s_book_margin_l = s_book_margin_r = 24;
    s_book_margin_t = s_book_margin_b = 0;
    if (has_margin_l) s_book_margin_l = margin_l;
    else if (!has_margin_r && has_legacy) s_book_margin_l = legacy_margin;
    if (has_margin_r) s_book_margin_r = margin_r;
    else if (!has_margin_l && has_legacy) s_book_margin_r = legacy_margin;
    if (nvs_get_u8(h, NVS_KEY_BOOK_MARGIN_T, &margin_t) == ESP_OK && margin_t <= 96)
        s_book_margin_t = margin_t;
    if (nvs_get_u8(h, NVS_KEY_BOOK_MARGIN_B, &margin_b) == ESP_OK && margin_b <= 96)
        s_book_margin_b = margin_b;
    uint8_t shelf_style = 2;
    if (nvs_get_u8(h, NVS_KEY_SHELF_STYLE, &shelf_style) == ESP_OK && shelf_style >= 1 && shelf_style <= 5)
        s_shelf_style = shelf_style == 3 || shelf_style == 4 ? 2 : shelf_style;
    uint8_t shelf_v22 = 0;
    bool migrate_shelf = nvs_get_u8(h, NVS_KEY_SHELF_V22, &shelf_v22) != ESP_OK || shelf_v22 != 1;
    bool retired_shelf = shelf_style == 3 || shelf_style == 4;
    if (migrate_shelf) s_shelf_style = 2;
    // 固定媒体根目录；忽略旧版自定义目录键。/ Fixed media roots ignore retired custom-directory keys.
    nvs_close(h);
    if ((migrate_shelf || retired_shelf) && settings_write_open(&h) == ESP_OK) {
        // 首次刷入此版只切换书架外观；随后尊重用户手动选择。/ Set acrylic once on upgrade, then preserve manual choices.
        esp_err_t saved = nvs_set_u8(h, NVS_KEY_SHELF_STYLE, 2);
        if (saved == ESP_OK) saved = nvs_set_u8(h, NVS_KEY_SHELF_V22, 1);
        if (saved == ESP_OK) (void)nvs_commit(h);
        settings_write_close(h);
    }
    // Migrate only the former default preset once; keep all other user mappings.
    // Committed marker makes a later explicit full-refresh choice remain untouched.
    if (migrate_reader_defaults && settings_write_open(&h) == ESP_OK) {
        esp_err_t saved = old_default_hold ? nvs_set_u8(h, NVS_KEY_HOLD_ACTION, APP_READER_KEY_HOME) : ESP_OK;
        if (saved == ESP_OK) saved = nvs_set_u8(h, NVS_KEY_READER_KEYS_V2, 1);
        if (saved == ESP_OK) saved = nvs_commit(h);
        settings_write_close(h);
        if (saved == ESP_OK && old_default_hold) s_reader_hold_action = APP_READER_KEY_HOME;
    }
    ESP_LOGI(
        TAG, "sleep mode %s, font %s, reader weight %d, middle hold %u",
        app_sleep_mode_name(s_sleep),
        s_font[0] != '\0' ? s_font : "(builtin)",
        app_settings_book_weight(), (unsigned)s_reader_hold_action
    );
}

app_sleep_mode_t app_settings_sleep_mode(void) {
    return s_sleep;
}

const char* app_sleep_mode_name(app_sleep_mode_t mode) {
    switch (mode) {
        case APP_SLEEP_LIGHT: return "light";
        case APP_SLEEP_DEEP: return "deep";
        case APP_SLEEP_OFF: return "off";
        default: return "?";
    }
}

void app_settings_set_sleep_mode(app_sleep_mode_t mode) {
    if (mode > APP_SLEEP_OFF) mode = APP_SLEEP_DEEP;
    s_sleep = mode;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_SLEEP, (uint8_t)mode);
    nvs_commit(h);
    settings_write_close(h);
}

app_shutdown_style_t app_settings_shutdown_style(void) { return s_shutdown_style; }
uint8_t app_settings_shutdown_minutes(void) {
    switch (s_shutdown_style) {
        case APP_SHUTDOWN_LIGHT_10: return 10;
        case APP_SHUTDOWN_LIGHT_30: return 30;
        case APP_SHUTDOWN_LIGHT_60: return 60;
        default: return 0;
    }
}
void app_settings_set_shutdown_style(app_shutdown_style_t style) {
    if (style < APP_SHUTDOWN_STAGED || style > APP_SHUTDOWN_LIGHT_60) style = APP_SHUTDOWN_STAGED;
    if (style == s_shutdown_style) return;
    s_shutdown_style = style;
    // 旧固件将仅浅睡/定时档回落为彻底关机。/ Older firmware falls back from light-only/timed styles to immediate off.
    nvs_put_u8(NVS_KEY_SHUTDOWN_MODE, (uint8_t)style);
}

app_home_component_t app_settings_home_component(void) { return s_home_component; }
uint8_t app_settings_home_goal_minutes(void) { return s_home_goal_minutes; }
static esp_err_t home_preference_save(const char *key, uint8_t value) {
    nvs_handle_t h;
    esp_err_t err = settings_write_open(&h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    return err;
}
esp_err_t app_settings_set_home_component(app_home_component_t component) {
    if ((unsigned)component >= APP_HOME_COMPONENT_COUNT || component == 3) return ESP_ERR_INVALID_ARG;
    esp_err_t err = home_preference_save(NVS_KEY_HOME_COMPONENT, component);
    if (err == ESP_OK) s_home_component = component;
    return err;
}
esp_err_t app_settings_set_home_goal_minutes(uint8_t minutes) {
    if (minutes < 5 || minutes > 180 || minutes % 5) return ESP_ERR_INVALID_ARG;
    esp_err_t err = home_preference_save(NVS_KEY_HOME_GOAL, minutes);
    if (err == ESP_OK) s_home_goal_minutes = minutes;
    return err;
}

bool app_settings_home_full_refresh(void) { return s_home_full_refresh; }
app_main_refresh_mode_t app_settings_main_refresh_mode(void) { return s_main_refresh; }
bool app_settings_main_fast_refresh(void) { return s_main_refresh == APP_MAIN_REFRESH_FAST; }
void app_settings_set_main_refresh_mode(app_main_refresh_mode_t mode) {
    // 只接受普通与水波纹；快刷不再有入口，内部调用也被忽略。
    // Accept ordinary and water only; the retired fast mode is ignored even from internal callers.
    if ((mode != APP_MAIN_REFRESH_NORMAL && mode != APP_MAIN_REFRESH_WATER) || mode == s_main_refresh) return;
    s_main_refresh = mode;
    nvs_put_u8(NVS_KEY_MAIN_REFRESH, (uint8_t)mode);
}
uint8_t app_settings_auto_lock_minutes(void) { return s_auto_lock_minutes; }
void app_settings_set_auto_lock_minutes(uint8_t minutes) {
    if (minutes != 0 && minutes != 1 && minutes != 5 && minutes != 10) return;
    if (s_auto_lock_minutes == minutes) return;
    s_auto_lock_minutes = minutes;
    nvs_put_u8(NVS_KEY_AUTO_LOCK, minutes);
}

bool app_settings_auto_time_sync(void) { return s_auto_time_sync; }
bool app_settings_auto_ota(void) { return s_auto_ota; }
void app_settings_set_auto_ota(bool on) {
    if (s_auto_ota == on) return;
    s_auto_ota = on;
    nvs_put_u8(NVS_KEY_AUTO_OTA, on ? 1 : 0);
}
void app_settings_set_auto_time_sync(bool on) {
    if (s_auto_time_sync == on) return;
    s_auto_time_sync = on;
    nvs_put_u8(NVS_KEY_AUTO_TIME, on ? 1 : 0);
}

uint32_t app_settings_time_sync_last(void) { return s_time_sync_last; }
void app_settings_set_time_sync_last(uint32_t utc) {
    if (s_time_sync_last == utc) return;
    s_time_sync_last = utc;
    nvs_put_u32(NVS_KEY_TSYNC_AT, utc);
}

uint32_t app_settings_time_sync_attempt(void) { return s_time_sync_try; }
void app_settings_set_time_sync_attempt(uint32_t utc) {
    if (s_time_sync_try == utc) return;
    s_time_sync_try = utc;
    nvs_put_u32(NVS_KEY_TSYNC_TRY, utc);
}

void app_settings_set_home_full_refresh(bool enabled) {
    if (enabled == s_home_full_refresh) return;
    s_home_full_refresh = enabled;
    nvs_put_u8(NVS_KEY_HOME_FULL, enabled ? 1 : 0);
}

bool app_settings_touch_haptics(void) { return s_touch_haptics; }
void app_settings_set_touch_haptics(bool on) {
    if (on == s_touch_haptics) return;
    s_touch_haptics = on;
    nvs_put_u8(NVS_KEY_TOUCH_HAPTIC, on ? 1 : 0);
    haptics_set_enabled(on);
}

bool app_settings_batt_log(void) { return s_batt_log; }
void app_settings_set_batt_log(bool on) {
    if (on == s_batt_log) return;
    s_batt_log = on;
    nvs_put_u8(NVS_KEY_BATT_LOG, on ? 1 : 0);
    // 立即生效：关闭后下一次唤醒不再建文件；打开后最多一分钟写入第一行。
    // Take effect at once: off stops any further file creation, on writes the first line within a minute.
    device_power_set_batt_log(on);
}

uint8_t app_settings_haptic_intensity(void) { return s_haptic_intensity; }
void app_settings_set_haptic_intensity(uint8_t intensity) {
    if (intensity > APP_HAPTIC_STRONG) intensity = APP_HAPTIC_STRONG;
    if (intensity == s_haptic_intensity) return;
    s_haptic_intensity = intensity;
    nvs_put_u8(NVS_KEY_HAPTIC_LEVEL, intensity);
    haptics_set_intensity(intensity);
}

static bool nvs_put_str(const char *key, const char *value) {
    nvs_handle_t h;
    esp_err_t err = settings_write_open(&h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "save %s open: %s", key, esp_err_to_name(err)); return false; }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save %s: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

const char *app_settings_device_name(void) { return s_device_name; }
void app_settings_set_device_name(const char *name) {
    if (!name || !name[0] || strnlen(name, sizeof(s_device_name)) >= sizeof(s_device_name)) return;
    if (nvs_put_str(NVS_KEY_DEVICE_NAME, name)) strlcpy(s_device_name, name, sizeof(s_device_name));
}
const char *app_settings_avatar_path(void) { return s_avatar; }
void app_settings_set_avatar_path(const char *path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/", 8)) ||
        strnlen(path, sizeof(s_avatar)) >= sizeof(s_avatar)) return;
    if (nvs_put_str(NVS_KEY_AVATAR, path)) strlcpy(s_avatar, path, sizeof(s_avatar));
}
const char *app_settings_status_signature(void) { return s_status_signature; }
void app_settings_set_status_signature(const char *signature) {
    if (!signature || strnlen(signature, sizeof(s_status_signature)) >= sizeof(s_status_signature)) return;
    if (nvs_put_str(NVS_KEY_STATUS_SIGNATURE, signature))
        strlcpy(s_status_signature, signature, sizeof(s_status_signature));
}

const char* app_settings_font_path(void) {
    return s_font;
}

const char* app_settings_system_font_path(void) { return s_system_font; }
void app_settings_set_system_font_path(const char* path) {
    if(!path)path="";
    if(strnlen(path,sizeof(s_system_font))>=sizeof(s_system_font))return;
    if(nvs_put_str(NVS_KEY_SYS_FONT,path))strlcpy(s_system_font,path,sizeof(s_system_font));
}

uint8_t app_settings_system_font_size(void) { return s_system_size; }
void app_settings_set_system_font_size(uint8_t percent) {
    if (percent < 100 || percent > 200 || percent % 10 || percent == s_system_size) return;
    nvs_handle_t h;
    if(settings_write_open(&h)!=ESP_OK)return;
    esp_err_t err=nvs_set_u8(h,NVS_KEY_SYS_SIZE,percent);
    if(err==ESP_OK)err=nvs_commit(h);
    settings_write_close(h);
    if(err==ESP_OK)s_system_size=percent;
}
uint8_t app_settings_system_contrast(void) { return s_system_contrast; }
void app_settings_set_system_contrast(uint8_t percent) {
    if (percent < 100 || percent > 140 || percent % 10 || percent == s_system_contrast) return;
    s_system_contrast = percent;
    nvs_put_u8(NVS_KEY_SYS_CONTRAST, percent);
}
uint8_t app_settings_lock_style(void) { return s_lock_style; }
void app_settings_set_lock_style(uint8_t style) {
    if (style > 3 || style == s_lock_style) return;
    s_lock_style = style;
    nvs_put_u8(NVS_KEY_LOCK_STYLE, style);
}
const char* app_settings_bookmark_path(void) { return s_bookmark; }
void app_settings_set_bookmark_path(const char *path) {
    const char *ext=path?strrchr(path,'.'):NULL;
    if(!path || (path[0] && (strncmp(path,"/sdcard/",8)||!ext||strcasecmp(ext,".png"))) ||
        strnlen(path,sizeof(s_bookmark))>=sizeof(s_bookmark)) return;
    nvs_handle_t h;
    if(settings_write_open(&h)!=ESP_OK)return;
    esp_err_t err=nvs_set_str(h,NVS_KEY_BOOKMARK,path);
    if(err==ESP_OK)err=nvs_commit(h);
    settings_write_close(h);
    if(err==ESP_OK)strlcpy(s_bookmark,path,sizeof(s_bookmark));
}
const char* app_settings_wallpaper_path(void) { return s_wallpaper; }
void app_settings_set_wallpaper_path(const char* path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/", 8)) ||
        strnlen(path, sizeof(s_wallpaper)) >= sizeof(s_wallpaper)) return;
    strlcpy(s_wallpaper, path, sizeof(s_wallpaper));
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_WALLPAPER, s_wallpaper);
    nvs_commit(h);
    settings_write_close(h);
}

static void nvs_put_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    settings_write_close(h);
}

static void nvs_put_u32(const char* key, uint32_t value) {
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return;
    nvs_set_u32(h, key, value);
    nvs_commit(h);
    settings_write_close(h);
}

uint8_t app_settings_last_wake(void) {
    return s_last_wake;
}

void app_settings_set_last_wake(uint8_t src) {
    if (src == s_last_wake) return;
    s_last_wake = src;
    nvs_put_u8(NVS_KEY_WAKE, src);
}

uint8_t app_settings_last_boot(void) {
    return s_last_boot;
}

void app_settings_set_last_boot(uint8_t reason) {
    if (reason == 0 || reason == s_last_boot) return;
    s_last_boot = reason;
    nvs_put_u8(NVS_KEY_BOOT, reason);
}

bool app_settings_pickup_wake(void) {
    return s_pickup_wake;
}

void app_settings_set_pickup_wake(bool on) {
    if (s_pickup_wake == on) return;
    s_pickup_wake = on;
    nvs_put_u8(NVS_KEY_PICKUP, on ? 1 : 0);
}

void app_settings_set_font_path(const char* path) {
    if(!path)path="";
    if(strnlen(path,sizeof(s_font))>=sizeof(s_font))return;
    if(nvs_put_str(NVS_KEY_FONT,path))strlcpy(s_font,path,sizeof(s_font));
}

int8_t app_settings_book_weight(void) { return (int8_t)s_book_weight - 2; }
bool app_settings_book_antialias(void) { return s_book_antialias; }
bool app_settings_set_book_antialias(bool enabled) {
    if (enabled == s_book_antialias) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_BOOK_AA, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_book_antialias = enabled;
    return err == ESP_OK;
}
int8_t app_settings_book_weight_fine(void) {return s_book_weight_fine;}
bool app_settings_set_book_weight_fine(int8_t steps) {
    if(steps < -APP_BOOK_WEIGHT_FINE_MAX || steps > APP_BOOK_WEIGHT_FINE_MAX)return false;
    if(steps==s_book_weight_fine)return true;
    uint8_t legacy=(uint8_t)(steps/APP_BOOK_WEIGHT_SUBSTEPS+2);
    nvs_handle_t h;
    if(settings_write_open(&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_u8(h,NVS_KEY_BOOK_WEIGHT_FINE,steps+APP_BOOK_WEIGHT_FINE_MAX);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_WEIGHT,legacy);
    if(err==ESP_OK)err=nvs_commit(h);
    settings_write_close(h);
    if(err==ESP_OK){s_book_weight=legacy;s_book_weight_fine=steps;}
    return err==ESP_OK;
}
bool app_settings_set_book_weight(int8_t level) {
    return level>=-2&&level<=2&&app_settings_set_book_weight_fine(level*APP_BOOK_WEIGHT_SUBSTEPS);
}

uint8_t app_settings_book_px(void) {
    return s_book_px;
}

void app_settings_set_book_px(uint8_t px) {
    px = valid_book_px(px);
    if (s_book_px == px) return;
    s_book_px = px;
    nvs_put_u8(NVS_KEY_BOOK_PX, px);
}

const char *app_settings_book_background_path(void) { return s_book_background; }
bool app_settings_set_book_background_path(const char *path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/pictures/", 17)) ||
        strnlen(path, sizeof(s_book_background)) >= sizeof(s_book_background) || strstr(path, "/../")) return false;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, NVS_KEY_BOOK_BACKGROUND, path);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err != ESP_OK) return false;
    strlcpy(s_book_background, path, sizeof(s_book_background));
    return true;
}

uint8_t app_settings_reader_full_pages(void) { return s_reader_full_pages; }
void app_settings_set_reader_full_pages(uint8_t pages) {
    if ((pages != 0 && pages != 5 && pages != 10 && pages != 15 && pages != 30) ||
        pages == s_reader_full_pages) return;
    s_reader_full_pages = pages;
    nvs_put_u8(NVS_KEY_READER_FULL, pages);
}
uint8_t app_settings_reader_turn_effect(void) { return s_reader_turn_effect; }
void app_settings_set_reader_turn_effect(uint8_t effect) {
    if (effect > 1 || effect == s_reader_turn_effect) return;
    s_reader_turn_effect = effect;
    nvs_put_u8(NVS_KEY_READER_TURN, effect);
}
bool app_settings_reader_power_turn(void) { return s_reader_power_turn; }
void app_settings_set_reader_power_turn(bool on) {
    if (s_reader_power_turn == on) return;
    s_reader_power_turn = on;
    nvs_put_u8(NVS_KEY_POWER_TURN, on ? 1 : 0);
}
bool app_settings_reader_volume_turn(void) { return s_reader_volume_turn; }
void app_settings_set_reader_volume_turn(bool on) {
    if (s_reader_volume_turn == on) return;
    s_reader_volume_turn = on;
    nvs_put_u8(NVS_KEY_VOLUME_TURN, on ? 1 : 0);
}
bool app_settings_reader_shake_turn(void) { return s_reader_shake_turn; }
void app_settings_set_reader_shake_turn(bool on) {
    if (s_reader_shake_turn == on) return;
    s_reader_shake_turn = on;
    nvs_put_u8(NVS_KEY_SHAKE_TURN, on ? 1 : 0);
}
bool app_settings_reader_statusbar(void) { return s_reader_statusbar; }
void app_settings_set_reader_statusbar(bool on) {
    if (s_reader_statusbar == on) return;
    s_reader_statusbar = on;
    nvs_put_u8(NVS_KEY_STATUS, on ? 1 : 0);
    // 同步旧沉浸位（1=隐藏），旧固件降级后全屏行为保持一致。/ Mirror the retired immersive
    // bit (1=hidden) so a downgrade keeps the same full-screen behavior.
    nvs_put_u8(NVS_KEY_IMMERSIVE, on ? 0 : 1);
}
bool app_settings_reader_open_fullscreen(void) { return s_reader_open_fullscreen; }
void app_settings_set_reader_open_fullscreen(bool on) {
    if (s_reader_open_fullscreen == on) return;
    s_reader_open_fullscreen = on;
    nvs_put_u8(NVS_KEY_OPEN_FULL, on ? 1 : 0);
}
bool app_settings_reader_hide_images(void) { return s_reader_hide_images; }
void app_settings_set_reader_hide_images(bool on) {
    if (s_reader_hide_images == on) return;
    s_reader_hide_images = on;
    nvs_put_u8(NVS_KEY_HIDE_IMAGES, on ? 1 : 0);
}
bool app_settings_book_fonts(void) { return s_book_fonts; }
void app_settings_set_book_fonts(bool on) {
    if (s_book_fonts == on) return;
    s_book_fonts = on;
    nvs_put_u8(NVS_KEY_BOOK_FONTS, on ? 1 : 0);
}
bool app_settings_book_vertical(void) { return s_book_vertical; }
void app_settings_set_book_vertical(bool on) {
    if (s_book_vertical == on) return;
    s_book_vertical = on;
    nvs_put_u8(NVS_KEY_BOOK_VERTICAL, on ? 1 : 0);
}
uint8_t app_settings_book_title_style(void) { return s_book_title_style; }
void app_settings_set_book_title_style(uint8_t style) {
    if (style >= APP_BOOK_TITLE_STYLE_COUNT) style = APP_BOOK_TITLE_STYLE_DEFAULT;
    if (s_book_title_style == style) return;
    s_book_title_style = style;
    nvs_put_u8(NVS_KEY_BOOK_TITLE_STYLE, style);
}
uint8_t app_settings_book_title_size(void) { return s_book_title_size; }
void app_settings_set_book_title_size(uint8_t px) {
    px = book_title_size_snap(px);
    if (s_book_title_size == px) return;
    s_book_title_size = px;
    nvs_put_u8(NVS_KEY_BOOK_TITLE_SIZE, px);
}
bool app_settings_ble_turner(void) { return s_ble_turner; }
app_reader_key_action_t app_settings_reader_key_action(unsigned key) {
    return key < 3 ? (app_reader_key_action_t)s_reader_keys[key] : APP_READER_KEY_NONE;
}
bool app_settings_reader_key_action_allowed(unsigned key, app_reader_key_action_t action) {
    return key < 6 && action >= 0 && action < APP_READER_KEY_COUNT;
}
bool app_settings_set_reader_key_action(unsigned key, app_reader_key_action_t action) {
    if (key >= 3 || !app_settings_reader_key_action_allowed(key, action)) return false;
    if (s_reader_keys[key] == action) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, s_reader_key_names[key], action);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_keys[key] = action;
    return err == ESP_OK;
}
app_reader_key_action_t app_settings_reader_hold_action(void) { return (app_reader_key_action_t)s_reader_hold_action; }
bool app_settings_set_reader_hold_action(app_reader_key_action_t action) {
    if (!app_settings_reader_key_action_allowed(3, action)) return false;
    if (s_reader_hold_action == action) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, action);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_hold_action = action;
    return err == ESP_OK;
}
app_reader_key_action_t app_settings_reader_double_action(void) { return (app_reader_key_action_t)s_reader_double_action; }
bool app_settings_set_reader_double_action(app_reader_key_action_t action) {
    if (!app_settings_reader_key_action_allowed(4, action)) return false;
    if (s_reader_double_action == action) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_DOUBLE_ACTION, action);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_double_action = action;
    return err == ESP_OK;
}
app_reader_key_action_t app_settings_reader_up_action(void) { return (app_reader_key_action_t)s_reader_up_action; }
bool app_settings_set_reader_up_action(app_reader_key_action_t action) {
    if (!app_settings_reader_key_action_allowed(5, action)) return false;
    if (s_reader_up_action == action) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_UP_ACTION, action);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_up_action = action;
    return err == ESP_OK;
}
void app_settings_set_reader_key_preset(unsigned preset) {
    static const uint8_t values[2][3] = {
        {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT},
        {APP_READER_KEY_HOME, APP_READER_KEY_FULLSCREEN, APP_READER_KEY_TOOLS}
    };
    if (preset < 1 || preset > 2) return;
    nvs_handle_t h;
    esp_err_t err = settings_write_open(&h);
    if (err != ESP_OK) return;
    for (unsigned i = 0; i < 3 && err == ESP_OK; ++i) err = nvs_set_u8(h, s_reader_key_names[i], values[preset - 1][i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) {
        memcpy(s_reader_keys, values[preset - 1], sizeof(s_reader_keys));
        s_reader_hold_action = preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH;
    }
}

app_reader_slot_item_t app_settings_reader_chrome_item(unsigned bar, unsigned slot) {
    return bar < 2 && slot < 3
        ? (app_reader_slot_item_t)s_reader_chrome[bar * 3 + slot] : APP_READER_SLOT_BLANK;
}
bool app_settings_set_reader_chrome_item(unsigned bar, unsigned slot, app_reader_slot_item_t item) {
    if (bar >= 2 || slot >= 3 || item >= APP_READER_SLOT_COUNT) return false;
    if (s_reader_chrome[bar * 3 + slot] == item) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, s_reader_chrome_names[bar * 3 + slot], item);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_chrome[bar * 3 + slot] = item;
    return err == ESP_OK;
}
uint8_t app_settings_reader_chrome_height(unsigned bar) {
    return bar < 2 ? s_reader_chrome_heights[bar] : 0;
}
bool app_settings_set_reader_chrome_heights(uint8_t header, uint8_t footer) {
    if (!reader_chrome_heights_valid(header, footer)) return false;
    if (s_reader_chrome_heights[0] == header && s_reader_chrome_heights[1] == footer) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_CHROME_HEADER_H, header);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_CHROME_FOOTER_H, footer);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) {
        s_reader_chrome_heights[0] = header;
        s_reader_chrome_heights[1] = footer;
    }
    return err == ESP_OK;
}
bool app_settings_reader_progress_bar(void) { return s_reader_progress_bar; }
bool app_settings_set_reader_progress_bar(bool on) {
    if (s_reader_progress_bar == on) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_READER_PROGRESS, on ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_reader_progress_bar = on;
    return err == ESP_OK;
}
bool app_settings_set_reader_chrome_default(void) {
    if (!memcmp(s_reader_chrome, s_reader_chrome_defaults, sizeof(s_reader_chrome)) &&
        s_reader_chrome_heights[0] == APP_READER_HEADER_HEIGHT_DEFAULT &&
        s_reader_chrome_heights[1] == APP_READER_FOOTER_HEIGHT_DEFAULT && s_reader_progress_bar) return true;
    nvs_handle_t h;
    esp_err_t err = settings_write_open(&h);
    if (err != ESP_OK) return false;
    for (unsigned i = 0; i < 6 && err == ESP_OK; ++i)
        err = nvs_set_u8(h, s_reader_chrome_names[i], s_reader_chrome_defaults[i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_CHROME_HEADER_H, APP_READER_HEADER_HEIGHT_DEFAULT);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_CHROME_FOOTER_H, APP_READER_FOOTER_HEIGHT_DEFAULT);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_READER_PROGRESS, 1);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) {
        memcpy(s_reader_chrome, s_reader_chrome_defaults, sizeof(s_reader_chrome));
        s_reader_chrome_heights[0] = APP_READER_HEADER_HEIGHT_DEFAULT;
        s_reader_chrome_heights[1] = APP_READER_FOOTER_HEIGHT_DEFAULT;
        s_reader_progress_bar = true;
    }
    return err == ESP_OK;
}

bool app_settings_reader_hold_refresh(void) { return s_reader_hold_refresh; }
bool app_settings_reader_vertical_turn(void) { return s_reader_vertical_turn; }
void app_settings_set_reader_vertical_turn(bool on) {
    if (s_reader_vertical_turn == on) return;
    s_reader_vertical_turn = on;
    nvs_put_u8(NVS_KEY_VERTICAL_TURN, on ? 1 : 0);
}
void app_settings_set_reader_hold_refresh(bool on) {
    if (s_reader_hold_refresh == on) return;
    s_reader_hold_refresh = on;
    nvs_put_u8(NVS_KEY_HOLD_REFRESH, on ? 1 : 0);
}
void app_settings_set_ble_turner(bool on) {
    if (s_ble_turner == on) return;
    s_ble_turner = on;
    nvs_put_u8(NVS_KEY_BLE_TURNER, on ? 1 : 0);
}
bool app_settings_frontlight_on(void) { return s_frontlight_on; }
void app_settings_set_frontlight_on(bool on) {
    if (s_frontlight_on == on) return;
    s_frontlight_on = on;
    nvs_put_u8(NVS_KEY_FRONTLIGHT_ON, on ? 1 : 0);
}
uint8_t app_settings_frontlight_brightness(void) { return s_frontlight_brightness; }
void app_settings_set_frontlight_brightness(uint8_t percent) {
    if (percent < 1) percent = 1;
    else if (percent > 100) percent = 100;
    if (s_frontlight_brightness == percent) return;
    s_frontlight_brightness = percent;
    nvs_put_u8(NVS_KEY_FRONTLIGHT_BRI, percent);
}
uint8_t app_settings_frontlight_cct(void) { return s_frontlight_cct; }
void app_settings_set_frontlight_cct(uint8_t percent) {
    if (percent > 100) percent = 100;
    if (s_frontlight_cct == percent) return;
    s_frontlight_cct = percent;
    nvs_put_u8(NVS_KEY_FRONTLIGHT_CCT, percent);
}
bool app_settings_shelf_recent_sort(void) { return s_shelf_recent_sort; }
void app_settings_set_shelf_recent_sort(bool on) {
    if (s_shelf_recent_sort == on) return;
    s_shelf_recent_sort = on;
    nvs_put_u8(NVS_KEY_SHELF_RECENT, on ? 1 : 0);
}
uint8_t app_settings_book_tracking(void) { return s_book_tracking; }
void app_settings_set_book_tracking(uint8_t index) {
    if (index >= APP_BOOK_TRACK_COUNT || index == s_book_tracking) return;
    s_book_tracking = index;
    nvs_put_u8(NVS_KEY_BOOK_TRACK, 192+index);
}
uint8_t app_settings_book_indent(void) { return s_book_indent; }
void app_settings_set_book_indent(uint8_t em) {
    if (em > 3 || em == s_book_indent) return;
    s_book_indent = em;
    nvs_put_u8(NVS_KEY_BOOK_INDENT, em);
}
int8_t app_settings_book_indent_adjust(void) { return s_book_indent_adjust <= 120 ? (int8_t)((int)s_book_indent_adjust - 20) : (int8_t)(100 - (int)s_book_indent_adjust); }
void app_settings_set_book_indent_adjust(int8_t px) {
    if (px < -100 || px > 100 || px == app_settings_book_indent_adjust()) return;
    // 0..40保留旧配置语义，扩展负值使用121..200。/ Preserve legacy 0..40 encoding; extended negatives use 121..200.
    s_book_indent_adjust = (uint8_t)(px >= -20 ? px + 20 : 100 - px);
    nvs_put_u8(NVS_KEY_BOOK_INDENT_ADJUST, s_book_indent_adjust);
}
uint8_t app_settings_book_reading_line(void) { return s_book_reading_line; }
void app_settings_set_book_reading_line(uint8_t style) {
    if (style > 2 || style == s_book_reading_line) return;
    s_book_reading_line = style;
    nvs_put_u8(NVS_KEY_BOOK_RULE, style);
}
int8_t app_settings_book_reading_line_offset(void) { return (int8_t)((int)s_book_rule_offset - 4) * 2; }
void app_settings_set_book_reading_line_offset(int8_t offset_px) {
    if (offset_px < -8 || offset_px > 8 || offset_px % 2) return;
    uint8_t index = (uint8_t)(offset_px / 2 + 4);
    if (index == s_book_rule_offset) return;
    s_book_rule_offset = index;
    nvs_put_u8(NVS_KEY_BOOK_RULE_OFFSET, index);
}
uint8_t app_settings_book_line_spacing(void) { return s_book_line; }
void app_settings_set_book_line_spacing(uint8_t percent) {
    if (percent < 110 || percent > 150) return;
    if (s_book_line == percent) return;
    s_book_line = percent;
    nvs_put_u8(NVS_KEY_BOOK_LINE, percent);
}
uint8_t app_settings_book_margin_left(void) { return s_book_margin_l; }
void app_settings_set_book_margin_left(uint8_t px) {
    if (px > 96 || s_book_margin_l == px) return;
    s_book_margin_l = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN_L, px);
    // 降级回写：旧固件只认单一 bk_margin 且上限 48，按旧上限夹取。
    // Downgrade write: older firmware reads only the single bk_margin, whose ceiling is 48.
    nvs_put_u8(NVS_KEY_BOOK_MARGIN, px > 48 ? 48 : px);
}
uint8_t app_settings_book_margin_right(void) { return s_book_margin_r; }
void app_settings_set_book_margin_right(uint8_t px) {
    if (px > 96 || s_book_margin_r == px) return;
    s_book_margin_r = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN_R, px);
}
uint8_t app_settings_book_margin_top(void) { return s_book_margin_t; }
void app_settings_set_book_margin_top(uint8_t px) {
    if (px > 96 || s_book_margin_t == px) return;
    s_book_margin_t = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN_T, px);
}
uint8_t app_settings_book_margin_bottom(void) { return s_book_margin_b; }
void app_settings_set_book_margin_bottom(uint8_t px) {
    if (px > 96 || s_book_margin_b == px) return;
    s_book_margin_b = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN_B, px);
}
// 非线性段距保留旧档位，并将25%置于几何中点。/ Nonlinear gaps retain legacy stops with 25% at the geometric midpoint.
uint8_t app_settings_book_paragraph_value(uint8_t level) {
    static const uint8_t values[APP_BOOK_PARAGRAPH_COUNT]={0,5,10,15,20,25,30,40,50,75,100};
    return values[level<APP_BOOK_PARAGRAPH_COUNT?level:APP_BOOK_PARAGRAPH_COUNT-1];
}
uint8_t app_settings_book_paragraph_level(uint8_t percent) {
    uint8_t best=0;
    unsigned distance=255;
    for(uint8_t i=0;i<APP_BOOK_PARAGRAPH_COUNT;++i) {
        int delta=(int)app_settings_book_paragraph_value(i)-(int)percent;
        unsigned d=(unsigned)(delta<0?-delta:delta);
        if(d<distance){distance=d;best=i;}
    }
    return best;
}
bool app_settings_book_styles(void) { return s_book_styles; }
bool app_settings_set_book_styles(bool enabled) {
    if (enabled == s_book_styles) return true;
    nvs_handle_t h;
    if (settings_write_open(&h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_BOOK_STYLES, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err == ESP_OK) s_book_styles = enabled;
    return err == ESP_OK;
}

uint8_t app_settings_book_paragraph_spacing(void) { return s_book_para; }
void app_settings_set_book_paragraph_spacing(uint8_t percent) {
    if (app_settings_book_paragraph_value(app_settings_book_paragraph_level(percent)) != percent) return;
    if (s_book_para == percent) return;
    s_book_para = percent;
    nvs_put_u8(NVS_KEY_BOOK_PARA, percent);
}
uint8_t app_settings_shelf_style(void) { return s_shelf_style; }
void app_settings_set_shelf_style(uint8_t style) {
    if (style < 1 || style > 5) return;
    if (style == 3 || style == 4) style = 2;
    if (s_shelf_style == style) return;
    s_shelf_style = style;
    nvs_put_u8(NVS_KEY_SHELF_STYLE, style);
}
const char* app_settings_books_dir(void) { return s_books_dir; }
const char* app_settings_fonts_dir(void) { return s_fonts_dir; }
#ifndef APP_SETTINGS_BACKUP_ROOT
#define APP_SETTINGS_BACKUP_ROOT "/sdcard"
#endif
#define BACKUP_FILE APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup"
#define BACKUP_TEMP APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.tmp"
#define BACKUP_PREVIOUS APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.previous"

// All fields are bytes, so the v1 disk layout is independent of structure padding.
// 所有字段均为字节，v1 磁盘格式不依赖编译器的结构体填充。
typedef struct {
    char magic[8];
    uint8_t flags[17];
    char font[FONT_PATH_MAX];
    char system_font[FONT_PATH_MAX];
    char wallpaper[288];
    char books_dir[MEDIA_DIR_MAX];
    char fonts_dir[MEDIA_DIR_MAX];
    uint8_t checksum[4];
} settings_backup_v1_t;

typedef struct {
    char device_name[64];
    char avatar[288];
    char status_signature[96];
    uint8_t home_full_refresh;
    uint8_t checksum[4];
} settings_backup_profile_t;

typedef struct {
    wifi_transfer_wifi_backup_t credentials;
    uint8_t checksum[4];
} settings_backup_wifi_t;

// PICOSETA为第10版八字节标识；映射块独立校验，不改旧网络/阅读数据块。
// PICOSETA is the eight-byte v10 identifier; independently check mappings without changing old network/history blocks.
typedef struct { uint8_t actions[3], checksum[4]; } settings_backup_keys_t;
static uint32_t backup_keys_checksum(const settings_backup_keys_t *keys) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 3; ++i) hash = (hash ^ keys->actions[i]) * 16777619u;
    return hash;
}
static bool backup_keys_valid(const settings_backup_keys_t *keys) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)keys->checksum[i] << (8 * i);
    for (unsigned i = 0; i < 3; ++i) if (keys->actions[i] >= APP_READER_KEY_COUNT) return false;
    return stored == backup_keys_checksum(keys);
}

typedef struct { uint8_t action, checksum[4]; } settings_backup_hold_t;
static uint32_t backup_hold_checksum(const settings_backup_hold_t *hold) {
    return (2166136261u ^ hold->action) * 16777619u;
}
static bool backup_hold_valid(const settings_backup_hold_t *hold) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)hold->checksum[i] << (8 * i);
    return hold->action < APP_READER_KEY_COUNT && stored == backup_hold_checksum(hold);
}
// v12 追加独立校验的微调块，保留旧字段偏移与旧备份读取。/ V12 appends a sealed adjustment block, preserving legacy fields and readers.
typedef struct { uint8_t encoded, checksum[4]; } settings_backup_indent_adjust_t;
static uint32_t backup_indent_adjust_checksum(const settings_backup_indent_adjust_t *adjust) {
    return (2166136261u ^ adjust->encoded) * 16777619u;
}
static bool backup_indent_adjust_valid(const settings_backup_indent_adjust_t *adjust) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)adjust->checksum[i] << (8 * i);
    return adjust->encoded <= 200 && stored == backup_indent_adjust_checksum(adjust);
}
// v13 adds a checked reading-weight block; older backups restore native strokes.
typedef struct { uint8_t encoded, checksum[4]; } settings_backup_weight_t;
static uint32_t backup_weight_checksum(const settings_backup_weight_t *weight) {
    return (2166136261u ^ weight->encoded) * 16777619u;
}
static bool backup_weight_valid(const settings_backup_weight_t *weight) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)weight->checksum[i] << (8 * i);
    return (weight->encoded <= 4 || (weight->encoded>=128&&weight->encoded<=192)) && stored == backup_weight_checksum(weight);
}
// v18 adds the reader's remembered full-screen preference; older backups restore the default.
typedef struct { uint8_t open_full, checksum[4]; } settings_backup_open_full_t;
static uint32_t backup_open_full_checksum(const settings_backup_open_full_t *open_full) {
    return (2166136261u ^ open_full->open_full) * 16777619u;
}
static bool backup_open_full_valid(const settings_backup_open_full_t *open_full) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)open_full->checksum[i] << (8 * i);
    return open_full->open_full <= 1 && stored == backup_open_full_checksum(open_full);
}
// v19 追加章节题头样式；旧备份回落默认「静页余白」。
// V19 appends the chapter heading style; older backups fall back to the plain default.
typedef struct { uint8_t style, checksum[4]; } settings_backup_title_style_t;
static uint32_t backup_title_style_checksum(const settings_backup_title_style_t *style) {
    return (2166136261u ^ style->style) * 16777619u;
}
static bool backup_title_style_valid(const settings_backup_title_style_t *style) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)style->checksum[i] << (8 * i);
    return style->style < APP_BOOK_TITLE_STYLE_COUNT && stored == backup_title_style_checksum(style);
}
// v20 追加章节题头字号；旧备份回落基准档 48px。
// V20 appends the chapter heading size; older backups fall back to the 48 px reference level.
typedef struct { uint8_t px, checksum[4]; } settings_backup_title_size_t;
static uint32_t backup_title_size_checksum(const settings_backup_title_size_t *size) {
    return (2166136261u ^ size->px) * 16777619u;
}
static bool backup_title_size_valid(const settings_backup_title_size_t *size) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)size->checksum[i] << (8 * i);
    // 只认正好落在档位上的字号：备份里的数就是画面上的档位，不猜中间值。
    // Only an exact level is accepted: the number in the backup is the level on screen, never a guess.
    return size->px >= APP_BOOK_TITLE_SIZE_MIN && size->px <= APP_BOOK_TITLE_SIZE_MAX &&
           !((size->px - APP_BOOK_TITLE_SIZE_MIN) % APP_BOOK_TITLE_SIZE_STEP) &&
           stored == backup_title_size_checksum(size);
}
// v21 追加中键双击动作；旧备份回落「不操作」。
// V21 appends the middle double-press action; older backups fall back to no action.
typedef struct { uint8_t action, checksum[4]; } settings_backup_double_t;
static uint32_t backup_double_checksum(const settings_backup_double_t *dbl) {
    return (2166136261u ^ dbl->action) * 16777619u;
}
static bool backup_double_valid(const settings_backup_double_t *dbl) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)dbl->checksum[i] << (8 * i);
    return dbl->action < APP_READER_KEY_COUNT && stored == backup_double_checksum(dbl);
}
// v25 追加音量键翻页；旧备份回落关闭。
// V25 appends the volume-key turn switch; older backups fall back to off.
typedef struct { uint8_t on, checksum[4]; } settings_backup_volume_t;
static uint32_t backup_volume_checksum(const settings_backup_volume_t *volume) {
    return (2166136261u ^ volume->on) * 16777619u;
}
static bool backup_volume_valid(const settings_backup_volume_t *volume) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)volume->checksum[i] << (8 * i);
    return volume->on <= 1 && stored == backup_volume_checksum(volume);
}
// v30 追加晃动翻页开关；旧备份回落默认关闭。
// V30 appends the shake-turn switch; older backups fall back to the off default.
typedef struct { uint8_t on, checksum[4]; } settings_backup_shake_t;
static uint32_t backup_shake_checksum(const settings_backup_shake_t *shake) {
    return (2166136261u ^ shake->on) * 16777619u;
}
static bool backup_shake_valid(const settings_backup_shake_t *shake) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)shake->checksum[i] << (8 * i);
    return shake->on <= 1 && stored == backup_shake_checksum(shake);
}
// v31 追加页眉页脚六槽内容；旧备份回落默认布局。
// V31 appends the six header/footer chrome slots; older backups fall back to the default layout.
typedef struct { uint8_t items[6], checksum[4]; } settings_backup_chrome_t;
// v32 追加两条信息栏高度，单独校验并兼容旧备份默认值。
// V32 appends independently checked bar heights, retaining defaults for legacy backups.
typedef struct { uint8_t header, footer, checksum[4]; } settings_backup_chrome_heights_t;
static uint32_t backup_chrome_heights_checksum(const settings_backup_chrome_heights_t *height) {
    return ((2166136261u ^ height->header) * 16777619u ^ height->footer) * 16777619u;
}
static bool backup_chrome_heights_valid(const settings_backup_chrome_heights_t *height) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)height->checksum[i] << (8 * i);
    return reader_chrome_heights_valid(height->header, height->footer) &&
        stored == backup_chrome_heights_checksum(height);
}
// v33 追加版本化首页布局，独立校验且禁止重复实际组件。
// V33 appends an independently checked, versioned Home layout with unique real widgets.
typedef struct { uint8_t value, checksum[4]; } settings_backup_home_regions_t;
static uint32_t backup_home_regions_checksum(const settings_backup_home_regions_t *regions) {
    return (2166136261u ^ regions->value) * 16777619u;
}
static bool backup_home_regions_valid(const settings_backup_home_regions_t *regions) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)regions->checksum[i] << (8 * i);
    return app_home_components_value_valid(regions->value) && stored == backup_home_regions_checksum(regions);
}
// v34 追加进度条显隐；旧备份默认显示。/ V34 appends progress visibility; old backups show it.
typedef struct { uint8_t on, checksum[4]; } settings_backup_progress_bar_t;
static uint32_t backup_progress_bar_checksum(const settings_backup_progress_bar_t *bar) {
    return (2166136261u ^ bar->on) * 16777619u;
}
static bool backup_progress_bar_valid(const settings_backup_progress_bar_t *bar) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)bar->checksum[i] << (8 * i);
    return bar->on <= 1 && stored == backup_progress_bar_checksum(bar);
}
// v35 追加书内样式开关；旧备份保留书内样式。/ V35 appends book-style control; older backups retain book styles.
typedef struct { uint8_t on, checksum[4]; } settings_backup_book_styles_t;
static uint32_t backup_book_styles_checksum(const settings_backup_book_styles_t *styles) {
    return (2166136261u ^ styles->on) * 16777619u;
}
static bool backup_book_styles_valid(const settings_backup_book_styles_t *styles) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)styles->checksum[i] << (8 * i);
    return styles->on <= 1 && stored == backup_book_styles_checksum(styles);
}
static uint32_t backup_chrome_checksum(const settings_backup_chrome_t *chrome) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 6; ++i) hash = (hash ^ chrome->items[i]) * 16777619u;
    return hash;
}
static bool backup_chrome_valid(const settings_backup_chrome_t *chrome) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)chrome->checksum[i] << (8 * i);
    for (unsigned i = 0; i < 6; ++i) if (chrome->items[i] >= APP_READER_SLOT_COUNT) return false;
    return stored == backup_chrome_checksum(chrome);
}
// v26 追加更新上键单击；旧备份回落「不操作」。
// V26 appends the up-key single press; older backups fall back to no action.
typedef struct { uint8_t action, checksum[4]; } settings_backup_up_t;
static uint32_t backup_up_checksum(const settings_backup_up_t *up) {
    return (2166136261u ^ up->action) * 16777619u;
}
static bool backup_up_valid(const settings_backup_up_t *up) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)up->checksum[i] << (8 * i);
    return up->action < APP_READER_KEY_COUNT && stored == backup_up_checksum(up);
}
// v27 追加书内字体、竖排正文与完整关机方式；旧备份回落开启、横排与扩展位。
// V27 appends embedded book fonts, vertical body text and the full shutdown style;
// older backups fall back to on, horizontal and the extension bit.
typedef struct { uint8_t on, checksum[4]; } settings_backup_book_fonts_t;
static uint32_t backup_book_fonts_checksum(const settings_backup_book_fonts_t *fonts) {
    return (2166136261u ^ fonts->on) * 16777619u;
}
static bool backup_book_fonts_valid(const settings_backup_book_fonts_t *fonts) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)fonts->checksum[i] << (8 * i);
    return fonts->on <= 1 && stored == backup_book_fonts_checksum(fonts);
}
typedef struct { uint8_t on, checksum[4]; } settings_backup_book_vertical_t;
static uint32_t backup_book_vertical_checksum(const settings_backup_book_vertical_t *vertical) {
    return (2166136261u ^ vertical->on) * 16777619u;
}
static bool backup_book_vertical_valid(const settings_backup_book_vertical_t *vertical) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)vertical->checksum[i] << (8 * i);
    return vertical->on <= 1 && stored == backup_book_vertical_checksum(vertical);
}
typedef struct { uint8_t style, checksum[4]; } settings_backup_shutdown_style_t;
static uint32_t backup_shutdown_style_checksum(const settings_backup_shutdown_style_t *style) {
    return (2166136261u ^ style->style) * 16777619u;
}
static bool backup_shutdown_style_valid(const settings_backup_shutdown_style_t *style) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)style->checksum[i] << (8 * i);
    return style->style <= APP_SHUTDOWN_LIGHT_60 && stored == backup_shutdown_style_checksum(style);
}
// v28 追加触摸震动开关与强度；旧备份回落开启、弱档。
// V28 appends the touch-vibration switch and intensity; older backups fall back to on and weak.
typedef struct { uint8_t on, level, checksum[4]; } settings_backup_haptic_t;
static uint32_t backup_haptic_checksum(const settings_backup_haptic_t *haptic) {
    return ((2166136261u ^ haptic->on) * 16777619u ^ haptic->level) * 16777619u;
}
static bool backup_haptic_valid(const settings_backup_haptic_t *haptic) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)haptic->checksum[i] << (8 * i);
    return haptic->on <= 1 && haptic->level <= APP_HAPTIC_STRONG && stored == backup_haptic_checksum(haptic);
}
// v29 追加四向边距；旧备份把单一 bk_margin 恢复成左右同值、上下 0。
// V29 appends the four per-side margins; older backups restore the single bk_margin as equal
// left/right insets with zero top/bottom.
typedef struct { uint8_t left, right, top, bottom, checksum[4]; } settings_backup_margins_t;
static uint32_t backup_margins_checksum(const settings_backup_margins_t *margins) {
    const uint8_t values[4] = {margins->left, margins->right, margins->top, margins->bottom};
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 4; ++i) hash = (hash ^ values[i]) * 16777619u;
    return hash;
}
static bool backup_margins_valid(const settings_backup_margins_t *margins) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)margins->checksum[i] << (8 * i);
    return margins->left <= 96 && margins->right <= 96 && margins->top <= 96 &&
           margins->bottom <= 96 && stored == backup_margins_checksum(margins);
}
typedef struct { char path[288]; uint8_t checksum[4]; } settings_backup_background_t;
static uint32_t backup_background_checksum(const settings_backup_background_t *background) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(background->path); ++i) hash = (hash ^ (uint8_t)background->path[i]) * 16777619u;
    return hash;
}
static bool backup_background_valid(const settings_backup_background_t *background) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)background->checksum[i] << (i * 8);
    return strnlen(background->path, sizeof(background->path)) < sizeof(background->path) &&
        (!background->path[0] || !strncmp(background->path, "/sdcard/pictures/", 17)) &&
        stored == backup_background_checksum(background);
}
typedef struct { uint8_t component, minutes, checksum[4]; } settings_backup_home_t;
static uint32_t backup_home_checksum(const settings_backup_home_t *v) {
    return ((2166136261u ^ v->component) * 16777619u ^ v->minutes) * 16777619u;
}
static bool backup_home_valid(const settings_backup_home_t *v) {
    uint32_t crc = 0;
    for (unsigned i = 0; i < 4; ++i) crc |= (uint32_t)v->checksum[i] << (8*i);
    return v->component < APP_HOME_COMPONENT_COUNT && v->minutes >= 5 && v->minutes <= 180 && !(v->minutes % 5) && crc == backup_home_checksum(v);
}
static unsigned backup_version(const settings_backup_v1_t *backup) {
    if (memcmp(backup->magic, "PICOSET", 7)) return 0;
    unsigned char c = backup->magic[7];
    // W 追加 v32 信息栏高度；X 追加 v33 首页布局；Y 追加 v34 进度条显隐；Z 追加 v35 书内样式。
    // W appends v32 bar heights; X appends v33 Home regions; Y appends v34 progress visibility; Z appends v35 book styles.
    // A-F 为旧版；G/H 为 rc9x；K 为上游 rc96/97；L 为本地旧布局；M/N/O/P/Q 为本合并格式；R 追加 v27；S 追加 v28；T 追加 v29；U 追加 v30；V 追加 v31。
    // A-F are legacy; G/H are rc9x; K is upstream rc96/97; L is the old local layout; M/N/O/P/Q are this merge; R appends v27; S appends v28; T appends v29; U appends v30; V appends v31.
    return c >= '1' && c <= '9' ? c - '0' : c == 'A' ? 10 : c == 'B' ? 11 : c == 'C' ? 12 : c == 'D' ? 13 : c == 'E' ? 14 : c == 'F' ? 15 : c == 'G' ? 16 : c == 'H' ? 17 : c == 'I' ? 18 : c == 'J' ? 19 : c == 'K' ? 20 : c == 'L' ? 21 : c == 'M' ? 22 : c == 'N' ? 23 : c == 'O' ? 24 : c == 'P' ? 25 : c == 'Q' ? 26 : c == 'R' ? 27 : c == 'S' ? 28 : c == 'T' ? 29 : c == 'U' ? 30 : c == 'V' ? 31 : c == 'W' ? 32 : c == 'X' ? 33 : c == 'Y' ? 34 : c == 'Z' ? 35 : 0;
}

static void backup_erase_secret(void *ptr, size_t size) {
    volatile uint8_t *bytes = ptr;
    while (size--) *bytes++ = 0;
}

static uint32_t backup_wifi_checksum(const settings_backup_wifi_t *wifi) {
    const uint8_t *bytes = (const uint8_t *)wifi;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_wifi_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void backup_wifi_seal(settings_backup_wifi_t *wifi) {
    uint32_t hash = backup_wifi_checksum(wifi);
    for (int i = 0; i < 4; ++i) wifi->checksum[i] = (uint8_t)(hash >> (i * 8));
}

static bool backup_wifi_valid(const settings_backup_wifi_t *wifi) {
    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= (uint32_t)wifi->checksum[i] << (i * 8);
    return stored == backup_wifi_checksum(wifi) &&
           wifi_transfer_wifi_backup_valid(&wifi->credentials);
}

enum {
    BK_SLEEP, BK_PICKUP, BK_SYS_SIZE, BK_SYS_CONTRAST, BK_LOCK,
    BK_BOOK_PX, BK_SHAKE, BK_FULL_PAGES, BK_TURN, BK_POWER_TURN,
    BK_IMMERSIVE, BK_TRACKING, BK_READING_LINE, BK_LINE_SPACING,
    BK_MARGIN, BK_PARAGRAPH, BK_SHELF,
};

static uint32_t backup_checksum(const settings_backup_v1_t *backup) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_v1_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static uint32_t backup_indent_checksum(const settings_backup_v1_t *backup, uint8_t indent) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(*backup); ++i) hash = (hash ^ data[i]) * 16777619u;
    return (hash ^ indent) * 16777619u;
}
static uint32_t backup_rule_offset_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                            uint8_t rule_offset) {
    return (backup_indent_checksum(backup, indent) ^ rule_offset) * 16777619u;
}
static uint32_t backup_shutdown_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                         uint8_t rule_offset, uint8_t staged_shutdown) {
    return (backup_rule_offset_checksum(backup, indent, rule_offset) ^ staged_shutdown) * 16777619u;
}
static uint32_t backup_profile_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                        uint8_t rule_offset, uint8_t staged_shutdown,
                                        const settings_backup_profile_t *profile) {
    uint32_t hash = backup_shutdown_checksum(backup, indent, rule_offset, staged_shutdown);
    const uint8_t *data = (const uint8_t *)profile;
    for (size_t i = 0; i < offsetof(settings_backup_profile_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static void backup_seal(settings_backup_v1_t *backup) {
    uint32_t value = backup_checksum(backup);
    for (int i = 0; i < 4; ++i) backup->checksum[i] = (uint8_t)(value >> (i * 8));
}

static bool backup_card_ready(void) {
    board_sd_info_t info = {0};
    return board_sd_get_info(&info) == ESP_OK && info.mounted;
}

esp_err_t app_settings_backup_save(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    app_weather_init();
    app_home_components_init(app_weather_view()->enabled);
    settings_backup_v1_t backup = {0};
    // v27：在 v26 的上键单击尾部再追加书内字体、竖排正文与完整关机方式。
    // v27: appends embedded book fonts, vertical body text and the full shutdown style to v26.
    // v28：在 v27 尾部再追加触摸震动开关与强度。
    // v28: appends the touch-vibration switch and intensity after the v27 tail.
    // v29：在 v28 尾部再追加四向边距；旧槽位继续写左边距。
    // v29: appends the four per-side margins after the v28 tail; the legacy slot keeps the left inset.
    // v30：在 v29 尾部再追加晃动翻页开关。
    // v30: appends the shake-turn switch after the v29 tail.
    // v31：在 v30 尾部再追加页眉页脚六槽内容。
    // v31: appends the six header/footer chrome slots after the v30 tail.
    // v32：在 v31 尾部追加页眉页脚高度。/ V32 appends header/footer heights to v31.
    // v33：追加首页上下组件，不改变既有尾块顺序。/ V33 appends Home regions without reordering previous blocks.
    // v34：追加页脚和全屏进度条显隐。/ V34 appends footer/full-screen progress visibility.
    memcpy(backup.magic, "PICOSETZ", sizeof(backup.magic));
    uint8_t *f = backup.flags;
    f[BK_SLEEP] = s_sleep;
    f[BK_PICKUP] = s_pickup_wake;
    f[BK_SYS_SIZE] = s_system_size;
    f[BK_SYS_CONTRAST] = s_system_contrast;
    f[BK_LOCK] = s_lock_style;
    f[BK_BOOK_PX] = s_book_px;
    // v15复用已退役的晃动标志保存关闭抗锯齿；旧备份始终恢复开启。
    // v15 reuses the retired shake flag for disabled AA; older backups always restore AA on.
    f[BK_SHAKE] = s_book_antialias ? 0 : 1;
    f[BK_FULL_PAGES] = s_reader_full_pages;
    f[BK_TURN] = s_reader_turn_effect;
    f[BK_POWER_TURN] = s_reader_power_turn;
    // BK_IMMERSIVE 槽位沿用旧沉浸位语义：1=状态栏隐藏。/ The BK_IMMERSIVE slot keeps the old
    // bit meaning: 1 = status bar hidden.
    f[BK_IMMERSIVE] = s_reader_statusbar ? 0 : 1;
    f[BK_TRACKING] = s_book_tracking;
    f[BK_READING_LINE] = s_book_reading_line;
    f[BK_LINE_SPACING] = s_book_line;
    // 旧槽位只供旧固件读取，上限仍是 48；四向真值在 v29 尾块里。
    // The legacy slot only serves older firmware and keeps its 48 px ceiling; the real four
    // values live in the v29 tail block.
    f[BK_MARGIN] = s_book_margin_l > 48 ? 48 : s_book_margin_l;
    f[BK_PARAGRAPH] = s_book_para;
    f[BK_SHELF] = s_shelf_style;
    strlcpy(backup.font, !strcmp(s_font, "builtin") ? "" : s_font, sizeof(backup.font));
    strlcpy(backup.system_font, s_system_font, sizeof(backup.system_font));
    strlcpy(backup.wallpaper, s_wallpaper, sizeof(backup.wallpaper));
    strlcpy(backup.books_dir, s_books_dir, sizeof(backup.books_dir));
    strlcpy(backup.fonts_dir, s_fonts_dir, sizeof(backup.fonts_dir));
    backup_seal(&backup);
    uint8_t idle_index = s_auto_lock_minutes == 1 ? 1 : s_auto_lock_minutes == 5 ? 2 : s_auto_lock_minutes == 10 ? 3 : 0;
    // 扩展字节 bit0 仍按旧义记「非彻底断电」供旧固件读取（现恒为 1）；v27 独立块保存完整待机档位。
    // Extension bit 0 still records "not the removed full-off style" for older firmware (always 1 now); the v27 block preserves the complete standby style.
    uint8_t extension[7] = {s_book_indent, s_book_rule_offset, (s_shutdown_style != APP_SHUTDOWN_FULL ? 1 : 0) | (idle_index << 1) | ((uint8_t)s_main_refresh << 3)};
    uint32_t extension_hash = backup_shutdown_checksum(&backup, s_book_indent,
                                                        s_book_rule_offset, extension[2]);
    for (int i = 0; i < 4; ++i) extension[i + 3] = (uint8_t)(extension_hash >> (i * 8));
    settings_backup_profile_t profile = {0};
    strlcpy(profile.device_name, s_device_name, sizeof(profile.device_name));
    strlcpy(profile.avatar, s_avatar, sizeof(profile.avatar));
    strlcpy(profile.status_signature, s_status_signature, sizeof(profile.status_signature));
    // v5 及之后复用此字段的预留位；v7 在其后追加 WiFi 与阅读资料。
    // V5+ use reserved bits here; v7 appends WiFi/reading records, and v9 stores main mode in the extension.
    profile.home_full_refresh = (s_home_full_refresh ? 1 : 0) |
                                (s_reader_hide_images ? 2 : 0) |
                                (s_shelf_recent_sort ? 4 : 0) |
                                (s_ble_turner ? 8 : 0) |
                                (s_reader_hold_refresh ? 16 : 0) |
                                (s_reader_vertical_turn ? 32 : 0) |
                                (app_settings_main_fast_refresh() ? 128 : 0);
    uint32_t profile_hash = backup_profile_checksum(&backup, extension[0], extension[1], extension[2], &profile);
    for (int i = 0; i < 4; ++i) profile.checksum[i] = (uint8_t)(profile_hash >> (i * 8));

    settings_backup_keys_t keys = {0};
    memcpy(keys.actions, s_reader_keys, sizeof(keys.actions));
    uint32_t keys_hash = backup_keys_checksum(&keys);
    for (unsigned i = 0; i < 4; ++i) keys.checksum[i] = (uint8_t)(keys_hash >> (8 * i));
    settings_backup_hold_t hold = {.action = s_reader_hold_action};
    uint32_t hold_hash = backup_hold_checksum(&hold);
    for (unsigned i = 0; i < 4; ++i) hold.checksum[i] = (uint8_t)(hold_hash >> (8 * i));
    settings_backup_indent_adjust_t adjust = {.encoded = s_book_indent_adjust};
    uint32_t adjust_hash = backup_indent_adjust_checksum(&adjust);
    for (unsigned i = 0; i < 4; ++i) adjust.checksum[i] = (uint8_t)(adjust_hash >> (8 * i));
    settings_backup_weight_t weight = {.encoded = 128+s_book_weight_fine+APP_BOOK_WEIGHT_FINE_MAX};
    uint32_t weight_hash = backup_weight_checksum(&weight);
    for (unsigned i = 0; i < 4; ++i) weight.checksum[i] = (uint8_t)(weight_hash >> (8 * i));
    settings_backup_open_full_t open_full = {.open_full = s_reader_open_fullscreen ? 1 : 0};
    uint32_t open_full_hash = backup_open_full_checksum(&open_full);
    for (unsigned i = 0; i < 4; ++i) open_full.checksum[i] = (uint8_t)(open_full_hash >> (8 * i));
    settings_backup_title_style_t title_style = {.style = s_book_title_style};
    uint32_t title_style_hash = backup_title_style_checksum(&title_style);
    for (unsigned i = 0; i < 4; ++i) title_style.checksum[i] = (uint8_t)(title_style_hash >> (8 * i));
    settings_backup_title_size_t title_size = {.px = s_book_title_size};
    uint32_t title_size_hash = backup_title_size_checksum(&title_size);
    for (unsigned i = 0; i < 4; ++i) title_size.checksum[i] = (uint8_t)(title_size_hash >> (8 * i));
    settings_backup_double_t double_action = {.action = s_reader_double_action};
    uint32_t double_hash = backup_double_checksum(&double_action);
    for (unsigned i = 0; i < 4; ++i) double_action.checksum[i] = (uint8_t)(double_hash >> (8 * i));
    settings_backup_volume_t volume = {.on = s_reader_volume_turn ? 1 : 0};
    uint32_t volume_hash = backup_volume_checksum(&volume);
    for (unsigned i = 0; i < 4; ++i) volume.checksum[i] = (uint8_t)(volume_hash >> (8 * i));
    settings_backup_up_t up_action = {.action = s_reader_up_action};
    uint32_t up_hash = backup_up_checksum(&up_action);
    for (unsigned i = 0; i < 4; ++i) up_action.checksum[i] = (uint8_t)(up_hash >> (8 * i));
    settings_backup_book_fonts_t book_fonts = {.on = s_book_fonts ? 1 : 0};
    uint32_t book_fonts_hash = backup_book_fonts_checksum(&book_fonts);
    for (unsigned i = 0; i < 4; ++i) book_fonts.checksum[i] = (uint8_t)(book_fonts_hash >> (8 * i));
    settings_backup_book_vertical_t book_vertical = {.on = s_book_vertical ? 1 : 0};
    uint32_t book_vertical_hash = backup_book_vertical_checksum(&book_vertical);
    for (unsigned i = 0; i < 4; ++i) book_vertical.checksum[i] = (uint8_t)(book_vertical_hash >> (8 * i));
    settings_backup_shutdown_style_t shutdown_style = {.style = (uint8_t)s_shutdown_style};
    uint32_t shutdown_style_hash = backup_shutdown_style_checksum(&shutdown_style);
    for (unsigned i = 0; i < 4; ++i) shutdown_style.checksum[i] = (uint8_t)(shutdown_style_hash >> (8 * i));
    settings_backup_haptic_t haptic = {.on = s_touch_haptics ? 1 : 0, .level = s_haptic_intensity};
    uint32_t haptic_hash = backup_haptic_checksum(&haptic);
    for (unsigned i = 0; i < 4; ++i) haptic.checksum[i] = (uint8_t)(haptic_hash >> (8 * i));
    settings_backup_margins_t margins = {.left = s_book_margin_l, .right = s_book_margin_r,
                                         .top = s_book_margin_t, .bottom = s_book_margin_b};
    uint32_t margins_hash = backup_margins_checksum(&margins);
    for (unsigned i = 0; i < 4; ++i) margins.checksum[i] = (uint8_t)(margins_hash >> (8 * i));
    settings_backup_shake_t shake = {.on = s_reader_shake_turn ? 1 : 0};
    uint32_t shake_hash = backup_shake_checksum(&shake);
    for (unsigned i = 0; i < 4; ++i) shake.checksum[i] = (uint8_t)(shake_hash >> (8 * i));
    settings_backup_chrome_t chrome = {0};
    memcpy(chrome.items, s_reader_chrome, sizeof(chrome.items));
    uint32_t chrome_hash = backup_chrome_checksum(&chrome);
    for (unsigned i = 0; i < 4; ++i) chrome.checksum[i] = (uint8_t)(chrome_hash >> (8 * i));
    settings_backup_wifi_t wifi = {0};
    esp_err_t wifi_err = wifi_transfer_export_wifi_backup(&wifi.credentials);
    if (wifi_err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return wifi_err; }
    backup_wifi_seal(&wifi);
    settings_backup_background_t background = {0};
    strlcpy(background.path, s_book_background, sizeof(background.path));
    uint32_t background_hash = backup_background_checksum(&background);
    for (unsigned i = 0; i < 4; ++i) background.checksum[i] = (uint8_t)(background_hash >> (8 * i));

    FILE *file = fopen(BACKUP_TEMP, "wb");
    if (!file) { backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
    bool ok = fwrite(&backup, 1, sizeof(backup), file) == sizeof(backup);
    if (ok) ok = fwrite(extension, 1, sizeof(extension), file) == sizeof(extension);
    if (ok) ok = fwrite(&profile, 1, sizeof(profile), file) == sizeof(profile);
    if (ok) ok = fwrite(&keys, 1, sizeof(keys), file) == sizeof(keys);
    if (ok) ok = fwrite(&hold, 1, sizeof(hold), file) == sizeof(hold);
    if (ok) ok = fwrite(&adjust, 1, sizeof(adjust), file) == sizeof(adjust);
    if (ok) ok = fwrite(&weight, 1, sizeof(weight), file) == sizeof(weight);
    if (ok) ok = fwrite(&wifi, 1, sizeof(wifi), file) == sizeof(wifi);
    if (ok) ok = fwrite(&background, 1, sizeof(background), file) == sizeof(background);
    settings_backup_home_t home = {.component=s_home_component, .minutes=s_home_goal_minutes};
    uint32_t home_hash = backup_home_checksum(&home);
    for (unsigned i = 0; i < 4; ++i) home.checksum[i] = home_hash >> (8*i);
    if (ok) ok = fwrite(&home, 1, sizeof(home), file) == sizeof(home);
    // 记录顺序与版本号一一对应：18 bookmark、21 open_full、22 title_style、23 title_size、24 double_action、25 volume turns、26 up action、27 book fonts/vertical/shutdown；28 haptic、29 margins、30 shake、31 chrome 尾块随后。
    // Record order matches the version numbers: 18 bookmark, 21 open_full, 22 title_style, 23 title_size, 24 double_action, 25 volume turns, 26 up action, 27 book fonts/vertical/shutdown; the 28 haptic, 29 margins, 30 shake and 31 chrome tail blocks follow.
    settings_backup_background_t bookmark = {0};
    strlcpy(bookmark.path,s_bookmark,sizeof(bookmark.path));
    uint32_t bookmark_hash=backup_background_checksum(&bookmark);
    for(unsigned i=0;i<4;++i)bookmark.checksum[i]=bookmark_hash>>(8*i);
    if(ok)ok=fwrite(&bookmark,1,sizeof(bookmark),file)==sizeof(bookmark);
    if (ok) ok = fwrite(&open_full, 1, sizeof(open_full), file) == sizeof(open_full);
    if (ok) ok = fwrite(&title_style, 1, sizeof(title_style), file) == sizeof(title_style);
    if (ok) ok = fwrite(&title_size, 1, sizeof(title_size), file) == sizeof(title_size);
    if (ok) ok = fwrite(&double_action, 1, sizeof(double_action), file) == sizeof(double_action);
    if (ok) ok = fwrite(&volume, 1, sizeof(volume), file) == sizeof(volume);
    if (ok) ok = fwrite(&up_action, 1, sizeof(up_action), file) == sizeof(up_action);
    if (ok) ok = fwrite(&book_fonts, 1, sizeof(book_fonts), file) == sizeof(book_fonts);
    if (ok) ok = fwrite(&book_vertical, 1, sizeof(book_vertical), file) == sizeof(book_vertical);
    if (ok) ok = fwrite(&shutdown_style, 1, sizeof(shutdown_style), file) == sizeof(shutdown_style);
    if (ok) ok = fwrite(&haptic, 1, sizeof(haptic), file) == sizeof(haptic);
    if (ok) ok = fwrite(&margins, 1, sizeof(margins), file) == sizeof(margins);
    if (ok) ok = fwrite(&shake, 1, sizeof(shake), file) == sizeof(shake);
    if (ok) ok = fwrite(&chrome, 1, sizeof(chrome), file) == sizeof(chrome);
    settings_backup_chrome_heights_t heights = {
        .header = s_reader_chrome_heights[0], .footer = s_reader_chrome_heights[1]};
    uint32_t heights_hash = backup_chrome_heights_checksum(&heights);
    for (unsigned i = 0; i < 4; ++i) heights.checksum[i] = (uint8_t)(heights_hash >> (8 * i));
    if (ok) ok = fwrite(&heights, 1, sizeof(heights), file) == sizeof(heights);
    settings_backup_home_regions_t regions = {.value = app_home_components_value()};
    uint32_t regions_hash = backup_home_regions_checksum(&regions);
    for (unsigned i = 0; i < 4; ++i) regions.checksum[i] = (uint8_t)(regions_hash >> (8 * i));
    if (ok) ok = fwrite(&regions, 1, sizeof(regions), file) == sizeof(regions);
    settings_backup_progress_bar_t progress_bar = {.on = s_reader_progress_bar ? 1 : 0};
    uint32_t progress_hash = backup_progress_bar_checksum(&progress_bar);
    for (unsigned i = 0; i < 4; ++i) progress_bar.checksum[i] = (uint8_t)(progress_hash >> (8 * i));
    if (ok) ok = fwrite(&progress_bar, 1, sizeof(progress_bar), file) == sizeof(progress_bar);
    settings_backup_book_styles_t book_styles = {.on = s_book_styles ? 1 : 0};
    uint32_t styles_hash = backup_book_styles_checksum(&book_styles);
    for (unsigned i = 0; i < 4; ++i) book_styles.checksum[i] = (uint8_t)(styles_hash >> (8 * i));
    if (ok) ok = fwrite(&book_styles, 1, sizeof(book_styles), file) == sizeof(book_styles);
    backup_erase_secret(&wifi, sizeof(wifi));
    if (ok) ok = book_history_backup_write(file) == ESP_OK;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    bool rotated = false;
    if (ok && remove(BACKUP_PREVIOUS) != 0 && errno != ENOENT) ok = false;
    if (ok && rename(BACKUP_FILE, BACKUP_PREVIOUS) == 0) rotated = true;
    else if (ok && errno != ENOENT) ok = false;
    if (ok && rename(BACKUP_TEMP, BACKUP_FILE) != 0) ok = false;
    if (!ok && rotated) (void)rename(BACKUP_PREVIOUS, BACKUP_FILE);
    if (ok && rotated) (void)remove(BACKUP_PREVIOUS);
    if (!ok) { (void)remove(BACKUP_TEMP); return ESP_FAIL; }
    return ESP_OK;
}

static bool backup_path_valid(const char *path, size_t capacity) {
    size_t len = strnlen(path, capacity);
    if (len == capacity) return false;
    if (!len) return true;
    if (strncmp(path, "/sdcard/", 8) || !path[8]) return false;
    for (size_t i = 8; i < len; ++i)
        if ((unsigned char)path[i] < 32 || path[i] == '\\' || path[i] == ':') return false;
    for (const char *part = path + 8; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (!end) break;
        part = end + 1;
    }
    return true;
}

static bool backup_valid(const settings_backup_v1_t *backup) {
    const uint8_t *f = backup->flags;
    uint32_t checksum = 0;
    for (int i = 0; i < 4; ++i) checksum |= (uint32_t)backup->checksum[i] << (i * 8);
    const unsigned version = backup_version(backup);
    if (!version || checksum != backup_checksum(backup)) return false;
    // 旧本地 L=21 备份仍可能带 49..60 px 的边距；恢复时统一夹到 48。
    // Old local L=21 backups may carry a 49..60 px margin; restore clamps it to 48.
    const bool legacy_local_layout = version == 21 && backup->magic[7] == 'L';
    if (f[BK_SLEEP] > APP_SLEEP_OFF || f[BK_PICKUP] > 1 ||
        f[BK_SYS_SIZE] < 100 || f[BK_SYS_SIZE] > 200 || f[BK_SYS_SIZE] % 10 ||
        f[BK_SYS_CONTRAST] < 100 || f[BK_SYS_CONTRAST] > 140 || f[BK_SYS_CONTRAST] % 10 ||
        f[BK_LOCK] > 3 || f[BK_BOOK_PX] < BOOK_READER_PX_MIN || f[BK_BOOK_PX] > BOOK_READER_PX_MAX ||
        f[BK_SHAKE] > 1 || (f[BK_FULL_PAGES] != 0 && f[BK_FULL_PAGES] != 5 &&
                            f[BK_FULL_PAGES] != 10 && f[BK_FULL_PAGES] != 15 &&
                            f[BK_FULL_PAGES] != 30) ||
        f[BK_TURN] > 1 || f[BK_POWER_TURN] > 1 || f[BK_IMMERSIVE] > 1 ||
        f[BK_TRACKING] > (legacy_local_layout ? 4 : version>=20 ? APP_BOOK_TRACK_COUNT-1 : version==19 ? 20 : 4) || f[BK_READING_LINE] > 2 ||
        f[BK_LINE_SPACING] < 110 || f[BK_LINE_SPACING] > 150 ||
        f[BK_MARGIN] > (legacy_local_layout ? 60 : 48) ||
        (version>=20 ?
         app_settings_book_paragraph_value(app_settings_book_paragraph_level(f[BK_PARAGRAPH])) != f[BK_PARAGRAPH] :
         f[BK_PARAGRAPH] > 75 || f[BK_PARAGRAPH] % 25) ||
        f[BK_SHELF] < 1 || f[BK_SHELF] > 5) return false;
    if (strnlen(backup->books_dir, sizeof(backup->books_dir)) == sizeof(backup->books_dir) ||
        strnlen(backup->fonts_dir, sizeof(backup->fonts_dir)) == sizeof(backup->fonts_dir)) return false;
    return backup_path_valid(backup->font, sizeof(backup->font)) &&
           backup_path_valid(backup->system_font, sizeof(backup->system_font)) &&
           backup_path_valid(backup->wallpaper, sizeof(backup->wallpaper)) &&
           valid_media_dir(backup->books_dir) && valid_media_dir(backup->fonts_dir);
}

static bool backup_file_exists(const char *path, bool directory) {
    struct stat st;
    return stat(path, &st) == 0 && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

esp_err_t app_settings_backup_restore(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    const char *source = BACKUP_FILE;
    FILE *file = fopen(source, "rb");
    if (!file) { source = BACKUP_PREVIOUS; file = fopen(source, "rb"); }
    if (!file) return ESP_ERR_NOT_FOUND;
    settings_backup_v1_t backup;
    bool ok = fread(&backup, 1, sizeof(backup), file) == sizeof(backup);
    unsigned version = ok ? backup_version(&backup) : 0;
    // 本地旧布局 L=21 把 18..21 用在 open_full/title_style/title_size/double_action，且没有 bookmark。
    // The old local L=21 layout used 18..21 for open_full/title_style/title_size/double_action and had no bookmark.
    const bool legacy_local_layout = ok && version == 21 && backup.magic[7] == 'L';
    settings_backup_hold_t hold = {.action = APP_READER_KEY_HOME};
    uint8_t indent = 2, rule_offset = 4, staged_shutdown = 0;
    settings_backup_indent_adjust_t adjust = {.encoded = 20};
    settings_backup_weight_t weight = {.encoded = 2};
    settings_backup_open_full_t open_full = {.open_full = 1};
    settings_backup_title_style_t title_style = {.style = APP_BOOK_TITLE_STYLE_DEFAULT};
    settings_backup_title_size_t title_size = {.px = APP_BOOK_TITLE_SIZE_DEFAULT};
    settings_backup_double_t double_action = {.action = APP_READER_KEY_NONE};
    settings_backup_volume_t volume = {.on = 0};
    settings_backup_up_t up_action = {.action = APP_READER_KEY_NONE};
    settings_backup_book_fonts_t book_fonts = {.on = 1};
    settings_backup_book_vertical_t book_vertical = {.on = 0};
    settings_backup_shutdown_style_t shutdown_style = {.style = APP_SHUTDOWN_STAGED};
    settings_backup_haptic_t haptic = {.on = 1, .level = APP_HAPTIC_WEAK};
    settings_backup_margins_t margins = {0};
    settings_backup_shake_t shake = {.on = 0};
    settings_backup_home_regions_t regions = {0};
    settings_backup_progress_bar_t progress_bar = {.on = 1};
    settings_backup_book_styles_t book_styles = {.on = 1};
    settings_backup_chrome_heights_t heights = {
        .header = APP_READER_HEADER_HEIGHT_DEFAULT, .footer = APP_READER_FOOTER_HEIGHT_DEFAULT};
    settings_backup_chrome_t chrome = {.items = {
        APP_READER_SLOT_TITLE, APP_READER_SLOT_CLOCK, APP_READER_SLOT_ICON_BATTERY,
        APP_READER_SLOT_CHAPTER, APP_READER_SLOT_BLANK, APP_READER_SLOT_PAGE_PROGRESS}};
    settings_backup_profile_t profile = {.device_name = DEFAULT_DEVICE_NAME};
    settings_backup_keys_t keys = {.actions = {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT}};
    settings_backup_wifi_t wifi = {0};
    bool has_wifi = false;
    settings_backup_background_t background = {0}, bookmark = {0};
    settings_backup_home_t home = {.component=APP_HOME_FOLDER, .minutes=30};
    app_main_refresh_mode_t main_refresh = APP_MAIN_REFRESH_WATER;
    if (ok && !memcmp(backup.magic, "PICOSET2", 8)) {
        uint8_t extension[5];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 1] << (i * 8);
            ok = indent <= 3 && stored == backup_indent_checksum(&backup, indent);
        }
    } else if (ok && !memcmp(backup.magic, "PICOSET3", 8)) {
        uint8_t extension[6];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 2] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 &&
                 stored == backup_rule_offset_checksum(&backup, indent, rule_offset);
        }
    } else if (ok && (version >= 4)) {
        uint8_t extension[7];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1]; staged_shutdown = extension[2];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 3] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 && staged_shutdown <= ((version >= 9) ? 23 : !memcmp(backup.magic, "PICOSET8", 8) ? 7 : 1) &&
                 stored == backup_shutdown_checksum(&backup, indent, rule_offset, staged_shutdown);
        }
        if (ok && (version >= 5)) {
            ok = fread(&profile, 1, sizeof(profile), file) == sizeof(profile);
            if (ok) {
                uint32_t stored = 0;
                for (int i = 0; i < 4; ++i) stored |= (uint32_t)profile.checksum[i] << (i * 8);
                ok = stored == backup_profile_checksum(&backup, indent, rule_offset,
                                                        staged_shutdown, &profile) &&
                     strnlen(profile.device_name, sizeof(profile.device_name)) < sizeof(profile.device_name) &&
                     profile.device_name[0] &&
                     strnlen(profile.status_signature, sizeof(profile.status_signature)) < sizeof(profile.status_signature) &&
                     // 位6仍忽略旧实验模式；v9 在校验扩展内保存主页刷新模式，快刷值回落普通。/ Bit 6 still ignores the retired experiment; v9 stores the main mode in its checked extension, with fast values falling back to ordinary.
                     backup_path_valid(profile.avatar, sizeof(profile.avatar));
            }
        }
    }
    if (ok && (version >= 9)) main_refresh = (app_main_refresh_mode_t)(staged_shutdown >> 3);
    // 旧备份里的快刷（v9 的 1 或更早的位7）不再恢复，统一回落默认水波纹。
    // Fast refresh from older backups (1 in v9 or bit 7 earlier) no longer returns; it falls back to the water default.
    if (main_refresh != APP_MAIN_REFRESH_NORMAL && main_refresh != APP_MAIN_REFRESH_WATER)
        main_refresh = APP_MAIN_REFRESH_WATER;
    if (ok && version >= 10)
        ok = fread(&keys, 1, sizeof(keys), file) == sizeof(keys) && backup_keys_valid(&keys);
    if (ok && version >= 11)
        ok = fread(&hold, 1, sizeof(hold), file) == sizeof(hold) && backup_hold_valid(&hold);
    if (ok && version >= 12)
        ok = fread(&adjust, 1, sizeof(adjust), file) == sizeof(adjust) && backup_indent_adjust_valid(&adjust);
    if (ok && version >= 13)
        ok = fread(&weight, 1, sizeof(weight), file) == sizeof(weight) && backup_weight_valid(&weight) && (version>=17 || weight.encoded<=4);
    long history_position = -1;
    has_wifi = ok && version >= 7;
    if (has_wifi) ok = fread(&wifi, 1, sizeof(wifi), file) == sizeof(wifi) && backup_wifi_valid(&wifi);
    if (ok && version >= 14) ok = fread(&background, 1, sizeof(background), file) == sizeof(background) &&
        backup_background_valid(&background) && backup_path_valid(background.path, sizeof(background.path));
    if (ok && version >= 16) ok = fread(&home, 1, sizeof(home), file) == sizeof(home) && backup_home_valid(&home);
    if (legacy_local_layout) {
        // 旧本地格式：18 open_full、19 title_style、20 title_size、21 double_action。
        // The old local layout: 18 open_full, 19 title_style, 20 title_size, 21 double_action.
        if (ok && version >= 18) ok = fread(&open_full, 1, sizeof(open_full), file) == sizeof(open_full) && backup_open_full_valid(&open_full);
        if (ok && version >= 19) ok = fread(&title_style, 1, sizeof(title_style), file) == sizeof(title_style) && backup_title_style_valid(&title_style);
        if (ok && version >= 20) ok = fread(&title_size, 1, sizeof(title_size), file) == sizeof(title_size) && backup_title_size_valid(&title_size);
        if (ok && version >= 21) ok = fread(&double_action, 1, sizeof(double_action), file) == sizeof(double_action) && backup_double_valid(&double_action);
    } else {
        // 上游/新格式：18 bookmark、21 open_full、22 title_style、23 title_size、24 double_action；25 音量键、26 上键、27 书内字体/竖排/关机方式。
        // Upstream/new layout: 18 bookmark, 21 open_full, 22 title_style, 23 title_size, 24 double_action; 25 volume, 26 up key, 27 book fonts/vertical/shutdown.
        if (ok && version >= 18) ok = fread(&bookmark, 1, sizeof(bookmark), file) == sizeof(bookmark) &&
            backup_background_valid(&bookmark) && backup_path_valid(bookmark.path, sizeof(bookmark.path));
        if (ok && version >= 21) ok = fread(&open_full, 1, sizeof(open_full), file) == sizeof(open_full) && backup_open_full_valid(&open_full);
        if (ok && version >= 22) ok = fread(&title_style, 1, sizeof(title_style), file) == sizeof(title_style) && backup_title_style_valid(&title_style);
        if (ok && version >= 23) ok = fread(&title_size, 1, sizeof(title_size), file) == sizeof(title_size) && backup_title_size_valid(&title_size);
        if (ok && version >= 24) ok = fread(&double_action, 1, sizeof(double_action), file) == sizeof(double_action) && backup_double_valid(&double_action);
    }
    if (ok && version >= 25) ok = fread(&volume, 1, sizeof(volume), file) == sizeof(volume) && backup_volume_valid(&volume);
    if (ok && version >= 26) ok = fread(&up_action, 1, sizeof(up_action), file) == sizeof(up_action) && backup_up_valid(&up_action);
    if (ok && version >= 27) ok = fread(&book_fonts, 1, sizeof(book_fonts), file) == sizeof(book_fonts) && backup_book_fonts_valid(&book_fonts);
    if (ok && version >= 27) ok = fread(&book_vertical, 1, sizeof(book_vertical), file) == sizeof(book_vertical) && backup_book_vertical_valid(&book_vertical);
    if (ok && version >= 27) ok = fread(&shutdown_style, 1, sizeof(shutdown_style), file) == sizeof(shutdown_style) && backup_shutdown_style_valid(&shutdown_style);
    if (ok && version >= 28) ok = fread(&haptic, 1, sizeof(haptic), file) == sizeof(haptic) && backup_haptic_valid(&haptic);
    if (ok && version >= 29) ok = fread(&margins, 1, sizeof(margins), file) == sizeof(margins) && backup_margins_valid(&margins);
    if (ok && version >= 30) ok = fread(&shake, 1, sizeof(shake), file) == sizeof(shake) && backup_shake_valid(&shake);
    if (ok && version >= 31) ok = fread(&chrome, 1, sizeof(chrome), file) == sizeof(chrome) && backup_chrome_valid(&chrome);
    if (ok && version >= 32) ok = fread(&heights, 1, sizeof(heights), file) == sizeof(heights) && backup_chrome_heights_valid(&heights);
    if (ok && version >= 33) ok = fread(&regions, 1, sizeof(regions), file) == sizeof(regions) && backup_home_regions_valid(&regions);
    if (ok && version >= 34) ok = fread(&progress_bar, 1, sizeof(progress_bar), file) == sizeof(progress_bar) && backup_progress_bar_valid(&progress_bar);
    if (ok && version >= 35) ok = fread(&book_styles, 1, sizeof(book_styles), file) == sizeof(book_styles) && backup_book_styles_valid(&book_styles);
    // 旧备份没有本块：chrome 保持默认值并照常写回，与其他设置的恢复语义一致。
    // Older backups lack the block: chrome keeps its defaults and is written back like every other setting.
    bool has_history = ok && version >= 6;
    if (has_history) {
        history_position = ftell(file);
        ok = history_position >= 0 && book_history_backup_validate(file);
    } else if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || !backup_valid(&backup)) {
        backup_erase_secret(&wifi, sizeof(wifi));
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (legacy_local_layout) {
        // 旧本地字距是 0..4 档，对应 -4/-2/0/+2/+4；先还原实际像素再映射到 31 档索引。
        // Old local tracking was 0..4 for -4/-2/0/+2/+4; restore the pixel value, then map to the 31-step index.
        int px = (int)backup.flags[BK_TRACKING] * 2 - 4;
        if (px < APP_BOOK_TRACK_MIN) px = APP_BOOK_TRACK_MIN;
        if (px > APP_BOOK_TRACK_MAX) px = APP_BOOK_TRACK_MAX;
        backup.flags[BK_TRACKING] = (uint8_t)(px - APP_BOOK_TRACK_MIN);
    } else if (version < 20) {
        int px=version<19?(int)backup.flags[BK_TRACKING]*2-4:(int)backup.flags[BK_TRACKING]-4;
        if(px<APP_BOOK_TRACK_MIN)px=APP_BOOK_TRACK_MIN;
        if(px>APP_BOOK_TRACK_MAX)px=APP_BOOK_TRACK_MAX;
        backup.flags[BK_TRACKING]=(uint8_t)(px-APP_BOOK_TRACK_MIN);
    }
    backup.flags[BK_MARGIN] = backup.flags[BK_MARGIN] > 48 ? 48 : backup.flags[BK_MARGIN];
    // v29 之前只有单一边距：按旧横排语义恢复成左右同值、上下 0。
    // Before v29 there was one margin: restore it as equal left/right insets with zero top/bottom.
    if (version < 29) {
        margins.left = margins.right = backup.flags[BK_MARGIN];
        margins.top = margins.bottom = 0;
    }
    if (version < 15) backup.flags[BK_SHAKE] = 0; // 旧晃动标志不改变AA。/ Old shake flags never affect AA.
    // 已删除组件的备份仍可恢复，其余字段不受影响。/ Restore retired widget backups without rejecting other preferences.
    if (home.component == 3) home.component = APP_HOME_CLOCK;
    // The backup contains paths, not the corresponding font or image bytes.
    // 备份只含资源路径；资源已被删除时回退到安全的内建选项。
    if (backup.font[0] && !backup_file_exists(backup.font, false)) backup.font[0] = 0;
    if (backup.system_font[0] && !backup_file_exists(backup.system_font, false)) backup.system_font[0] = 0;
    if (backup.wallpaper[0] && !backup_file_exists(backup.wallpaper, false)) {
        backup.wallpaper[0] = 0;
        if (backup.flags[BK_LOCK] == 1) backup.flags[BK_LOCK] = 0;
    }
    if (!backup.wallpaper[0] && backup.flags[BK_LOCK] == 1) backup.flags[BK_LOCK] = 0;
    // 已移除的书架样式保留旧编号的读取兼容，恢复时改为默认亚克力。
    // Accept retired IDs from old backups and restore the default acrylic style.
    if (backup.flags[BK_SHELF] == 3 || backup.flags[BK_SHELF] == 4) backup.flags[BK_SHELF] = 2;
    if (profile.avatar[0] && !backup_file_exists(profile.avatar, false)) profile.avatar[0] = 0;
    migrate_device_name(profile.device_name, sizeof(profile.device_name));
    strlcpy(backup.books_dir, "/sdcard/books", sizeof(backup.books_dir));
    strlcpy(backup.fonts_dir, "/sdcard/fonts", sizeof(backup.fonts_dir));
    // v27 用独立块保存关机方式；旧备份仍按扩展 bit0 回落（仅浅睡降级为彻底关机）。
    // 「彻底断电」档已移除：备份里读到的旧值并入彻底关机，仅浅睡和定时档原样保留。
    // V27 keeps the shutdown style in its own block; older backups still read extension bit 0,
    // where light-only degrades to full off. The removed full-off style merges into full off from
    // any backup; light-only and timed styles survive as is.
    app_shutdown_style_t shutdown_mode = version >= 27 ? (app_shutdown_style_t)shutdown_style.style
        : ((staged_shutdown & 1) ? APP_SHUTDOWN_STAGED : APP_SHUTDOWN_FULL);
    if (shutdown_mode < APP_SHUTDOWN_LIGHT || shutdown_mode > APP_SHUTDOWN_LIGHT_60)
        shutdown_mode = APP_SHUTDOWN_STAGED;

    nvs_handle_t h;
    esp_err_t err = settings_write_open(&h);
    if (err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return err; }
#define BACKUP_SET_U8(key, index) do { if (err == ESP_OK) err = nvs_set_u8(h, key, backup.flags[index]); } while (0)
#define BACKUP_SET_STR(key, value) do { if (err == ESP_OK) err = nvs_set_str(h, key, value); } while (0)
    for (unsigned i = 0; i < 3 && err == ESP_OK; ++i) err = nvs_set_u8(h, s_reader_key_names[i], keys.actions[i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, hold.action);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_DOUBLE_ACTION, double_action.action);
    BACKUP_SET_U8(NVS_KEY_SLEEP, BK_SLEEP);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHUTDOWN_MODE, (uint8_t)shutdown_mode);
    uint8_t idle_values[] = {0, 1, 5, 10};
    uint8_t idle_minutes = idle_values[(staged_shutdown >> 1) & 3];
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_AUTO_LOCK, idle_minutes);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BLE_TURNER, (profile.home_full_refresh & 8) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_REFRESH, (profile.home_full_refresh & 16) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_VERTICAL_TURN, (profile.home_full_refresh & 32) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_VOLUME_TURN, volume.on);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHAKE_TURN, shake.on);
    for (unsigned i = 0; i < 6 && err == ESP_OK; ++i)
        err = nvs_set_u8(h, s_reader_chrome_names[i], chrome.items[i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_CHROME_HEADER_H, heights.header);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_CHROME_FOOTER_H, heights.footer);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_READER_PROGRESS, progress_bar.on);
    if (err == ESP_OK && version >= 33) err = nvs_set_u8(h, "home_regions", regions.value);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_UP_ACTION, up_action.action);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_FONTS, book_fonts.on);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_VERTICAL, book_vertical.on);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_STYLES, book_styles.on);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_TOUCH_HAPTIC, haptic.on);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HAPTIC_LEVEL, haptic.level);

    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_FULL, profile.home_full_refresh & 1);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_MAIN_REFRESH, (uint8_t)main_refresh);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_COMPONENT, home.component);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_GOAL, home.minutes);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HIDE_IMAGES, (profile.home_full_refresh & 2) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHELF_RECENT, (profile.home_full_refresh & 4) != 0);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_DEVICE_NAME, profile.device_name);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_AVATAR, profile.avatar);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_STATUS_SIGNATURE, profile.status_signature);
    BACKUP_SET_U8(NVS_KEY_PICKUP, BK_PICKUP);
    BACKUP_SET_U8(NVS_KEY_SYS_SIZE, BK_SYS_SIZE);
    BACKUP_SET_U8(NVS_KEY_SYS_CONTRAST, BK_SYS_CONTRAST);
    BACKUP_SET_U8(NVS_KEY_LOCK_STYLE, BK_LOCK);
    BACKUP_SET_U8(NVS_KEY_BOOK_PX, BK_BOOK_PX);

    BACKUP_SET_U8(NVS_KEY_READER_FULL, BK_FULL_PAGES);
    BACKUP_SET_U8(NVS_KEY_READER_TURN, BK_TURN);
    BACKUP_SET_U8(NVS_KEY_POWER_TURN, BK_POWER_TURN);
    // 旧槽位写旧键，状态栏新键按位取反；两键都落盘，升级/降级双向一致。/ Write the legacy
    // key from the legacy slot and the inverted status-bar key; both stay in sync across
    // upgrades and downgrades.
    BACKUP_SET_U8(NVS_KEY_IMMERSIVE, BK_IMMERSIVE);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_STATUS, backup.flags[BK_IMMERSIVE] ? 0 : 1);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_OPEN_FULL, open_full.open_full);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_TITLE_STYLE, title_style.style);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_TITLE_SIZE, title_size.px);
    // 新编码：192..222 对应 -15..+15 px，兼容旧的 0..4 档与 128..148 中间格式。
    // New encoding: 192..222 maps -15..+15 px, alongside the legacy 0..4 and 128..148 forms.
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_TRACK,192+backup.flags[BK_TRACKING]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_INDENT, indent);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_INDENT_ADJUST, adjust.encoded);
    int fine_weight=weight.encoded<=4?((int)weight.encoded-2)*APP_BOOK_WEIGHT_SUBSTEPS:(int)weight.encoded-128-APP_BOOK_WEIGHT_FINE_MAX;
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_WEIGHT, fine_weight/APP_BOOK_WEIGHT_SUBSTEPS+2);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_WEIGHT_FINE, fine_weight+APP_BOOK_WEIGHT_FINE_MAX);
    const bool antialias = version < 15 || backup.flags[BK_SHAKE] == 0;
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_AA, antialias ? 1 : 0);
    BACKUP_SET_U8(NVS_KEY_BOOK_RULE, BK_READING_LINE);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_RULE_OFFSET, rule_offset);
    BACKUP_SET_U8(NVS_KEY_BOOK_LINE, BK_LINE_SPACING);
    BACKUP_SET_U8(NVS_KEY_BOOK_MARGIN, BK_MARGIN);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_MARGIN_L, margins.left);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_MARGIN_R, margins.right);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_MARGIN_T, margins.top);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_MARGIN_B, margins.bottom);
    BACKUP_SET_U8(NVS_KEY_BOOK_PARA, BK_PARAGRAPH);
    BACKUP_SET_U8(NVS_KEY_SHELF_STYLE, BK_SHELF);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHELF_V22, 1);
    if (background.path[0] && !backup_file_exists(background.path, false)) background.path[0] = 0;
    BACKUP_SET_STR(NVS_KEY_BOOK_BACKGROUND, background.path);
    BACKUP_SET_STR(NVS_KEY_FONT, backup.font);
    BACKUP_SET_STR(NVS_KEY_SYS_FONT, backup.system_font);
    if(bookmark.path[0] && !backup_file_exists(bookmark.path,false))bookmark.path[0]=0;
    if(!bookmark.path[0] && backup.flags[BK_LOCK]==3)backup.flags[BK_LOCK]=0;
    BACKUP_SET_STR(NVS_KEY_BOOKMARK, bookmark.path);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_LOCK_STYLE,backup.flags[BK_LOCK]);
    BACKUP_SET_STR(NVS_KEY_WALLPAPER, backup.wallpaper);
    BACKUP_SET_STR(NVS_KEY_BOOKS_DIR, backup.books_dir);
    BACKUP_SET_STR(NVS_KEY_FONTS_DIR, backup.fonts_dir);
#undef BACKUP_SET_U8
#undef BACKUP_SET_STR
    if (err == ESP_OK) err = nvs_commit(h);
    settings_write_close(h);
    if (err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return err; }

    if (version >= 33) app_home_components_apply(regions.value);
    memcpy(s_reader_keys, keys.actions, sizeof(s_reader_keys));
    s_reader_hold_action = hold.action;
    s_reader_double_action = double_action.action;
    memcpy(s_reader_chrome, chrome.items, sizeof(s_reader_chrome));
    s_reader_chrome_heights[0] = heights.header;
    s_reader_chrome_heights[1] = heights.footer;
    s_reader_progress_bar = progress_bar.on != 0;
    s_book_weight = fine_weight/APP_BOOK_WEIGHT_SUBSTEPS+2;
    s_book_weight_fine = fine_weight;
    s_book_antialias = antialias;
    const uint8_t *f = backup.flags;
    s_sleep = (app_sleep_mode_t)f[BK_SLEEP];
    s_shutdown_style = shutdown_mode;
    s_book_fonts = book_fonts.on != 0;
    s_book_vertical = book_vertical.on != 0;
    s_book_styles = book_styles.on != 0;
    s_touch_haptics = haptic.on != 0;
    s_haptic_intensity = haptic.level;
    haptics_set_enabled(s_touch_haptics);
    haptics_set_intensity(s_haptic_intensity);
    s_auto_lock_minutes = idle_minutes;
    s_ble_turner = (profile.home_full_refresh & 8) != 0;
    s_reader_hold_refresh = (profile.home_full_refresh & 16) != 0;
    s_reader_vertical_turn = (profile.home_full_refresh & 32) != 0;
    s_home_full_refresh = (profile.home_full_refresh & 1) != 0;
    s_main_refresh = main_refresh;
    s_reader_hide_images = (profile.home_full_refresh & 2) != 0;
    s_shelf_recent_sort = (profile.home_full_refresh & 4) != 0;
    strlcpy(s_device_name, profile.device_name, sizeof(s_device_name));
    strlcpy(s_avatar, profile.avatar, sizeof(s_avatar));
    strlcpy(s_status_signature, profile.status_signature, sizeof(s_status_signature));
    s_pickup_wake = f[BK_PICKUP];
    s_system_size = f[BK_SYS_SIZE];
    s_system_contrast = f[BK_SYS_CONTRAST];
    s_lock_style = f[BK_LOCK];
    s_book_px = f[BK_BOOK_PX];

    s_reader_full_pages = f[BK_FULL_PAGES];
    s_reader_turn_effect = f[BK_TURN];
    s_reader_power_turn = f[BK_POWER_TURN];
    s_reader_volume_turn = volume.on != 0;
    s_reader_shake_turn = shake.on != 0;
    s_reader_up_action = up_action.action;
    s_reader_statusbar = f[BK_IMMERSIVE] == 0;
    s_reader_open_fullscreen = open_full.open_full == 1;
    s_book_title_style = title_style.style;
    s_book_title_size = title_size.px;
    s_book_tracking = f[BK_TRACKING];
    s_book_indent = indent;
    s_book_indent_adjust = adjust.encoded;
    s_book_reading_line = f[BK_READING_LINE];
    s_book_rule_offset = rule_offset;
    s_book_line = f[BK_LINE_SPACING];
    s_book_margin_l = margins.left;
    s_book_margin_r = margins.right;
    s_book_margin_t = margins.top;
    s_book_margin_b = margins.bottom;
    s_book_para = f[BK_PARAGRAPH];
    s_shelf_style = f[BK_SHELF];
    strlcpy(s_book_background, background.path, sizeof(s_book_background));
    s_home_component = (app_home_component_t)home.component;
    s_home_goal_minutes = home.minutes;
    strlcpy(s_font, backup.font, sizeof(s_font));
    strlcpy(s_system_font, backup.system_font, sizeof(s_system_font));
    strlcpy(s_wallpaper, backup.wallpaper, sizeof(s_wallpaper));
    strlcpy(s_bookmark,bookmark.path,sizeof(s_bookmark));
    if (has_history) {
        file = fopen(source, "rb");
        if (!file) { backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
        esp_err_t history_err = fseek(file, history_position, SEEK_SET) == 0
            ? book_history_backup_restore(file) : ESP_FAIL;
        if (fclose(file) != 0 && history_err == ESP_OK) history_err = ESP_FAIL;
        if (history_err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return history_err; }
    }
    if (has_wifi) {
        esp_err_t wifi_err = wifi_transfer_import_wifi_backup(&wifi.credentials);
        backup_erase_secret(&wifi, sizeof(wifi));
        if (wifi_err != ESP_OK) return wifi_err;
    }
    return ESP_OK;
}
