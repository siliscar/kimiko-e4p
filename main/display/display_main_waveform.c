/* SPDX-License-Identifier: Apache-2.0
 * 中文：主页保持等灰；退出书架只向白色加强擦除。柔和水波纹端点单向驱动，中灰保留原序列，不改厂商表和扫描时序。
 * English: Main pages hold equal grays; shelf exit strengthens whitening only toward white. Soft water uses directional endpoints and original intermediate-gray sequences without editing vendor tables or scan timing.
 * 用户修订：快刷亚克力保留校准GL16，移除偏深且慢的试验灰阶；保留同向擦白用于书架分页及退出。
 * User revision: fast acrylic retains calibrated GL16; remove the dark, slow experimental gray drive and keep directional whitening for shelf paging/exit.
 * 用户 2026-10-24 修订：水波纹变黑端点弃用 DU 直推，改走原厂 GL16 前导擦除并补回 18 次推黑；
 * 实机确认灰起点文字欠饱和发花，擦白深度照抄原厂/阅读页校准，白端点、相数与拍时序不变。
 * User revision 2026-10-24: the water black endpoint leaves DU for the vendor GL16
 * erase-first sequence with 18 restored pushes; on-device gray-start text was
 * undersaturating. Erase depth reuses the vendor/reader calibration; the white
 * endpoint, phase count and tick timing are unchanged.
 */
#include "display_main_waveform.h"
#include "display_waveform_smooth.h"
#include "esp_heap_caps.h"
#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;
#define MAIN_ERASE_EXTRA_FRAMES 4
#define MAIN_EXIT_FRAMES (E0470_FULL_DU_FRAMES + MAIN_ERASE_EXTRA_FRAMES)
static uint8_t s_exit_data[MAIN_EXIT_FRAMES][16][4];
static const EpdWaveformPhases s_exit_phases = {MAIN_EXIT_FRAMES, (const uint8_t*)s_exit_data, NULL};
static const EpdWaveformPhases* s_exit_ranges[] = {&s_exit_phases};
static const EpdWaveformMode s_exit_mode = {1, 1, s_exit_ranges};
static const EpdWaveformMode* s_exit_modes[] = {&s_exit_mode};
static EpdWaveform s_exit_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_exit_modes};
static bool s_exit_ready;
static uint8_t s_water_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_water_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_water_data, NULL};
static const EpdWaveformPhases* s_water_ranges[] = {&s_water_phases};
static const EpdWaveformMode s_water_mode = {5, 1, s_water_ranges};
static const EpdWaveformMode* s_water_modes[] = {&s_water_mode};
static EpdWaveform s_water_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_water_modes};
static bool s_water_ready;
static uint8_t s_bw_data[E0470_FULL_DU_FRAMES][16][4];
static const EpdWaveformPhases s_bw_phases={E0470_FULL_DU_FRAMES,(const uint8_t*)s_bw_data,NULL};
static const EpdWaveformPhases* s_bw_ranges[]={&s_bw_phases};
static const EpdWaveformMode s_bw_mode={1,1,s_bw_ranges};
static const EpdWaveformMode* s_bw_modes[]={&s_bw_mode};
static EpdWaveform s_bw_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=s_bw_modes};
static bool s_bw_ready;
_Static_assert(MAIN_EXIT_FRAMES <= E0470_GL16_FRAMES, "soft water must fit endpoint cleanup");
// 水波纹在 DU 段之后额外补 6 相向白推动；书架退出保持原 4 帧。
// Water adds six extra white pushes after the DU segment; shelf exit keeps its original four frames.
#define WATER_ERASE_EXTRA_FRAMES 6
_Static_assert(WATER_ERASE_EXTRA_FRAMES >= MAIN_ERASE_EXTRA_FRAMES, "water whitening must not be weaker than shelf exit");

static int endpoint_action(const EpdWaveformPhases* bw, int f, int to, int from, int extra) {
    if (to == from) return 0;
    if (f < bw->phases) return e0470_phase_action(bw, f, to, from);
    // 只给变白像素补同向推动；未变白底与最终黑字均不增加推动。
    // Add same-direction pushes only to whitening pixels, never unchanged backgrounds or final black text.
    return to == 15 && f < bw->phases + extra ? 2 : 0;
}

const EpdWaveform* display_main_gray_waveform(void) {
    if (!s_ready) {
        const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
        if (!original || original->phases != E0470_GL16_FRAMES || !original->luts || original->phase_times)
            return &E0470_WAVEFORM;
        memcpy(s_data, original->luts, sizeof(s_data));
        for (int f = 0; f < E0470_GL16_FRAMES; ++f)
            for (int v = 0; v < 16; ++v)
                s_data[f][v][v / 4] &= (uint8_t)~(3u << (6 - 2 * (v % 4)));
        s_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_ready = true;
    }
    return &s_waveform;
}

const EpdWaveform* display_main_shelf_exit_waveform(void) {
    if (!s_exit_ready) {
        const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
        if (!bw || bw->phases != E0470_FULL_DU_FRAMES || !bw->luts || bw->phase_times)
            return &E0470_WAVEFORM;
        // 取消GC16中灰到白的反向压黑；一次输出内增强白推动，不另起清屏。
        // Remove GC16's reverse darkening on gray-to-white; strengthen white drive in one update without a separate clear.
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from) {
                for (int f = 0; f < MAIN_EXIT_FRAMES; ++f)
                    s_exit_data[f][to][from / 4] |= (uint8_t)(endpoint_action(bw, f, to, from, MAIN_ERASE_EXTRA_FRAMES) << (6 - 2 * (from % 4)));
            }
        s_exit_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_exit_ready = true;
    }
    return &s_exit_waveform;
}

const EpdWaveform* display_main_water_waveform(void) {
    if (!s_water_ready) {
        const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
        const EpdWaveformPhases* gl = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
        if (!bw || !gl || !bw->luts || !gl->luts || bw->phases != E0470_FULL_DU_FRAMES ||
            gl->phases != E0470_GL16_FRAMES || bw->phase_times || gl->phase_times)
            return &E0470_WAVEFORM;
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from)
                for (int f = 0; f < E0470_GL16_FRAMES; ++f) {
                    // 变白端点保持 DU 直推；变黑端点用原厂 GL16 的先擦白序列，推黑随后统一补足。
                    // The white endpoint keeps DU drives; the black endpoint takes the vendor
                    // erase-first sequence, its pushes restored right after the loop.
                    const int action = to == from ? 0 : to == 15
                        ? endpoint_action(bw, f, 15, from, WATER_ERASE_EXTRA_FRAMES)
                        : e0470_phase_action(gl, f, to, from);
                    s_water_data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
                }
        // 灰起点文字欠饱和：按原厂/阅读页校准补回 18 次连续推黑，擦白深度不变。
        // Gray-start text undersaturates: restore 18 consecutive pushes per the
        // vendor/reader calibration without changing the erase depth.
        e0470_boost_black_column(s_water_data, E0470_GL16_FRAMES);
        s_water_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_water_ready = true;
    }
    return &s_water_waveform;
}

const EpdWaveform* display_main_bw_waveform(void) {
    if(!s_bw_ready) {
        const EpdWaveformPhases* bw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
        if(!bw || !bw->luts || bw->phases!=E0470_FULL_DU_FRAMES || bw->phase_times)return &E0470_WAVEFORM;
        memcpy(s_bw_data,bw->luts,sizeof(s_bw_data));
        for(int f=0;f<E0470_FULL_DU_FRAMES;++f)for(int v=0;v<16;++v)
            s_bw_data[f][v][v/4]&=(uint8_t)~(3u<<(6-2*(v%4)));
        s_bw_waveform.temp_intervals=E0470_WAVEFORM.temp_intervals;s_bw_ready=true;
    }
    return &s_bw_waveform;
}

// 普通切页独立派生；水波纹/快刷不改，内存紧张回退现有柔和表。
// Derive ordinary output independently; water/fast stay unchanged and low memory falls back to the existing soft table.
const EpdWaveform* display_main_normal_waveform(void) {
    static EpdWaveformPhases phases;
    static const EpdWaveformPhases *ranges[]={&phases};
    static EpdWaveformMode mode={MODE_GL16,1,ranges};
    static const EpdWaveformMode *modes[]={&mode};
    static EpdWaveform waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=modes};
    if(phases.luts)return &waveform;
    const EpdWaveform *base=display_main_water_waveform();
    const EpdWaveformPhases *original=e0470_waveform_phases(base,MODE_GL16);
    if(!original||!original->luts||original->phases!=E0470_GL16_FRAMES||original->phase_times)return base;
    uint8_t *data=heap_caps_malloc(E0470_GL16_FRAMES*16*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return base;
    memcpy(data,original->luts,E0470_GL16_FRAMES*16*4);
    for(int to=1;to<15;++to)for(int from=0;from<16;++from)
        if(to!=from)display_soften_gray_pair(data,original,to,from);
    phases=(EpdWaveformPhases){E0470_GL16_FRAMES,data,NULL};
    waveform.temp_intervals=base->temp_intervals;
    return &waveform;
}

// 仅书架普通切页：封面/材质灰阶完整保留，留白只向白推动，不再先压黑。
// Ordinary shelf transitions retain calibrated art grays; whitespace drives only white without reverse blackening.
const EpdWaveform* display_main_shelf_waveform(enum EpdDrawMode draw_mode) {
    static EpdWaveformPhases phases[2];
    static const EpdWaveformPhases *ranges[2][1]={{&phases[0]},{&phases[1]}};
    static EpdWaveformMode modes[2]={{MODE_GL16,1,ranges[0]},{MODE_GC16,1,ranges[1]}};
    static const EpdWaveformMode *mode_ptrs[2][1]={{&modes[0]},{&modes[1]}};
    static EpdWaveform waves[2]={
        {.num_modes=1,.num_temp_ranges=1,.mode_data=mode_ptrs[0]},
        {.num_modes=1,.num_temp_ranges=1,.mode_data=mode_ptrs[1]}};
    int index=draw_mode==MODE_GC16?1:0;
    if(phases[index].luts)return &waves[index];
    const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_WAVEFORM,draw_mode);
    const EpdWaveformPhases *soft=e0470_waveform_phases(display_main_normal_waveform(),MODE_GL16);
    if(!original||!soft||!original->luts||!soft->luts||original->phase_times||soft->phase_times||original->phases>soft->phases)
        return &E0470_WAVEFORM;
    size_t size=(size_t)original->phases*16*4;
    uint8_t *data=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return &E0470_WAVEFORM;
    memcpy(data,original->luts,size);
    for(int f=0;f<original->phases;++f)memcpy(data+f*16*4+15*4,soft->luts+f*16*4+15*4,4);
    phases[index]=(EpdWaveformPhases){original->phases,data,NULL};
    waves[index].temp_intervals=E0470_WAVEFORM.temp_intervals;
    return &waves[index];
}
