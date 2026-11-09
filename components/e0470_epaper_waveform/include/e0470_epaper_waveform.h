/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * E0470A01（684×1216，40pin）的 epdiy 波形。
 * 自行调整屏幕波形会使设备失去保修。
 *   E0470_WAVEFORM         默认：裁剪 GC16 36 相 / GL16 37 相 + 50/50 阈值 DU
 *   E0470_FULL_WAVEFORM    GC16 / GL16 各 48 相，一个温度档 0-50°C
 *   E0470_GRAY8_WAVEFORM   8 灰阶：GC16 / GL16 各 30 相，灰阶少一半换来整屏约 360ms
 *   E0470_FOLLOW_WAVEFORM  跟随 DU：8 帧（黑 7 / 白 8），用于触摸笔迹
 * 默认与 8 灰阶波形挂阈值 DU（目标 0-7→黑、8-15→白）；波形保留只驱动 0/15 的源表。
 * GL16 给 15→15 补 1 帧白推；刷新走全像素，否则差分跳过白底。
 * 开机先调一次 e0470_waveform_init()。
 *
 * epdiy waveforms for the E0470A01 (684×1216, 40-pin).
 * Changing panel waveforms voids the warranty.
 *   E0470_WAVEFORM         Default: trimmed GC16 36 / GL16 37 + 50/50 threshold DU
 *   E0470_FULL_WAVEFORM    GC16 / GL16 48 phases each, one 0–50°C temp range
 *   E0470_GRAY8_WAVEFORM   8-gray: GC16 / GL16 30 phases; half the steps, ~360 ms full
 *   E0470_FOLLOW_WAVEFORM  Follow DU: 8 frames (black 7 / white 8) for touch ink
 * Default and 8-gray hang a threshold DU (dest 0–7→black, 8–15→white); the
 * source tables still drive only 0/15. GL16 adds one white push on 15→15;
 * refresh is full-pixel or the diff skips already-white.
 * Call e0470_waveform_init() once at boot.
 */

#pragma once

#include <stdint.h>

#include "epd_waveform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 完整灰阶表：GC16 / GL16 各 48 相，白推动梯子 17 档、去重后 11 级可分辨灰阶。
/// DU 是 20 相，只驱动目标 0/15；E0470_FULL_WAVEFORM 原样保留对照。
/// / Full gray tables: GC16 / GL16 48 phases, 17-step white-push ladder,
/// 11 distinguishable after collapse. DU is 20 phases, dest 0/15 only;
/// E0470_FULL_WAVEFORM is kept as a reference.
#define E0470_FULL_DU_FRAMES 20
#define E0470_FULL_GC16_FRAMES 48
#define E0470_FULL_GL16_FRAMES 48
extern const EpdWaveform E0470_FULL_WAVEFORM;

/// 另一份表：只有 8 级可分辨灰阶（净推动梯子 8 档），30 相，整屏约 360ms（默认表约 430ms）。
/// 灰阶档数 = 白推动梯子的档数，在这块屏上 16 级与 30 相不可兼得，所以它是备选不是默认。
/// / Alternate table: 8 distinguishable grays (8-step net push), 30 phases,
/// ~360 ms full (default ~430 ms). Gray count is the white-push ladder; 16
/// levels and 30 phases cannot both fit this panel, so this is optional.
#define E0470_GRAY8_GC16_FRAMES 30
#define E0470_GRAY8_GL16_FRAMES 30
extern const EpdWaveform E0470_GRAY8_WAVEFORM;

/// 裁剪版保留新屏所需的 3 个尾部保持相。完整48相实测未明显改善边缘残影，因此恢复
/// 已验证的主动段裁剪，避免无收益地增加约150ms；无损前导保持跳过仍由渲染器执行。
/// / Trim keeps the 3 trailing hold phases the new panel needs. The full 48
/// did not clearly help edge ghosting, so the proven active-segment trim is
/// restored instead of adding ~150 ms for no gain. Lossless leading-hold
/// skip is still done by the renderer.
#define E0470_TRIM_ERASE_MAX 11
#define E0470_TRIM_SAT_CUT 5
#define E0470_TRIM_WHITE_SAT_CUT 0
#define E0470_TRIM_HOLD 3
#define E0470_GC16_FRAMES 36
#define E0470_GL16_FRAMES 37

/// to=0 补回的原厂连续推黑次数；与 DU / 原厂 GL16 的黑饱和段一致，实机验证的字黑下限。
/// / Vendor black pushes restored for to=0; matches the DU / vendor GL16 black
/// saturation run, the panel-verified floor for solid text black.
#define E0470_BOOST_BLACK_PUSHES 18

/// 帧周期取算法文档第 8 节的设计点 ν≈90Hz。
/// / Frame period is the §8 design point ν≈90 Hz.
#define E0470_WAVEFORM_FRAME_US 11090
extern const EpdWaveform E0470_WAVEFORM;

/// CrossMux/FreeInk 普通文字翻页 GL16；仅配合真实 4bpp 历史帧差分使用，定期 GC16 保留。
/// CrossMux/FreeInk text-turn GL16; use with actual 4bpp history differentials and retain periodic GC16.
extern const EpdWaveform E0470_TEXTTURN_WAVEFORM;

/// 触摸笔迹跟手用的 8 帧短 DU，走 FAST 扫描（帧周期约 7ms）。
/// 推动次数按 |to-from| 比例分配，满幅迁移往黑推 7 相、往白推 8 相。
/// / 8-frame short DU for touch ink, FAST scan (~7 ms/frame). Push count
/// scales with |to-from|; full travel is 7 black / 8 white.
#define E0470_FOLLOW_BLACK_FRAMES 7
#define E0470_FOLLOW_WHITE_FRAMES 8
#define E0470_FOLLOW_FRAMES 8
extern const EpdWaveform E0470_FOLLOW_WAVEFORM;

/// 快捷栏胶囊滑杆专用表：胶囊内部只有底灰 0x30（level 3，取首页最近阅读进度
/// 条已读段同色，拖动近似落点与之相近、残影随底色隐没）与白 15 互转，其余
/// (from,to) 全部保持不驱动。拖动表 8 帧走 FAST 扫描保跟手，近似即可；松手
/// 提交也按拖动表补推终态（用户 2026-10-25 决定取消两段定稿：左滑暴露带终态
/// 为灰，洗白段白闪约 200ms）。白表/灰表保留备用——白表按原厂 DU 同款擦白
/// CAPSULE_WHITE_FRAMES 帧，灰表单调压灰 CAPSULE_GRAY_K 相，从饱和白按
/// 「18 推黑 = 全黑」的线性梯子落底灰（GL16 的 (9,15) 行是 11 推深压 + 回拉
/// 尾的阻尼落法，单行播放会黑闪）。阈值 DU 把 to≥8 当纯白、to<8 走全黑路径，
/// 绝不能用于胶囊。
/// / Dedicated tables for the quick-layer capsule sliders: the capsule interior
/// only round-trips body gray 0x30 (level 3, matching the dashboard
/// reading-progress read fill so the drag's approximate landing blends into the
/// body) and white 15; every other (from,to) holds. The drag table is 8 frames
/// on FAST scan for approximate tracking; the release commit also replays the
/// drag table for the final row (user decision 2026-10-25 removed the two-stage
/// settle: the left-swipe band ends gray, so the wash flashed white for
/// ~200 ms). The white/gray tables stay for a restored settle — the white table
/// erases for CAPSULE_WHITE_FRAMES pushes (the vendor DU's proven ladder), the
/// gray table darkens monotonically for CAPSULE_GRAY_K phases to land the body
/// gray from saturated white on the vendor's 18-push-to-black ladder (GL16's
/// (9,15) row is the damped 11-darken plus braking tail, which flashes black
/// when played alone). The threshold DU drives to>=8 as pure white and to<8 as
/// full black, and must never drive the capsules.
#define E0470_CAPSULE_DRAG_FRAMES 8
#define E0470_CAPSULE_DRAG_K 4
#define E0470_CAPSULE_DRAG_W 8
#define E0470_CAPSULE_WHITE_FRAMES 18
#define E0470_CAPSULE_GRAY_K 1
/// 胶囊底灰级数：与首页最近阅读进度条已读段的 0x30 同级，所有胶囊表单元引用
/// 本定义，换色只需改这里并重标定 DRAG_K / DRAG_W。
/// / Capsule body-gray level: same 0x30 as the dashboard reading-progress read
/// fill; every capsule table cell references this define, so a recolor only
/// changes this plus a DRAG_K / DRAG_W recalibration.
#define E0470_CAPSULE_BODY_LEVEL 3
extern const EpdWaveform E0470_CAPSULE_DRAG_WAVEFORM;
extern const EpdWaveform E0470_CAPSULE_WHITE_WAVEFORM;
extern const EpdWaveform E0470_CAPSULE_GRAY_WAVEFORM;

/// 单相位执行表供水波纹的错相 GL16 扫描使用；像素动作来自逐带相位 LUT。
/// One-phase apply waveform for staggered GL16; per-band phase LUTs supply pixel actions.
extern const EpdWaveform E0470_APPLY_WAVEFORM;

/// 按上面的公式生成 frames 帧的跟随表写进 dst（容量 frames×16×4 字节）。
/// / Build a follow table of `frames` into dst (frames×16×4 bytes).
void e0470_follow_lut_build(int frames, uint8_t (*dst)[16][4]);

/// 在一条 EpdWaveform 里按模式（MODE_GC16 / MODE_GL16 / MODE_DU）找唯一温度档的相位表。
/// / Look up the single temp-range phase table for MODE_GC16 / MODE_GL16 / MODE_DU.
const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode);

/// 查 (from→to) 在第 phase 相的 2bit 动作：0 保持、1 压黑、2 擦白。
/// / 2-bit action for (from→to) at `phase`: 0 hold, 1 darken, 2 erase.
int e0470_phase_action(const EpdWaveformPhases* phases, int phase, int to, int from);

/// 把 to=0 各行被裁掉的推黑补回 E0470_BOOST_BLACK_PUSHES 次连续相位，占用该行前导保持相，
/// 行尾保留 E0470_TRIM_HOLD 个保持相；只改写「先擦白后推黑」的整齐行，其他形状原样。
/// data 是 phases 相的运行表（如 E0470_GL16_FRAMES 布局）。
/// / Restore each to=0 row's trim-cut black pushes to E0470_BOOST_BLACK_PUSHES consecutive
/// phases by consuming that row's leading holds, keeping E0470_TRIM_HOLD trailing holds;
/// only tidy [erase then black] rows change. data holds `phases` phases (GL16 layout).
void e0470_boost_black_column(uint8_t (*data)[16][4], int phases);

/// 生成 E0470_WAVEFORM 与 E0470_FOLLOW_WAVEFORM 的表。必须在第一次刷新之前调用一次。
/// / Build E0470_WAVEFORM and E0470_FOLLOW_WAVEFORM. Call once before the first refresh.
void e0470_waveform_init(void);

#ifdef __cplusplus
}
#endif
