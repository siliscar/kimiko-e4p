/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：遮罩与面板收起后的归一表。被白底或半透明遮罩盖过的字已经多走过一轮
 * 「擦白 → 推黑」，比没被盖过的字更饱和；单次还原无法追平，只能让整页字芯
 * 再统一走一遍同样的序列。
 *
 * English: Normalization table for closing a panel or the quick layer. Text the white sheet
 * or the shade covered has already run one extra erase-then-black cycle and is more
 * saturated than the rest; a single restore cannot match it, so every glyph core on the page
 * runs the same cycle once more.
 *
 * 冻结：只改写 to=0 列与非黑相等对；白列与相数沿用那张与背景设置无关的阅读整页表，
 * 基底不随阅读背景切换。哪个页面用哪张由调用方决定，本模块不读全局设置。
 * Frozen: rewrite only the to=0 column and, for hold_gray, the non-black equal pairs; keep
 * the background-independent reader page table's white column and phase count. The caller
 * picks the variant; this module never reads the global setting.
 */
#pragma once
#include "epdiy.h"
#include <stdbool.h>

/// 归一表：任意起点的字芯都是「擦白 18 相 + 推黑 18 相」，其余目标同基底整页表。
/// hold_gray 为真时返回把非黑相等对清成保持的变体，整屏 pass 只重驱字芯——只有画面上
/// 有阅读背景等图片灰阶时才该这样做；其余页面必须走完整归一。
/// / Normalize table: every glyph core runs 18 erase phases then 18 black phases from any
/// start; all other targets match the base page table. With hold_gray the variant holds
/// non-black equal pairs so the pass re-drives only glyph cores; use it only when the frame
/// carries picture grays such as a reading background. Every other page needs the full pass.
const EpdWaveform* display_normalize_waveform(bool hold_gray);
