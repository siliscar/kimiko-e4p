/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度换算和晃动判定，不访问硬件。/ Reading position and shake policy without hardware access.
 * 用户修订（2026-11-01）：改为连续甩动检测。慢基线做高通，高通 x 上穿 +阈值翻下一页、
 * 下穿 -阈值翻上一页；y 轴高通须小于 x 减裕量，排除纵向抖动；不应期吸收回摆半程；
 * 横轴变化速率过滤缓慢倾斜；采样断档或调用方压制（触摸）后重新锚定基线，不继承半个动作。
 * 幅度阈值按竖持校准，见常量。
 * User revision (2026-11-01): continuous-shake detection. A slow per-axis baseline high-passes
 * the sample; the high-passed x crossing +threshold turns forward and -threshold turns back.
 * The high-passed y must stay below x minus a margin to reject vertical jiggle, a refractory
 * window swallows the rebound half-swing, a horizontal slew gate rejects slow tilts, and a sampling gap or caller suppression (touch)
 * re-anchors the baseline so no half gesture carries over. The threshold is calibrated for a
 * vertical hold; see the constants.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// UTF8章内偏移映射回文件进度，避免GBK字节单位混用。/ Map UTF8 chapter offsets to source bytes, including GBK.
static inline uint32_t book_position_bytes(uint32_t start, uint32_t end, size_t off, size_t len) {
    if (end < start || !len) return start;
    if (off > len) off = len;
    return start + (uint64_t)(end - start) * off / len;
}

enum {
    /// 基线每帧 1/32 递推，50ms 采样下时间常数约 1.6s：慢倾斜跟得上，甩动半程拖不走。
    /// / Baseline EMA 1/32 per feed, ~1.6s at 50ms: tracks slow tilts, survives a swing.
    BOOK_SHAKE_BASE_SHIFT = 5,
    /// 高通 x 触发阈值（mg），2026-11-01 真机校准：竖持时甩动弧线把部分平动泄入被忽略的
    /// z 轴，同力度幅度约为平持一半（竖持典型 250-350、平持 500-900），按竖持校准。
    /// / High-pass x trigger (mg), device-calibrated 2026-11-01: in a vertical hold the swing
    /// arc leaks part of the translation into the ignored z axis and the same effort reads
    /// about half of a flat hold (vertical 250-350 vs flat 500-900), so the threshold is
    /// calibrated against the vertical hold.
    BOOK_SHAKE_THRESHOLD_MG = 250,
    /// 横轴变化速率下限，按真实采样间隔归一，避免重力基线滞后被当作甩动。
    /// Minimum horizontal slew in mg/s, normalized by sample time to reject gravity-baseline lag.
    BOOK_SHAKE_MIN_SLEW_MG_S = 700,
    /// x 高通须高出 y 高通的裕量（mg），过滤纵向抖动。/ x must beat high-passed y by this much (mg).
    BOOK_SHAKE_Y_MARGIN_MG = 50,
    /// 触发后不应期（ms），覆盖回摆半程。/ Refractory after a turn (ms), covers the rebound.
    BOOK_SHAKE_REFRACTORY_MS = 800,
    /// 重新锚定后需静置的采样时长（ms），触摸与断档后半动作不得直接触发。
    /// / Settled sampling required after a re-anchor (ms); no half gesture fires straight away.
    BOOK_SHAKE_SETTLE_MS = 300,
    /// 采样断档重新锚定（ms）。/ Re-anchor after a sampling gap (ms).
    BOOK_SHAKE_GAP_MS = 250,
};

typedef struct {
    bool ready;
    int baseline_x, baseline_y; ///< 慢速跟踪的重力基准，作高通滤波 / Slow gravity baseline for the high pass
    int previous_hpx, previous_x; ///< 上一帧高通与原始 x / Previous high-passed and raw x
    int64_t last_ms;            ///< 上一帧时间，断档重新锚定 / Last feed time; a gap re-anchors
    int64_t anchor_ms;          ///< 最近一次锚定时刻，静置期从这起算 / Last re-anchor time; the settle window starts here
    int64_t cooldown_ms;        ///< 不应期截止 / Refractory deadline
} book_shake_gate_t;

static inline int book_shake_abs(int value) { return value < 0 ? -value : value; }

/// 输入为屏幕坐标的毫克加速度，采样周期应不小于传感器出数周期；连续甩动中每一下各自触发。
/// Input is screen-axis acceleration in mg; the feed period must not be shorter than the
/// sensor's output period, and each swing of a continuous shake triggers on its own.
static inline int book_shake_feed(book_shake_gate_t* g, int x, int y, int z,
                                  bool suppressed, int64_t now) {
    (void)z;
    if (!g) return 0;
    if (!g->ready || suppressed || now < g->last_ms || now - g->last_ms > BOOK_SHAKE_GAP_MS) {
        g->ready = true;
        g->baseline_x = x;
        g->baseline_y = y;
        g->previous_hpx = 0;
        g->previous_x = x;
        g->last_ms = g->anchor_ms = now;
        return 0;
    }
    const int64_t elapsed = now - g->last_ms;
    if (!elapsed) return 0;
    const int slew = book_shake_abs(x - g->previous_x);
    g->previous_x = x;
    g->last_ms = now;
    // 基线全时段慢速跟踪：连续甩动的对称摆动只轻微摆动基准，不会被拖走。
    // The baseline tracks all the time: a continuous shake's symmetric swings only wobble it
    // instead of dragging it away.
    g->baseline_x += (x - g->baseline_x) >> BOOK_SHAKE_BASE_SHIFT;
    g->baseline_y += (y - g->baseline_y) >> BOOK_SHAKE_BASE_SHIFT;
    const int hpx = x - g->baseline_x;
    const int hpy = y - g->baseline_y;
    int dir = 0;
    // 上穿正阈值=向右甩（下一页），下穿负阈值=向左甩（上一页）；静置期、y 优势与不应期在此把关。
    // Crossing up turns forward, crossing down turns back; the settle window, y dominance and
    // the refractory window do the gating here.
    if (now >= g->cooldown_ms && now - g->anchor_ms >= BOOK_SHAKE_SETTLE_MS &&
        book_shake_abs(hpx) >= BOOK_SHAKE_THRESHOLD_MG &&
        (int64_t)slew * 1000 >= BOOK_SHAKE_MIN_SLEW_MG_S * elapsed &&
        book_shake_abs(hpx) > book_shake_abs(hpy) + BOOK_SHAKE_Y_MARGIN_MG) {
        if (g->previous_hpx < BOOK_SHAKE_THRESHOLD_MG && hpx >= BOOK_SHAKE_THRESHOLD_MG) dir = 1;
        else if (g->previous_hpx > -BOOK_SHAKE_THRESHOLD_MG && hpx <= -BOOK_SHAKE_THRESHOLD_MG) dir = -1;
    }
    g->previous_hpx = hpx;
    if (dir) g->cooldown_ms = now + BOOK_SHAKE_REFRACTORY_MS;
    return dir;
}
