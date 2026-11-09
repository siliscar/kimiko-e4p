/* SPDX-License-Identifier: Apache-2.0
 * 中文：全宽浅顶部层，下圆角与四个无文字圆钮；有前光的板把第四枚圆钮画成太阳
 * 开关。灯灭时层高只有圆钮区；点按开灯后圆钮下方才展开两条横向胶囊滑杆：直接
 * 浮在遮罩上，无卡片、无文字、无旋钮，图标在胶囊内左端，白色填充从左缘生长，
 * 无极滑动；再次熄灯即收起。灯灭时开层后停顿再继续下拉也可临时展开胶囊滑杆，
 * 只属本次开层：收层或再开层即回到只有圆钮区，熄灯点按同样收起；一次连续快滑
 * 只开层不展开。
 * English: A shallow full-width top layer with rounded bottom corners and four
 * unlabeled circles; boards with a frontlight draw a sun switch in the fourth
 * circle. While the light is off the layer keeps only the circle band; switching
 * it on expands two horizontal capsule sliders below the circles: they float on
 * the shade with no card, text or knob, the icon sits inside the left end, a
 * white fill grows from the left edge, and dragging is continuous; switching off
 * collapses them again. With the light off, pausing after the open and then
 * pulling further down expands the capsules for that opening only: closing or
 * reopening returns to the circle band alone, and the light-off tap still
 * collapses them; one continuous fast swipe only opens, never expands.
 * 用户修订：锁屏图标移出快捷栏；常驻大滑杆先改为随开关展开的卡片行，再按用户
 * 参考图去掉卡片容器，改成控制中心式横向胶囊；亮度用半填充圆图标，色温用温度
 * 计图标，滑杆只在开灯时可触。
 * User revision: drop the lock icon; the always-present sliders first became a
 * card of rows, then per the user's reference the card was dropped for control
 * centre style horizontal capsules; brightness uses the half-filled circle and
 * colour temperature the thermometer, and the sliders respond only while the
 * light is on.
 * 用户新增：WiFi 与蓝牙圆键按住 0.5 秒直接进入各自的设置页；点按仍只切换开关。
 * User addition: holding the WiFi or Bluetooth circle for 0.5 s opens its settings
 * page; a tap still only toggles the switch.
 * 冻结：不访问硬件、设置或刷新，不申请帧缓存；只在展开/状态变化后由主循环调用绘图。
 * Frozen: No hardware, settings, presentation or frame allocations; the loop draws only on opening or state changes.
 */
#pragma once
#include "app.h"
#include "epdiy.h"

#define UI_QUICK_EDGE_PX 32
#define UI_QUICK_SWIPE_PX 72
// 同一次下拉的「继续」判定：开层后停顿至少这么久，再往下拉过开层行程才算继续
// 下拉；用户实测单次自然下滑总行程常超 150px，按总行程判定会让开层与展开在一
// 次滑动里连触发（2026-10-25 用户反馈）。
// The "continue" test for the same pull: after opening, the finger must pause at
// least this long and then pull the opening distance again; a user's single
// natural swipe routinely spans 150px+, so a total-travel test chains opening and
// expanding inside one flick (user report 2026-10-25).
#define UI_QUICK_PULL_SETTLE_MS 300
// WiFi / 蓝牙圆键按住这么久直接进对应设置页；阈值到了就关层，抬手不再算点按。
// Holding the WiFi / Bluetooth circle this long opens its settings page; the
// layer closes at the threshold and the release no longer counts as a tap.
#define UI_QUICK_HOLD_SETTINGS_MS 500
#define UI_QUICK_HEIGHT 264
#define UI_QUICK_FRONTLIGHT_HEIGHT 480
#define UI_QUICK_CIRCLE_Y 156
#define UI_QUICK_RADIUS 54
#define UI_QUICK_HIT_RADIUS 66

// 前光胶囊：开灯时圆钮下方展开两条横向胶囊滑杆，直接浮在遮罩上，无卡片、无
// 文字、无旋钮；胶囊内活动灰为空档，白色填充从左缘生长，图标放在胶囊内左端；
// 再次熄灯即收起。
// Frontlight capsules: switching the light on expands two horizontal capsule
// sliders below the circles, floating on the shade with no card, text or knob;
// the capsule interior is the active gray when empty, a white fill grows from
// the left edge, and the icon sits inside the left end; switching off collapses
// them again.
#define UI_QUICK_FL_ICON_CX 96
#define UI_QUICK_FL_ICON_SIZE 44
#define UI_QUICK_FL_BRI_CY 284
#define UI_QUICK_FL_CCT_CY 384
#define UI_QUICK_FL_TRACK_X 48
#define UI_QUICK_FL_TRACK_W 588
#define UI_QUICK_FL_TRACK_H 64
// 行局刷矩形：覆盖整条胶囊并留出手指余量。/ Local row repaint: the whole capsule plus finger margin.
#define UI_QUICK_FL_ROW_X 40
#define UI_QUICK_FL_ROW_W 604
#define UI_QUICK_FL_ROW_H 80
// 命中区覆盖整行：不要求压到胶囊本身。
// Hit zones cover the whole row, so a finger never has to land on the capsule.
#define UI_QUICK_FL_BRI_HIT_Y0 228
#define UI_QUICK_FL_BRI_HIT_Y1 333
#define UI_QUICK_FL_CCT_HIT_Y0 334
#define UI_QUICK_FL_CCT_HIT_Y1 440

typedef enum {
    UI_QUICK_NONE, UI_QUICK_OPEN, UI_QUICK_CLOSE, UI_QUICK_EXPAND,
    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
    UI_QUICK_WIFI_SETTINGS, UI_QUICK_BLUETOOTH_SETTINGS,
} ui_quick_action_t;

/// 前光控件；快捷层只产出事件，硬件和设置由主循环执行。
/// Frontlight controls; the layer only emits events and the loop owns hardware and settings.
typedef enum {
    UI_QUICK_CTL_NONE = 0,
    UI_QUICK_CTL_ON,
    UI_QUICK_CTL_BRIGHTNESS,
    UI_QUICK_CTL_CCT,
} ui_quick_control_t;

typedef struct {
    bool open, tracking, swallow, moved;
    uint16_t x0, y0;
    int hit;
    int64_t started_ms;
    /// 本板是否有前光；没有时第四枚圆钮画锁屏，不画卡片且层高仍是 UI_QUICK_HEIGHT。
    /// Whether this board has a frontlight; without one the fourth circle keeps the lock, the card is omitted and the layer stays UI_QUICK_HEIGHT.
    bool frontlight;
    bool fl_on;
    /// 灯灭时继续下拉产生的本次开层临时展开；开层、收层与熄灯点按都会清零，
    /// 不跨层记忆，也不落盘。
    /// Session-only expansion from pulling the open layer further down while the
    /// light is off; opening, closing and the light-off tap all clear it. It is
    /// never remembered across openings or persisted.
    bool fl_expanded;
    /// 接触态：本次下拉刚把层拉开且灯灭，手指未抬；停顿一小会儿后再继续往下拉
    /// 过一程才临时展开——一次连续快滑的总行程不算「继续下拉」，开层与展开不会
    /// 在一次滑动里连触发。
    /// Contact state: this very pull just opened the layer with the light off;
    /// only a pause and then a further pull expands the capsules. A continuous
    /// fast swipe's total travel never counts, so opening and expanding cannot
    /// chain inside one flick.
    bool fl_pull;
    uint16_t fl_pull_x;    ///< 停顿锚点 x。/ x of the pause anchor.
    uint16_t fl_pull_y;    ///< 停顿判定的移动锚点 y。/ y anchor for the stillness test.
    int64_t fl_pull_ms;    ///< 最近一次明显移动的时间。/ time of the last material movement.
    bool fl_pull_settled;  ///< 已停顿，继续下拉可展开。/ paused; a further pull may expand.
    uint8_t fl_bri;
    uint8_t fl_cct;
    /// 每条滑杆自上次定稿以来达到的最大档位：定稿第一段据此把弄脏范围洗白。
    /// 由输入层在拖动中只增不减，松手两段定稿后收回当前档位。
    /// Per slider, the highest level reached since the last settle: stage one
    /// whitens this extent. The input layer only grows it during drags and the
    /// two-stage settle shrinks it back to the current level on release.
    uint8_t fl_bri_dirty;
    uint8_t fl_cct_dirty;
    ui_quick_control_t drag;
} ui_quick_menu_t;

typedef struct {
    ui_quick_action_t action;
    ui_quick_control_t control;
    uint8_t value;
    /// 松手或点选时置位，拖动过程中保持 false；主循环只在这里写 NVS。
    /// Set on release or tap and left false while dragging; only then does the loop write NVS.
    bool commit;
} ui_quick_event_t;

/// 清除层和手势；保留本板前光能力与预览值。/ Clear layer and contact state while keeping frontlight capability and preview values.
void ui_quick_menu_reset(ui_quick_menu_t *menu);
/// 设置本板前光能力并同步预览值；层高随开灯状态伸缩。
/// Set frontlight capability and sync the preview values; the layer height follows the on state.
void ui_quick_menu_set_frontlight(ui_quick_menu_t *menu, bool available, bool on,
                                  uint8_t brightness, uint8_t cct);
/// 返回true表示整条触摸序列被接管；多指/读错不执行按钮与滑杆，收起之后仍吞到抬手。
/// True owns the whole contact stream; multitouch/read errors never activate controls, and dismissal still swallows until release.
bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                        ui_quick_event_t *event);
/// 取消接触但保留已打开的层；正在拖动的滑杆保留当前值，由主循环决定是否落盘。
/// Cancel a contact while retaining an already open layer; a dragged slider keeps its value and the loop decides persistence.
void ui_quick_menu_cancel_input(ui_quick_menu_t *menu);
/// 只覆盖顶部，保留下层其他像素；开灯时连同卡片一起绘制，熄灯只有圆钮区。
/// Cover the top only, leaving other pixels intact; draw the card while the light is on, otherwise only the circle band.
void ui_quick_menu_draw(uint8_t *fb, const ui_quick_menu_t *menu, bool wifi, bool bluetooth);
/// 当前按住的圆键，取消/滑出/释放或按住卡片时为-1。/ The held key, or -1 after cancellation, movement, release or a card press.
int ui_quick_menu_pressed(const ui_quick_menu_t *menu);
/// 重画实心圆键并叠加状态点/圆内按压，不分配、不模糊、不排字。第四枚圆钮按
/// `menu->frontlight` 画太阳或锁；前光板开灯为活动灰底黑太阳，熄灭为浅底黑太阳。
/// Repaint a solid key with its state badge and interior press, without allocating,
/// blurring or shaping text. The fourth circle shows a sun on frontlight boards
/// (or the lock elsewhere); the light-on face is the active gray under a black sun.
bool ui_quick_menu_draw_button(uint8_t *fb, const ui_quick_menu_t *menu, int button,
                               bool active, bool pressed, EpdRect *dirty);
/// 只重画胶囊行并返回受影响的矩形，供滑杆拖动局刷；熄灯时行不存在。
/// Repaint one capsule row and report the affected rectangle for local slider refreshes; rows do not exist while the light is off.
bool ui_quick_menu_draw_control(uint8_t *fb, const ui_quick_menu_t *menu,
                                ui_quick_control_t control, EpdRect *dirty);
/// 定稿第一段：把该行画成「白填充到弄脏上沿」的中间态并返回受影响的矩形，
/// 供主循环先洗白再落灰；熄灯时行不存在。
/// Settle stage one: paint the row with the white fill grown to the dirty
/// extent and report the rectangle, so the loop whitens before landing gray;
/// rows do not exist while the light is off.
bool ui_quick_menu_draw_control_white(uint8_t *fb, const ui_quick_menu_t *menu,
                                      ui_quick_control_t control, EpdRect *dirty);
/// 只重画清晰状态栏，供网络状态变化时局刷。/ Repaint only the sharp status strip for network changes.
void ui_quick_menu_draw_status(uint8_t *fb);
/// 当前层的整块区域；高度随开灯伸缩，收起前用旧高度并集擦除卡片。
/// The whole layer; the height follows the light state, and close uses the union with the previous height to erase the card.
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu);
/// 兼容主循环的关闭出口；实心控件没有缓存需要释放。/ Close hook for the loop; solid controls hold no cache.
void ui_quick_menu_end(void);
