/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * E0470A01 波形装配：裁剪默认表、8 灰阶、跟随 DU、快捷栏胶囊三表。
 * 自行调整屏幕波形会使设备失去保修。
 *
 * E0470A01 waveform assembly: trimmed default, 8-gray, follow DU, and the
 * three quick-layer capsule tables.
 * Changing panel waveforms voids the warranty.
 */

#include "e0470_epaper_waveform.h"

#include <assert.h>
#include <string.h>

#include "e0470_waveform_trim.h"
#include "du.h"
#include "gc16.h"
#include "gl16.h"
#include "gray8_gc16.h"
#include "gray8_gl16.h"

// 温度档 0-50°C。在此固件中没有温度分档的演示。
// / One 0–50°C temp range. This firmware has no multi-range demo.
static const EpdWaveformTempInterval e0470_intervals[] = {
    { .min = 0, .max = 50 },
};

// 把 (from, to) 的一个 2bit 动作写进 epdiy 的表：data[frame][to][from/4]，高位是 from0。
// / Write one 2-bit (from, to) action into the epdiy table: data[frame][to][from/4], MSB is from0.
static inline void lut_or(uint8_t (*data)[16][4], int f, int to, int from, int action) {
    data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
}

static inline int lut_get(const uint8_t (*data)[16][4], int f, int to, int from) {
    return (data[f][to][from / 4] >> (6 - 2 * (from % 4))) & 3;
}

// 把一个 2bit 动作原位写回（覆盖，不同于 lut_or 的叠加）。
// / Overwrite one 2-bit action in place (unlike lut_or, which ORs).
static inline void lut_set(uint8_t (*data)[16][4], int f, int to, int from, int action) {
    const int shift = 6 - 2 * (from % 4);
    data[f][to][from / 4] = (uint8_t)((data[f][to][from / 4] & ~(3 << shift)) | (action << shift));
}

void e0470_follow_lut_build(int frames, uint8_t (*dst)[16][4]) {
    memset(dst, 0, (size_t)frames * 16 * 4);
    // 两个方向各有自己的满推次数；短表（连续 DU 单帧、连调 dufr n）按表长封顶。
    // / Each direction has its own full-push count; short tables (1-frame
    // continuous DU, live-tune dufr n) cap at the table length.
    const int black_max = frames < E0470_FOLLOW_BLACK_FRAMES ? frames : E0470_FOLLOW_BLACK_FRAMES;
    const int white_max = frames < E0470_FOLLOW_WHITE_FRAMES ? frames : E0470_FOLLOW_WHITE_FRAMES;
    for (int to = 0; to < 16; to++) {
        for (int from = 0; from < 16; from++) {
            if (to == from) continue;
            const int diff = to > from ? to - from : from - to;
            const int action = to > from ? 2 : 1;  // 往白推 0b10，往黑推 0b01 / 0b10 erase, 0b01 darken
            const int budget = action == 2 ? white_max : black_max;
            // 推动次数 = ceil(diff · 满推 / 15)，至少 1：差得远多推，差得近少推。
            // / Push count = ceil(diff · full / 15), at least 1: far travels more, near travels less.
            const int pushes = (diff * budget + 14) / 15;
            for (int f = 0; f < pushes; f++) lut_or(dst, f, to, from, action);
        }
    }
}

/* ---- 跟随 DU：开机按公式生成 / Follow DU: built at boot ---- */
static uint8_t e0470_follow_data[E0470_FOLLOW_FRAMES][16][4];
static const EpdWaveformPhases e0470_follow_phases = {
    .phases = E0470_FOLLOW_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_follow_data[0],
};
static const EpdWaveformPhases* e0470_follow_ranges[] = { &e0470_follow_phases };
static const EpdWaveformMode e0470_follow_mode = {
    .type = 1,  // MODE_DU / MODE_DU
    .temp_ranges = 1,
    .range_data = &e0470_follow_ranges[0],
};
static const EpdWaveformMode* e0470_follow_modes[] = { &e0470_follow_mode };

const EpdWaveform E0470_FOLLOW_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_follow_modes,
    .temp_intervals = e0470_intervals,
};

/* ---- 快捷栏胶囊滑杆表 / Quick-layer capsule sliders ---- */
// 胶囊内部只有底灰 3（0x30，与首页最近阅读进度条已读段同色）与白 15 互转：
// 拖动表给近似短序列（FAST 扫描保跟手），松手提交同样按拖动表补推终态
// （2026-10-25 取消两段定稿，见头文件）；白表/灰表保留备用——灰表单行落灰，
// 因为裁剪 GC16 的 (9,15) 行被 sat_cut 砍掉 5 相压灰落不准，GL16 的 (9,15)
// 行是 11 推深压 + 回拉尾，单行播放会黑闪。
// The capsule interior only round-trips body gray 3 (0x30, the dashboard
// reading-progress read fill) and white 15: the drag table holds short
// approximate runs (FAST scan for tracking), and the release commit also
// replays the drag table for the final row (the two-stage settle was removed
// 2026-10-25, see the header); the white/gray tables stay for a restored
// settle — the gray table lands in a single run because the trimmed GC16
// (9,15) row lost five darken phases to sat_cut and cannot land gray 9, while
// GL16's (9,15) row is the damped 11-darken plus braking tail and flashes
// black when played alone.
static uint8_t e0470_capsule_drag_data[E0470_CAPSULE_DRAG_FRAMES][16][4];
static uint8_t e0470_capsule_white_data[E0470_CAPSULE_WHITE_FRAMES][16][4];
static uint8_t e0470_capsule_gray_data[E0470_CAPSULE_GRAY_K][16][4];

static const EpdWaveformPhases e0470_capsule_drag_phases = {
    .phases = E0470_CAPSULE_DRAG_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_capsule_drag_data[0],
};
static const EpdWaveformPhases e0470_capsule_white_phases = {
    .phases = E0470_CAPSULE_WHITE_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_capsule_white_data[0],
};
static const EpdWaveformPhases e0470_capsule_gray_phases = {
    .phases = E0470_CAPSULE_GRAY_K,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_capsule_gray_data[0],
};
static const EpdWaveformPhases* e0470_capsule_drag_ranges[] = { &e0470_capsule_drag_phases };
static const EpdWaveformPhases* e0470_capsule_white_ranges[] = { &e0470_capsule_white_phases };
static const EpdWaveformPhases* e0470_capsule_gray_ranges[] = { &e0470_capsule_gray_phases };
static const EpdWaveformMode e0470_capsule_drag_mode = {
    .type = 1, .temp_ranges = 1, .range_data = &e0470_capsule_drag_ranges[0],
};
static const EpdWaveformMode e0470_capsule_white_mode = {
    .type = 1, .temp_ranges = 1, .range_data = &e0470_capsule_white_ranges[0],
};
static const EpdWaveformMode e0470_capsule_gray_mode = {
    .type = 1, .temp_ranges = 1, .range_data = &e0470_capsule_gray_ranges[0],
};
static const EpdWaveformMode* e0470_capsule_drag_modes[] = { &e0470_capsule_drag_mode };
static const EpdWaveformMode* e0470_capsule_white_modes[] = { &e0470_capsule_white_mode };
static const EpdWaveformMode* e0470_capsule_gray_modes[] = { &e0470_capsule_gray_mode };

const EpdWaveform E0470_CAPSULE_DRAG_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_capsule_drag_modes,
    .temp_intervals = e0470_intervals,
};
const EpdWaveform E0470_CAPSULE_WHITE_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_capsule_white_modes,
    .temp_intervals = e0470_intervals,
};
const EpdWaveform E0470_CAPSULE_GRAY_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_capsule_gray_modes,
    .temp_intervals = e0470_intervals,
};

/* ---- 错相 GL16 执行表 / Staggered GL16 apply waveform ---- */
static uint8_t e0470_apply_data[1][16][4];
static const EpdWaveformPhases e0470_apply_phases = {
    .phases = 1,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_apply_data[0],
};
static const EpdWaveformPhases* e0470_apply_ranges[] = { &e0470_apply_phases };
static const EpdWaveformMode e0470_apply_mode = {
    .type = 1,
    .temp_ranges = 1,
    .range_data = &e0470_apply_ranges[0],
};
static const EpdWaveformMode* e0470_apply_modes[] = { &e0470_apply_mode };
const EpdWaveform E0470_APPLY_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_apply_modes,
    .temp_intervals = e0470_intervals,
};

/* ---- 阈值 DU / Threshold DU ---- */
// 源表只认目标 0/15。中间灰按 50/50 切开，暗的走整段到黑、亮的走整段到白。
// from 不切片，沿用源表对真实起点的时间序列，上一帧残留的浅墨也会被推到黑或白。
// / Source tables only drive dest 0/15. Mid grays split 50/50: dark runs
// the full path to black, light the full path to white. from is not sliced;
// the source time series for the real start is reused, so leftover ink
// from the last frame is also pushed to black or white.
static uint8_t e0470_complete_du_data[E0470_FULL_DU_FRAMES][16][4];
static const EpdWaveformPhases e0470_complete_du_phases = {
    .phases = E0470_FULL_DU_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_complete_du_data[0],
};
static const EpdWaveformPhases* e0470_complete_du_ranges[] = {
    &e0470_complete_du_phases,
};
static const EpdWaveformMode e0470_complete_du_mode = {
    .type = 1,
    .temp_ranges = 1,
    .range_data = &e0470_complete_du_ranges[0],
};

static void e0470_complete_du_build(void) {
    memset(e0470_complete_du_data, 0, sizeof(e0470_complete_du_data));
    for (int to = 0; to < 16; to++) {
        const int to_bin = to < 8 ? 0 : 15;
        for (int from = 0; from < 16; from++) {
            for (int f = 0; f < E0470_FULL_DU_FRAMES; f++) {
                const int action = lut_get(e0470_full_du_data, f, to_bin, from);
                if (action != 0) lut_or(e0470_complete_du_data, f, to, from, action);
            }
        }
    }
}

// 白底 15→15 源表全保持。挂在已经「往白推」的那一相上再推 1 帧，不增加相数。
// 差分会跳过未变白像素，GL16 必须走全像素这帧才打到白底。
// / Source 15→15 is all-hold. Hang one extra white push on an already-white
// phase without adding phases. Diff skips unchanged white; GL16 must be
// full-pixel for this tick to hit the white background.
static void e0470_gl16_white_tick(uint8_t (*data)[16][4], int frames) {
    int tick = -1;
    for (int f = frames - 1; f >= 0; f--) {
        for (int from = 0; from < 15; from++) {
            if (lut_get(data, f, 15, from) == 2) {
                tick = f;
                break;
            }
        }
        if (tick >= 0) break;
    }
    if (tick < 0) tick = frames > 2 ? frames - 3 : 0;
    lut_or(data, tick, 15, 15, 2);
}

// 普通文字翻页的序列对齐；来源与许可见 THIRD_PARTY_NOTICES.md。
// Text-turn alignment; see THIRD_PARTY_NOTICES.md for provenance and licenses.
static void e0470_left_align(const uint8_t (*src)[16][4], int frames, uint8_t (*dst)[16][4]) {
    memset(dst, 0, (size_t)frames * 16 * 4);
    for (int to = 0; to < 16; to++) {
        for (int from = 0; from < 16; from++) {
            uint8_t seq[64];
            for (int f = 0; f < frames; f++) seq[f] = (uint8_t)lut_get(src, f, to, from);
            int a = 0;
            while (a < frames && seq[a] == 0) a++;
            int z = frames;
            while (z > a && seq[z - 1] == 0) z--;
            for (int f = a; f < z; f++) lut_or(dst, f - a, to, from, seq[f]);
        }
    }
}

/* ---- 完整表 / Full tables ---- */
// DU 20 相，GC16 48 相；GL16 用 RAM 副本以便白底补 1 帧。
// / DU 20, GC16 48; GL16 uses a RAM copy so the white-bg tick can be added.
static uint8_t e0470_full_gl16_live[E0470_FULL_GL16_FRAMES][16][4];
static const EpdWaveformPhases e0470_full_gl16_live_phases = {
    .phases = E0470_FULL_GL16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_full_gl16_live[0],
};
static const EpdWaveformPhases* e0470_full_gl16_live_ranges[] = {
    &e0470_full_gl16_live_phases,
};
static const EpdWaveformMode e0470_full_gl16_live_mode = {
    .type = 5, .temp_ranges = 1, .range_data = &e0470_full_gl16_live_ranges[0],
};
static const EpdWaveformMode* e0470_full_modes[] = {
    &e0470_full_du_mode,
    &e0470_full_gc16_mode,
    &e0470_full_gl16_live_mode,
};

const EpdWaveform E0470_FULL_WAVEFORM = {
    .num_modes = 3,
    .num_temp_ranges = 1,
    .mode_data = e0470_full_modes,
    .temp_intervals = e0470_intervals,
};

/* ---- 8 灰阶表 / 8-gray tables ---- */
// GC16 / GL16 各 30 相，拿灰阶档数换速度。
// / GC16 / GL16 30 phases each; trade gray steps for speed.
static const EpdWaveformMode* e0470_gray8_modes[] = {
    &e0470_complete_du_mode,
    &e0470_gray8_gc16_mode,
    &e0470_gray8_gl16_mode,
};

const EpdWaveform E0470_GRAY8_WAVEFORM = {
    .num_modes = 3,
    .num_temp_ranges = 1,
    .mode_data = e0470_gray8_modes,
    .temp_intervals = e0470_intervals,
};

/* ---- 默认表 / Default tables ---- */
// 完整灰阶表裁掉余量，开机算进 RAM。
// / Trim slack from the full gray tables into RAM at boot.
static uint8_t e0470_gc16_data[E0470_FULL_GC16_FRAMES][16][4];
static uint8_t e0470_gl16_data[E0470_FULL_GL16_FRAMES][16][4];
static const EpdWaveformPhases e0470_gc16_phases = {
    .phases = E0470_GC16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_gc16_data[0],
};
static const EpdWaveformPhases e0470_gl16_phases = {
    .phases = E0470_GL16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_gl16_data[0],
};
static const EpdWaveformPhases* e0470_gc16_ranges[] = { &e0470_gc16_phases };
static const EpdWaveformPhases* e0470_gl16_ranges[] = { &e0470_gl16_phases };
static const EpdWaveformMode e0470_gc16_mode = {
    .type = 2, .temp_ranges = 1, .range_data = &e0470_gc16_ranges[0],
};
static const EpdWaveformMode e0470_gl16_mode = {
    .type = 5, .temp_ranges = 1, .range_data = &e0470_gl16_ranges[0],
};
static const EpdWaveformMode* e0470_modes[] = {
    &e0470_complete_du_mode,
    &e0470_gc16_mode,
    &e0470_gl16_mode,
};

const EpdWaveform E0470_WAVEFORM = {
    .num_modes = 3,
    .num_temp_ranges = 1,
    .mode_data = e0470_modes,
    .temp_intervals = e0470_intervals,
};

static uint8_t e0470_textturn_gl16_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases e0470_textturn_gl16_phases = {
    .phases = E0470_GL16_FRAMES,
    .phase_times = NULL,
    .luts = (const uint8_t*)&e0470_textturn_gl16_data[0],
};
static const EpdWaveformPhases* e0470_textturn_gl16_ranges[] = { &e0470_textturn_gl16_phases };
static const EpdWaveformMode e0470_textturn_gl16_mode = {
    .type = 5, .temp_ranges = 1, .range_data = &e0470_textturn_gl16_ranges[0],
};
static const EpdWaveformMode* e0470_textturn_modes[] = {
    &e0470_textturn_gl16_mode,
};

const EpdWaveform E0470_TEXTTURN_WAVEFORM = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = e0470_textturn_modes,
    .temp_intervals = e0470_intervals,
};

const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode) {
    if (waveform == NULL) return NULL;
    const int type = mode & 0x3F;
    for (int i = 0; i < waveform->num_modes; i++) {
        if (waveform->mode_data[i]->type == type) return waveform->mode_data[i]->range_data[0];
    }
    return NULL;
}

int e0470_phase_action(const EpdWaveformPhases* phases, int phase, int to, int from) {
    if (phases == NULL || phases->luts == NULL) return 0;
    if (phase < 0 || phase >= phases->phases) return 0;
    if ((unsigned)to > 15 || (unsigned)from > 15) return 0;
    const uint8_t* cell = phases->luts + ((size_t)phase * 16 + to) * 4 + from / 4;
    return (*cell >> (6 - 2 * (from % 4))) & 3;
}

/* ---- 裁表黑段补强 / Black-run boost ---- */
// 裁表只砍 to!=15 的黑饱和段，正文换字只剩 13 推而旧字擦白有 18~24 次。这里把 to=0
// 每行补回原厂 18 次连续推黑，占用该行前导保持相；只改写「先擦白后推黑」的整齐行。
// / The trim shortens only the non-white black saturation, leaving 13 pushes for
// new glyphs against 18–24 erases. Restore each to=0 row to the vendor's 18
// consecutive black pushes by consuming leading holds; only tidy
// [erase then black] rows are rewritten.
void e0470_boost_black_column(uint8_t (*data)[16][4], int phases) {
    if (data == NULL || phases <= E0470_TRIM_HOLD) return;
    const int limit = phases - 1 - E0470_TRIM_HOLD;
    // from=0 是保持黑底，不进入差分，也不额外驱动。
    // / from=0 is an unchanged black pixel outside the differential; leave it alone.
    for (int from = 1; from < 16; ++from) {
        int first = -1, black = 0, erase = 0, ordered = 1;
        for (int f = 0; f < phases; ++f) {
            const int action = lut_get(data, f, 0, from);
            if (!action) continue;
            if (first < 0) first = f;
            if (action == 1) ++black;
            else if (action == 2) {
                if (black) ordered = 0;
                else ++erase;
            } else ordered = 0;
        }
        if (!ordered || !black || black >= E0470_BOOST_BLACK_PUSHES) continue;
        const int span = erase + E0470_BOOST_BLACK_PUSHES;
        int start = first;
        if (start + span - 1 > limit) start = limit - span + 1;
        if (start < 0) continue;
        for (int f = 0; f < phases; ++f) lut_set(data, f, 0, from, 0);
        for (int i = 0; i < erase; ++i) lut_set(data, start + i, 0, from, 2);
        for (int i = 0; i < E0470_BOOST_BLACK_PUSHES; ++i)
            lut_set(data, start + erase + i, 0, from, 1);
    }
}

void e0470_waveform_init(void) {
    e0470_follow_lut_build(E0470_FOLLOW_FRAMES, e0470_follow_data);
    e0470_follow_lut_build(1, e0470_apply_data);
    e0470_complete_du_build();

    const e0470_trim_t trim = {
        .erase_max = E0470_TRIM_ERASE_MAX,
        .sat_cut = E0470_TRIM_SAT_CUT,
        .white_sat_cut = E0470_TRIM_WHITE_SAT_CUT,
        .hold = E0470_TRIM_HOLD,
    };
    const int gc = e0470_waveform_trim(&e0470_full_gc16_phases, &trim, e0470_gc16_data);
    const int gl = e0470_waveform_trim(&e0470_full_gl16_phases, &trim, e0470_gl16_data);
    assert(gc == E0470_GC16_FRAMES);
    assert(gl == E0470_GL16_FRAMES);

    // 胶囊拖动表：底灰压 4 相、白擦 8 相的近似序列，中途与松手提交都接受其
    // 近似结果（2026-10-25 取消两段定稿）；其他转换全部保持。
    // Capsule drag table: approximate 4-darken / 8-erase runs for the body gray
    // and white; both mid-drag and the release commit accept its approximation
    // (the two-stage settle was removed 2026-10-25). Every other transition holds.
    memset(e0470_capsule_drag_data, 0, sizeof(e0470_capsule_drag_data));
    for (int f = 0; f < E0470_CAPSULE_DRAG_K; ++f)
        lut_or(e0470_capsule_drag_data, f, E0470_CAPSULE_BODY_LEVEL, 15, 1);
    for (int f = 0; f < E0470_CAPSULE_DRAG_W; ++f)
        lut_or(e0470_capsule_drag_data, f, 15, E0470_CAPSULE_BODY_LEVEL, 2);
    // 备用白表：按 CAPSULE_WHITE_FRAMES 把底灰洗成饱和白（原厂 DU 同款 18 推）。
    // Spare white table: CAPSULE_WHITE_FRAMES erase pushes saturate the body
    // gray to white (the vendor DU's proven 18-push ladder).
    memset(e0470_capsule_white_data, 0, sizeof(e0470_capsule_white_data));
    for (int f = 0; f < E0470_CAPSULE_WHITE_FRAMES; ++f)
        lut_or(e0470_capsule_white_data, f, 15, E0470_CAPSULE_BODY_LEVEL, 2);
    // 备用灰表：从饱和白单调压灰 CAPSULE_GRAY_K 相（旧底灰 9 时的实测标定：
    // 响应前载，1 推即接近灰 9，多推过冲发黑；换 0x30 后需重标）；GL16 的
    // (9,15) 行是 11 推深压 + 回拉尾的阻尼落法，单行播放会黑闪。
    // Spare gray table: darken monotonically for CAPSULE_GRAY_K phases from
    // saturated white (panel-calibrated at the old body gray 9: the response is
    // front-loaded, one push landed near gray 9 and more overshoot dark;
    // recalibrate for 0x30); GL16's (9,15) row is the damped 11-darken plus
    // braking tail and flashes black when played alone.
    memset(e0470_capsule_gray_data, 0, sizeof(e0470_capsule_gray_data));
    for (int f = 0; f < E0470_CAPSULE_GRAY_K; ++f)
        lut_or(e0470_capsule_gray_data, f, E0470_CAPSULE_BODY_LEVEL, 15, 1);

    memcpy(e0470_full_gl16_live, e0470_full_gl16_data, sizeof(e0470_full_gl16_live));
    e0470_gl16_white_tick(e0470_full_gl16_live, E0470_FULL_GL16_FRAMES);
    e0470_gl16_white_tick(e0470_gl16_data, E0470_GL16_FRAMES);
    // 仅文字翻页使用：对齐起点、保持同灰像素，省去非白目标的前导擦白。
    // Text turns only: align starts, hold unchanged grays and omit leading erase for nonwhite targets.
    // 保留上游算法；灰阶与残影表现仍需实屏验收，其他波形不受影响。
    // Keep the upstream algorithm; gray accuracy and ghosting require panel validation. Other profiles are unchanged.
    e0470_left_align(e0470_gl16_data, E0470_GL16_FRAMES, e0470_textturn_gl16_data);
    for (int f = 0; f < E0470_GL16_FRAMES; f++) {
        for (int v = 0; v < 16; v++) {
            e0470_textturn_gl16_data[f][v][v / 4] &= (uint8_t)~(3u << (6 - 2 * (v % 4)));
        }
    }

    for (int to = 0; to < 15; to++) {
        for (int from = 0; from < 16; from++) {
            int f = 0;
            while (f < E0470_GL16_FRAMES && lut_get(e0470_textturn_gl16_data, f, to, from) == 2) {
                e0470_textturn_gl16_data[f][to][from / 4] &= (uint8_t)~(3u << (6 - 2 * (from % 4)));
                ++f;
            }
        }
    }
}
