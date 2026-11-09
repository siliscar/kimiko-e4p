/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"

/// 普通纯文字翻页：白底到中灰的少量回浅脉冲提前，to=0 补回原厂 18 次推黑。
/// Plain text turns: advance a few gray-edge settling pulses and restore the vendor's
/// 18 black pushes for to=0.
const EpdWaveform* display_reader_text_waveform(void);

/// 阅读页整页输出：原厂 GL16 序列，纯白目标沿用主页水波纹的完整擦白列，
/// to=0 同样补回 18 次推黑。
/// Full reader pages: vendor GL16 sequences with the main-page water whitening for
/// pure-white targets and the same 18 black pushes for to=0.
const EpdWaveform* display_reader_page_waveform(void);

/// 与背景设置无关的同一张整页表：归一表的基底，整屏归一后的字芯墨度不随背景开关变化。
/// The same page table regardless of the background setting: the normalize table builds
/// on it, so glyph ink after a whole-screen pass does not follow the background switch.
const EpdWaveform* display_reader_ink_waveform(void);

/// 水波纹翻页：原厂 GL16 表加 to=0 的 18 次推黑，保留每种迁移的前导擦除与白列；
/// 37 相不变，错相 8 带的拍数与节拍也不变。设置阅读背景时自动换用背景派生表。
/// Water turns: the vendor GL16 table plus 18 black pushes for to=0, keeping each
/// transition's leading erase and the white column; the 37 phases and the staggered
/// eight-band tick count are unchanged. Switches to the background derivative while a
/// reading background is set.
const EpdWaveform* display_reader_water_waveform(void);

/// 无背景设置分支的水波纹基底，供统一目录显式选择。/ Background-independent water base for explicit catalog selection.
const EpdWaveform* display_reader_water_base_waveform(void);

/// 背景页派生表：在水波纹表上再裁擦除与白/黑饱和，to=0 增黑保留；相数更少，
/// 深色图形上的黑闪更短。白列不跟随裁剪，改用主页水波纹已验证的 24 拍，旧字转白才擦得净。
/// page 与 water accessor 在背景开启时都会返回这张表。
/// Background derivative: the water table trimmed once more on erase and both saturation
/// runs while keeping the to=0 boost; fewer phases and shorter black flashing over dark
/// graphics. The white column skips the trim and keeps the main water table's verified 24
/// pushes, so erased text reaches pure white. Both page and water accessors return it while
/// a background is set.
const EpdWaveform* display_reader_bg_waveform(void);

/// 插图页派生表：与背景表同源，但深色对保留前导擦白（旧字落到图片灰也从白轨落位），
/// 相等对仍置保持（脏区膨胀带上的未变化像素不再被黑白往返推出一圈细线）。
/// Illustration derivative: same base as the background table, but dark pairs keep their
/// leading erase so old glyphs settle on an image gray from the white rail. Equal pairs still
/// hold, so the unchanged pixels that dilation drags along no longer take a white-black trip
/// that draws a thin outline.
const EpdWaveform* display_reader_image_waveform(void);
