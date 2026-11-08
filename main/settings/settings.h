/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 里的用户设置：睡眠档、字体路径、上次唤醒/开机原因、拿起唤醒开关。
 *
 * User settings in NVS: sleep mode, font path, last wake/boot reason,
 * pickup-wake switch.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/// 默认深睡。浅睡：按键回原页；拿起唤醒默认关。软睡：SOFT_SLEEP 拉低 EN，再短按开机。关机：EN=0，长按开机。
/// Default is deep. Light: key returns to the page; pickup-wake defaults off. Soft sleep: SOFT_SLEEP drops EN, then a short press boots. Off: EN=0, long-press to boot.
typedef enum {
    APP_SLEEP_LIGHT = 0,
    APP_SLEEP_DEEP = 1,
    APP_SLEEP_OFF = 2,
} app_sleep_mode_t;

void app_settings_init(void);
app_sleep_mode_t app_settings_sleep_mode(void);
void app_settings_set_sleep_mode(app_sleep_mode_t mode);
const char* app_sleep_mode_name(app_sleep_mode_t mode);
/// 锁屏睡眠方式，默认仅浅睡。彻底关机：锁屏出图后立即按原厂脉冲彻底断电，长按电源键约 1 秒冷开机；仅浅睡：锁屏浅睡永不断电，按键/拿起返回关机前页面。
/// 电源菜单「关机」固定立即断电，与本设置无关。
/// APP_SHUTDOWN_FULL 是旧档位：仅为 NVS/备份兼容保留编号，读取时并入彻底关机，界面不再提供。
/// 定时档在每次锁屏浅睡开始后计时，唤醒取消本轮计时；到时彻底关机，长按电源键约 1 秒开机。
/// Lock sleep style, light-only by default. Full off: the lock pulses off factory-style right after the lock art (a one-second power-key hold cold-boots); light-only: lock sleeps forever without powering off, key/pickup returns.
/// The power menu's 关机 always powers off immediately and ignores this setting.
/// APP_SHUTDOWN_FULL is a legacy value kept for NVS/backup compatibility; loads merge into full off and the UI no longer offers it.
/// Timed styles count from each locked light-sleep entry; waking cancels that countdown. Expiry powers off fully, requiring a one-second power-key hold to boot.
typedef enum {
    /// 旧档位保留编号，读取时并入彻底关机。/ Legacy ID; loads merge into full off.
    APP_SHUTDOWN_FULL = 0,
    APP_SHUTDOWN_STAGED = 1,
    APP_SHUTDOWN_LIGHT = 2,
    APP_SHUTDOWN_LIGHT_10 = 3,
    APP_SHUTDOWN_LIGHT_30 = 4,
    APP_SHUTDOWN_LIGHT_60 = 5,
} app_shutdown_style_t;
app_shutdown_style_t app_settings_shutdown_style(void);
void app_settings_set_shutdown_style(app_shutdown_style_t style);
/// 定时浅睡分钟数；非定时档返回 0。/ Timed light-sleep minutes; zero for untimed styles.
uint8_t app_settings_shutdown_minutes(void);
/// 首页进入时强制全刷；默认关闭。/ Full refresh when entering Home; off by default.
bool app_settings_home_full_refresh(void);
void app_settings_set_home_full_refresh(bool enabled);
/// 触摸震动开关与强度（0 弱 / 1 中 / 2 强）；默认开启、弱档。
/// / Touch vibration switch and intensity (0 weak / 1 medium / 2 strong); on and weak by default.
bool app_settings_touch_haptics(void);
void app_settings_set_touch_haptics(bool on);
uint8_t app_settings_haptic_intensity(void);
void app_settings_set_haptic_intensity(uint8_t intensity);
/// 电量校准日志（串口 + TF 卡 batt.log），调试开关，默认关闭；设备本地设置，不进配置备份。
/// Battery calibration log (serial + TF batt.log), a debug switch that defaults off; it stays
/// device-local and is not part of config backups.
bool app_settings_batt_log(void);
void app_settings_set_batt_log(bool on);
/// 已撤下的首页组件与目标，仅为旧 NVS/配置备份保留；首页不再使用。
/// Retired Home widget/goal fields kept for legacy NVS/profile compatibility; Home no longer consumes them.
typedef enum {
    APP_HOME_FOLDER = 0, APP_HOME_CLOCK = 1, APP_HOME_ASTERISK = 2,
    // 3为旧组件的保留编号，不能重排已有配置。/ ID 3 is retired; preserve existing persisted IDs.
    APP_HOME_MOON = 4, APP_HOME_KEHAI = 5, APP_HOME_DALI = 6, APP_HOME_CAT = 7, APP_HOME_HOUSE = 8, APP_HOME_COMPONENT_COUNT = 9
} app_home_component_t;
app_home_component_t app_settings_home_component(void);
esp_err_t app_settings_set_home_component(app_home_component_t component);
uint8_t app_settings_home_goal_minutes(void);
esp_err_t app_settings_set_home_goal_minutes(uint8_t minutes);
/// 主页刷新模式，只控制四个主页面；默认水波纹。/ Main-tab refresh mode; water ripple by default.
typedef enum {
    APP_MAIN_REFRESH_NORMAL = 0, ///< 普通灰阶 / Ordinary grayscale
    APP_MAIN_REFRESH_FAST = 1,   ///< 已撤下，仅为旧存档保留数值 / Retired; kept only for stored profiles
    APP_MAIN_REFRESH_WATER = 2,  ///< 水波纹灰阶 / Staggered grayscale
} app_main_refresh_mode_t;
app_main_refresh_mode_t app_settings_main_refresh_mode(void);
void app_settings_set_main_refresh_mode(app_main_refresh_mode_t mode);
/// 黑白渲染路径的开关；快刷撤下后恒为 false，仅保留内部判断。/ Monochrome path flag; always false now that fast refresh is retired.
bool app_settings_main_fast_refresh(void);
/// 无操作自动锁屏分钟数：0 关闭，1/5/10；默认 5 分钟。/ Idle lock timeout: off, 1/5/10; five minutes by default.
uint8_t app_settings_auto_lock_minutes(void);
void app_settings_set_auto_lock_minutes(uint8_t minutes);
/// 自动对时开关（默认开）。开机/唤醒后距上次成功对时超过阈值才联网校时，深睡期间不可能对时。
/// Auto time sync, on by default. It goes online only when the last successful sync is older than
/// the interval, and only while the host runs; deep sleep cannot reach the network at all.
bool app_settings_auto_time_sync(void);
void app_settings_set_auto_time_sync(bool on);
/// 自动 OTA 检查默认开启；设备本地保存，不随配置备份迁移。/ Automatic OTA checks default on; device-local, excluded from config backups.
bool app_settings_auto_ota(void);
void app_settings_set_auto_ota(bool on);
/// 上次成功对时的 UTC 秒与上次尝试的 UTC 秒（0 = 从未）。设备本地运行记录，不随设置备份迁移。
/// Last successful and last attempted sync in UTC seconds (zero means never). Device-local runtime
/// records; they deliberately stay out of the settings backup.
uint32_t app_settings_time_sync_last(void);
void app_settings_set_time_sync_last(uint32_t utc);
uint32_t app_settings_time_sync_attempt(void);
void app_settings_set_time_sync_attempt(uint32_t utc);
const char *app_settings_device_name(void);
void app_settings_set_device_name(const char *name);
const char *app_settings_avatar_path(void);
void app_settings_set_avatar_path(const char *path);
const char *app_settings_status_signature(void);
void app_settings_set_status_signature(const char *signature);
/// 空路径表示固件内建字体；非空为 SD 上的 TTF。/ Empty path is the built-in font; non-empty is a TTF on the SD card.
const char* app_settings_font_path(void);
void app_settings_set_font_path(const char* path);
/// 系统界面字体；空值使用内建思源黑体并从 TF 卡补字。/ UI font; empty uses built-in Source Han Sans with SD fallback.
const char* app_settings_system_font_path(void);
void app_settings_set_system_font_path(const char* path);
/// 系统字号 100..200%，每档 10%。/ System UI size 100..200%, in 10% steps.
uint8_t app_settings_system_font_size(void);
void app_settings_set_system_font_size(uint8_t percent);
/// 系统界面灰阶对比度，100..140%。/ System UI grayscale contrast, 100..140%.
uint8_t app_settings_system_contrast(void);
void app_settings_set_system_contrast(uint8_t percent);
/// 锁屏样式和壁纸路径。/ Lock style and wallpaper image path.
/// 锁屏：0阅读票根、1壁纸、2书架拼贴、3透明书封。/ Lock: ticket (0), wallpaper (1), collage (2), PNG cover (3).
uint8_t app_settings_lock_style(void);
void app_settings_set_lock_style(uint8_t style);
const char* app_settings_bookmark_path(void);
void app_settings_set_bookmark_path(const char *path);
const char* app_settings_wallpaper_path(void);
void app_settings_set_wallpaper_path(const char* path);
/// 上次浅睡唤醒源（app_wake_source_t），掉电也保留。/ Last light-sleep wake source (app_wake_source_t); kept across power loss.
uint8_t app_settings_last_wake(void);
void app_settings_set_last_wake(uint8_t src);
/// 最近一次非 0 的 PMU wake_reason。STATUS 报 0 时用这个回显。/ Last non-zero PMU wake_reason. Used when STATUS reports 0.
uint8_t app_settings_last_boot(void);
void app_settings_set_last_boot(uint8_t reason);
/// 浅睡拿起唤醒。默认关；有加速度计也不会自动开。/ Light-sleep pickup wake. Defaults off; an accelerometer does not turn it on.
bool app_settings_pickup_wake(void);
void app_settings_set_pickup_wake(bool on);
/// 阅读默认字号，18..90，默认 48。/ Default reading size, 18..90, initially 48.
/// Reading stroke weight -2..2: thin, lighter, native, heavier, bold.
int8_t app_settings_book_weight(void);
#define APP_BOOK_WEIGHT_SUBSTEPS 16
#define APP_BOOK_WEIGHT_FINE_MAX 32
/// 一档为1/16像素，旧像素值继续兼容。/ One step is 1/16 pixel; legacy pixel values remain compatible.
int8_t app_settings_book_weight_fine(void);
bool app_settings_set_book_weight_fine(int8_t steps);
bool app_settings_set_book_weight(int8_t level);
/// 阅读抗锯齿默认开启；关闭只改变字形边缘，不改变排版或图片。/ Reading AA defaults on; disabling affects glyph edges, not layout or images.
bool app_settings_book_antialias(void);
bool app_settings_set_book_antialias(bool enabled);
uint8_t app_settings_book_px(void);
/// 无效字号恢复 48。/ Invalid sizes fall back to 48.
void app_settings_set_book_px(uint8_t px);
/// Reading backgrounds are selected from /sdcard/pictures; empty means white.
const char *app_settings_book_background_path(void);
bool app_settings_set_book_background_path(const char *path);
/// 阅读时每 5/10/15/30 页全刷；0 关闭周期全刷，手动全刷仍可用。
/// Full-screen cleanup every 5/10/15/30 turns; 0 disables periodic cleanup only.
uint8_t app_settings_reader_full_pages(void);
void app_settings_set_reader_full_pages(uint8_t pages);
/// 阅读翻页效果：0默认，1采用PR17快速水波纹；沿用原设置值和默认效果。/ Reader turn effect: 0 default, 1 PR17 fast ripple; retain stored values and the default effect.
uint8_t app_settings_reader_turn_effect(void);
void app_settings_set_reader_turn_effect(uint8_t effect);
/// 阅读正文里短按电源键翻下一页；默认关闭。/ Short power press turns forward only in reader body; off by default.
bool app_settings_reader_power_turn(void);
void app_settings_set_reader_power_turn(bool on);
/// 阅读正文里用侧键音量键翻页：音量+上一页、音量-下一页；默认关闭。
/// Turn pages with the side volume keys in the reader body: up goes back, down forward; off by default.
bool app_settings_reader_volume_turn(void);
void app_settings_set_reader_volume_turn(bool on);
/// 阅读正文里横向晃动翻页：左晃上一页、右晃下一页；默认关闭。
/// Shake the reader body horizontally to turn: left goes back, right forward; off by default.
bool app_settings_reader_shake_turn(void);
void app_settings_set_reader_shake_turn(bool on);
/// 阅读页顶部状态栏显隐；关闭后全屏与非全屏一并隐藏。/ Show or hide the reader status bar; off hides it in both full screen and windowed modes.
bool app_settings_reader_statusbar(void);
void app_settings_set_reader_statusbar(bool on);
/// 打开图书时的全屏偏好：记住用户上次的全屏/非全屏模式，从书架进书沿用；默认全屏。
/// Full-screen preference when opening a book: remember the user's last mode for shelf
/// entries; full screen by default.
bool app_settings_reader_open_fullscreen(void);
void app_settings_set_reader_open_fullscreen(bool on);
/// 阅读时跳过书内插图页；原图仍保留在 EPUB 中。/ Skip inline illustrations while reading without changing the EPUB.
bool app_settings_reader_hide_images(void);
void app_settings_set_reader_hide_images(bool on);
/// 使用书内自带 TTF（EPUB @font-face 或包内字体）；默认开启，关掉后正文回到系统字体。
/// Use embedded book TTFs (CSS @font-face or a packaged face); on by default. Turning it off
/// puts body text back on the system face.
bool app_settings_book_fonts(void);
void app_settings_set_book_fonts(bool on);
/// 竖排正文（自上而下成列、列序自右向左）；默认关闭，只影响正文排版与插图分页。
/// Vertical body text (columns run down, ordered right to left); off by default, and only the
/// body layout and illustration pagination are affected.
bool app_settings_book_vertical(void);
void app_settings_set_book_vertical(bool on);
/// 章节题头字号：章名字号 px 八档，编号恒为它的一半，带内几何按它缩放；默认档等于历史尺寸。
/// 上限受 294 px 题头带约束：84 px 时细线离带底只剩 2 px，78 px 保留 13 px，取 78 为产品上限。
/// Chapter heading size: eight levels of chapter-title px with the label always half of it, scaling
/// the geometry inside the heading band; the default level is the historical size. The 294 px band
/// caps this at 84 px with only 2 px left below the closing rule, so 78 px keeps 13 px and is the cap.
#define APP_BOOK_TITLE_SIZE_MIN 36      ///< 最小章名 px / Smallest chapter-title px
#define APP_BOOK_TITLE_SIZE_MAX 78      ///< 最大章名 px / Largest chapter-title px
#define APP_BOOK_TITLE_SIZE_STEP 6      ///< 档位步长 / Step between levels
#define APP_BOOK_TITLE_SIZE_DEFAULT 48  ///< 历史尺寸，升级不变画面 / Historical size
/// 阅读页章节题头字号（章名 px）；越界值夹到最近一档，0 视为未设置并回到基准档。
/// Reader chapter heading size in chapter-title px; out-of-range values snap to the nearest level
/// and zero counts as unset and returns to the reference.
uint8_t app_settings_book_title_size(void);
void app_settings_set_book_title_size(uint8_t px);
/// 章节题头样式；静页余白是历史渲染，默认沿用，老用户升级后画面不变。
/// Chapter heading styles; plain is the historical rendering and stays the default, so upgrading
/// changes nothing on screen.
typedef enum {
    APP_BOOK_TITLE_STYLE_EMBLEM, ///< 中央章徽：胶囊编号居中悬于章名上方 / Centred emblem
    APP_BOOK_TITLE_STYLE_FRAME,  ///< 边框索引：细框加左侧粗边 / Framed index
    APP_BOOK_TITLE_STYLE_MARK,   ///< 左锋题签：左侧竖线加左对齐文字 / Left mark
    APP_BOOK_TITLE_STYLE_PLAIN,  ///< 静页余白：编号、章名、细线居中 / Plain quiet page
    APP_BOOK_TITLE_STYLE_COUNT
} app_book_title_style_t;
#define APP_BOOK_TITLE_STYLE_DEFAULT APP_BOOK_TITLE_STYLE_PLAIN
/// 阅读页章节题头样式，越界值回落到默认。/ Reader chapter heading style; an out-of-range value falls back to the default.
uint8_t app_settings_book_title_style(void);
void app_settings_set_book_title_style(uint8_t style);
typedef enum {
    APP_READER_KEY_PREV, ///< 上一页 / Previous page
    APP_READER_KEY_NEXT, ///< 下一页 / Next page
    APP_READER_KEY_TOOLS, ///< 阅读工具栏 / Reader toolbar
    APP_READER_KEY_FULLSCREEN, ///< 切换全屏 / Toggle full screen
    APP_READER_KEY_HOME, ///< 返回主页（本次开书来源） / Return to the opening origin
    APP_READER_KEY_REFRESH, ///< 手动全刷 / Manual full refresh
    APP_READER_KEY_NONE, ///< 不操作 / No action
    /// 本地测试固件把 7 存成已撤销的「返回」动作；保留占位，免得旧值在升级后被读成前光开关。
    /// Local test builds stored 7 for a withdrawn back action; keep it reserved so a stale
    /// value never turns into the frontlight toggle.
    APP_READER_KEY_LEGACY_BACK,
    APP_READER_KEY_FRONTLIGHT, ///< 开关前光 / Toggle the frontlight
    APP_READER_KEY_COUNT
} app_reader_key_action_t;
/// 三个短按功能，0/1/2对应左/中/右；长按独立配置。/ Short actions indexed left/middle/right, with a separate hold action.
app_reader_key_action_t app_settings_reader_key_action(unsigned key);
/// 仅验证按键与动作范围，不强制工具栏映射；3代表中键长按，4代表中键双击，5代表上键单击。/ Validate key/action ranges without requiring a toolbar mapping; 3 is the middle hold, 4 the middle double press and 5 the up key.
bool app_settings_reader_key_action_allowed(unsigned key, app_reader_key_action_t action);
/// 返回是否成功保存；失败不改变映射。/ Return successful persistence, leaving the mapping unchanged on failure.
bool app_settings_set_reader_key_action(unsigned key, app_reader_key_action_t action);
/// 中键长按，默认手动全刷；与短按独立保存。/ Middle hold defaults to full refresh and persists independently.
app_reader_key_action_t app_settings_reader_hold_action(void);
bool app_settings_set_reader_hold_action(app_reader_key_action_t action);
/// 中键双击，默认不操作；绑定后单击需等双击窗口结束才生效。/ Middle double press defaults to no action; once bound, a single click waits for the double-press window.
app_reader_key_action_t app_settings_reader_double_action(void);
bool app_settings_set_reader_double_action(app_reader_key_action_t action);
/// 上键单击（机身顶部实体键），默认不操作；「开关前光」绑定在阅读正文外的所有页面同样生效，其余动作仍只在阅读正文生效。/ Up-key single press (top physical key), no action by default; the frontlight toggle also works on every page outside the reader body, while other actions stay reader-body-only.
app_reader_key_action_t app_settings_reader_up_action(void);
bool app_settings_set_reader_up_action(app_reader_key_action_t action);
/// 一次提交整套推荐设置，1为翻页，2为首页/全屏/工具栏。/ Commit preset 1 (page turns) or 2 (Home/full screen/tools) together.
void app_settings_set_reader_key_preset(unsigned preset);
/// 页眉/页脚槽位内容：两条信息条各分左/中/右三格，每格自选一项。
/// Header/footer slot content: each bar splits into left/center/right slots, each picking one item.
typedef enum {
    APP_READER_SLOT_BLANK,         ///< 不显示 / Nothing
    APP_READER_SLOT_TITLE,         ///< 书名 / Book title
    APP_READER_SLOT_CHAPTER,       ///< 章节名 / Chapter name
    APP_READER_SLOT_PAGE,          ///< 本章页码 x/y / Chapter page x/y
    APP_READER_SLOT_PAGE_PROGRESS, ///< 页码与全书进度 / Chapter page plus book progress
    APP_READER_SLOT_PERCENT,       ///< 全书进度百分比 / Whole-book progress percentage
    APP_READER_SLOT_CLOCK,         ///< 时间 HH:MM / Clock
    APP_READER_SLOT_BATTERY,       ///< 电量百分比 / Battery percentage
    APP_READER_SLOT_DATE,          ///< 日期与星期 / Date and weekday
    APP_READER_SLOT_ICON_BATTERY,  ///< 电量图标 / Battery icon
    APP_READER_SLOT_COUNT
} app_reader_slot_item_t;
/// 槽位内容，bar 0=页眉 1=页脚，slot 0/1/2=左/中/右；越界回落「不显示」。
/// Slot item by bar (0 header, 1 footer) and slot (0/1/2 = left/center/right); out-of-range reads as blank.
app_reader_slot_item_t app_settings_reader_chrome_item(unsigned bar, unsigned slot);
/// 返回是否成功保存；失败不改变槽位。/ Return successful persistence, leaving the slot unchanged on failure.
bool app_settings_set_reader_chrome_item(unsigned bar, unsigned slot, app_reader_slot_item_t item);
/// 信息条高度，单位 px；bar 0=页眉、1=页脚，按 8px 调节并保持原默认布局。
/// Bar height in px; bar 0=header, 1=footer, in 8px steps with the original default geometry.
#define APP_READER_HEADER_HEIGHT_DEFAULT 80
#define APP_READER_HEADER_HEIGHT_MIN 64
#define APP_READER_HEADER_HEIGHT_MAX 144
#define APP_READER_FOOTER_HEIGHT_DEFAULT 57
#define APP_READER_FOOTER_HEIGHT_MIN 57
#define APP_READER_FOOTER_HEIGHT_MAX 121
#define APP_READER_CHROME_HEIGHT_STEP 8
uint8_t app_settings_reader_chrome_height(unsigned bar);
/// 同时保存两条高度；无效值或保存失败不改变内存。/ Save both heights; invalid values or failed writes leave memory unchanged.
bool app_settings_set_reader_chrome_heights(uint8_t header, uint8_t footer);
/// 页脚及全屏底部进度条，默认显示；阅读页关闭后回收预留带并重排，保留页脚文字与用户边距。
/// Footer/full-screen progress, shown by default; the reader reclaims its hidden band and reflows while retaining footer text and user margins.
bool app_settings_reader_progress_bar(void);
/// 保存失败返回 false，保持原显示状态。/ Failed persistence returns false and keeps visibility unchanged.
bool app_settings_set_reader_progress_bar(bool on);
/// 六槽一次提交恢复默认布局：页眉 书名/时间/电量图标，页脚 章节名/空/页码·进度。
/// Restore all six slots to the default layout in one commit: header title/clock/battery icon,
/// footer chapter/blank/page·progress.
/// 同时恢复默认高度并显示进度条；保存失败返回 false，内存布局不变。
/// Also restore default heights and show progress; failure preserves the in-memory layout.
bool app_settings_set_reader_chrome_default(void);
/// 旧长按设置API保留用于历史备份兼容。/ Retained legacy hold APIs for historical backup compatibility.
bool app_settings_reader_hold_refresh(void);
void app_settings_set_reader_hold_refresh(bool on);
/// 上1/3与下2/3轻点分区；默认左右各半。/ Upper 1/3 and lower 2/3 tap regions; default horizontal halves.
bool app_settings_reader_vertical_turn(void);
void app_settings_set_reader_vertical_turn(bool on);
/// 书架排序偏好重启后保持。/ Persist the shelf's recent-reading sort across restarts.
bool app_settings_shelf_recent_sort(void);
void app_settings_set_shelf_recent_sort(bool on);
/// 蓝牙翻页器：启用 BLE HID 主机，好让蓝牙翻页器连上并翻页；默认关闭。
/// 关掉会把整个 BLE 栈拆掉，把 NimBLE 占的内存还回去。
/// Bluetooth page-turner: bring up the BLE HID host so a remote can pair and turn pages; off
/// by default. Turning it off tears the whole stack down and returns NimBLE's memory.
bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool on);
/// 前光：开关、亮度 1..100%、色温 0..100%（0 全暖，50 冷暖同开，100 全冷）。
/// 设备本地硬件状态，不随设置备份迁移。
/// Frontlight: on/off, brightness 1..100%, colour temperature 0..100% (0 warm only,
/// 50 both, 100 cool only). Device-local hardware state, excluded from settings backups.
bool app_settings_frontlight_on(void);
void app_settings_set_frontlight_on(bool on);
uint8_t app_settings_frontlight_brightness(void);
void app_settings_set_frontlight_brightness(uint8_t percent);
uint8_t app_settings_frontlight_cct(void);
void app_settings_set_frontlight_cct(uint8_t percent);
/// 字间距默认0px，31档以0居中。/ Default 0px, centered among 31 tracking stops.
#define APP_BOOK_TRACK_MIN (-15)
#define APP_BOOK_TRACK_MAX 15
#define APP_BOOK_TRACK_COUNT (APP_BOOK_TRACK_MAX-APP_BOOK_TRACK_MIN+1)
/// 字间距索引0..30对应-15..+15px，每档1px。/ Tracking indices 0..30 map to -15..+15px, in 1px steps.
uint8_t app_settings_book_tracking(void);
void app_settings_set_book_tracking(uint8_t index);
/// 普通正文首行缩进 0..3 字，默认两字。/ First-line indent: 0..3 em, default 2.
uint8_t app_settings_book_indent(void);
void app_settings_set_book_indent(uint8_t em);
/// 正文首行缩进微调，-100..100像素，默认0；无缩进时不生效。/ First-line adjustment in pixels, -100..100 (default 0); ignored with no indent.
int8_t app_settings_book_indent_adjust(void);
void app_settings_set_book_indent_adjust(int8_t px);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Body reading guides: none, dashed or dotted.
uint8_t app_settings_book_reading_line(void);
void app_settings_set_book_reading_line(uint8_t style);
/// 阅读线相对默认位置 -8..+8 像素，2 像素一步，默认 0。/ Guide offset in 2 px steps.
int8_t app_settings_book_reading_line_offset(void);
void app_settings_set_book_reading_line_offset(int8_t offset_px);
/// 阅读行高百分比，110..150。/ Reader line-height percentage: 110..150.
uint8_t app_settings_book_line_spacing(void);
void app_settings_set_book_line_spacing(uint8_t percent);
/// 阅读页四向边距，0..96 像素；界面按 6 像素一档。默认左/右 24、上/下 0。
/// Reader margins per side: 0..96 px, 6 px per UI step; left/right default 24, top/bottom 0.
uint8_t app_settings_book_margin_left(void);
void app_settings_set_book_margin_left(uint8_t px);
uint8_t app_settings_book_margin_right(void);
void app_settings_set_book_margin_right(uint8_t px);
uint8_t app_settings_book_margin_top(void);
void app_settings_set_book_margin_top(uint8_t px);
uint8_t app_settings_book_margin_bottom(void);
void app_settings_set_book_margin_bottom(uint8_t px);
/// 段距11档，25%居中，0..100%。/ Eleven paragraph-gap stops centered on 25%, spanning 0..100%.
#define APP_BOOK_PARAGRAPH_COUNT 11
uint8_t app_settings_book_paragraph_value(uint8_t level);
uint8_t app_settings_book_paragraph_level(uint8_t percent);
uint8_t app_settings_book_paragraph_spacing(void);
void app_settings_set_book_paragraph_spacing(uint8_t percent);
/// 书内样式默认开启；关闭后正文由阅读设置统一排版并忽略空白行，保留标题和图片。
/// Book styles default on; off uses reader typography and skips blank lines, retaining headings and images.
bool app_settings_book_styles(void);
/// 保存失败返回 false，内存不变。/ Failed persistence returns false without changing memory.
bool app_settings_set_book_styles(bool enabled);
/// 书架样式：1 深色书轨、2 亚克力、5 列表；旧3/4安全回退2。/ Shelf styles: rail (1), acrylic (2), list (5); retired IDs 3/4 fall back to 2.
uint8_t app_settings_shelf_style(void);
void app_settings_set_shelf_style(uint8_t style);
/// 固定 TF 根目录：/sdcard/books 与 /sdcard/fonts；旧配置/备份不能修改。/ Fixed TF roots, unaffected by legacy settings/backups.
const char* app_settings_books_dir(void);
const char* app_settings_fonts_dir(void);

/// 将设置、阅读记录和 WiFi 凭据保存到 TF 卡根目录；备份含明文密码，需妥善保管。
/// Save settings, reading records and WiFi credentials to TF root; keep the plaintext-secret backup private.
esp_err_t app_settings_backup_save(void);
/// 校验备份后恢复 NVS；缺失的外部字体或壁纸回退到内建资源。
/// Validate before restoring NVS; missing external assets fall back safely.
esp_err_t app_settings_backup_restore(void);
