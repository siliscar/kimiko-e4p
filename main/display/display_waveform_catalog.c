/*
 * 中文：统一登记现有波形，保留原构造器和原始表；不读取页面或全局背景设置。
 * English: Register existing waveforms without changing their builders or data; never read page or background settings.
 */
#include "display_waveform_catalog.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "display_normalize.h"
#include "e0470_epaper_waveform.h"
#include <stddef.h>

#define BASE "components/e0470_epaper_waveform/e0470_epaper_waveform.c"
#define MAIN "main/display/display_main_waveform.c"
#define READER "main/display/display_reader_waveform.c"
#define NORMALIZE "main/display/display_normalize.c"
#define ENTRY(id, label, file, base, role, note) [id] = {label, file, note, base, role}
static const display_waveform_descriptor_t s_catalog[DISPLAY_WAVE_COUNT] = {
    ENTRY(DISPLAY_WAVE_DEFAULT,"default",BASE,DISPLAY_WAVE_FULL,DISPLAY_WAVE_ACTIVE,"裁剪 GC16/GL16 与阈值 DU / Trimmed GC16/GL16 and threshold DU"),
    ENTRY(DISPLAY_WAVE_FULL,"full",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_ACTIVE,"完整表，GL16 含白底补推 / Full tables, GL16 includes white tick"),
    ENTRY(DISPLAY_WAVE_GRAY8,"gray8",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_RESERVED,"备用八灰阶，产品无调用 / Reserved eight-gray table, no product caller"),
    ENTRY(DISPLAY_WAVE_FOLLOW,"follow",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_ACTIVE,"比例近似短 DU / Proportional approximate short DU"),
    ENTRY(DISPLAY_WAVE_TEXT_BASE,"text-base",BASE,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"左对齐、等灰保持、剥前导擦白 / Left align, hold equal grays, strip leading erase"),
    ENTRY(DISPLAY_WAVE_CAPSULE_DRAG,"capsule-drag",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_ACTIVE,"仅灰 3 与白 15 互转 / Only level 3 and white 15 transitions"),
    ENTRY(DISPLAY_WAVE_CAPSULE_WHITE,"capsule-white",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_RESERVED,"备用胶囊擦白 / Reserved capsule whitening"),
    ENTRY(DISPLAY_WAVE_CAPSULE_GRAY,"capsule-gray",BASE,DISPLAY_WAVE_COUNT,DISPLAY_WAVE_RESERVED,"备用胶囊落灰，换底色需重标 / Reserved capsule gray, recoloring requires calibration"),
    ENTRY(DISPLAY_WAVE_APPLY,"apply",BASE,DISPLAY_WAVE_FOLLOW,DISPLAY_WAVE_EXECUTOR,"错相逐带 LUT 的单相执行壳 / Single-phase shell for staggered band LUTs"),
    ENTRY(DISPLAY_WAVE_MAIN_GRAY,"main-gray",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"原灰阶保持相等对 / Original grays with equal pairs held"),
    ENTRY(DISPLAY_WAVE_SHELF_EXIT,"shelf-exit",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"DU 加四相擦白 / DU plus four whitening phases"),
    ENTRY(DISPLAY_WAVE_MAIN_WATER,"main-water",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"24 擦白、18 推黑、等灰保持 / 24 white pushes, 18 black pushes, held equals"),
    ENTRY(DISPLAY_WAVE_MAIN_BW,"main-bw",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"阈值 DU，等灰保持 / Threshold DU with held equal pairs"),
    ENTRY(DISPLAY_WAVE_MAIN_NORMAL,"main-normal",MAIN,DISPLAY_WAVE_MAIN_WATER,DISPLAY_WAVE_ACTIVE,"中灰脉冲交错，OOM 回退主页水波纹 / Interleave midgray pulses, OOM falls back to main water"),
    ENTRY(DISPLAY_WAVE_SHELF_GL16,"shelf-gl16",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"校准灰阶加普通主页白列 / Calibrated grays plus normal-main white row"),
    ENTRY(DISPLAY_WAVE_SHELF_GC16,"shelf-gc16",MAIN,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"书架周期整理，保留白列特例 / Shelf cleanup retaining the white-row exception"),
    ENTRY(DISPLAY_WAVE_READER_TEXT,"reader-text",READER,DISPLAY_WAVE_TEXT_BASE,DISPLAY_WAVE_ACTIVE,"软灰边、主页白列和黑段补强 / Soft edges, main white row and black boost"),
    ENTRY(DISPLAY_WAVE_READER_INK,"reader-ink",READER,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"与背景开关无关的整页基底 / Background-independent full-page base"),
    ENTRY(DISPLAY_WAVE_READER_WATER,"reader-water",READER,DISPLAY_WAVE_DEFAULT,DISPLAY_WAVE_ACTIVE,"保留前导擦除和原白列，补黑 / Preserve leading erase and original white row, boost black"),
    ENTRY(DISPLAY_WAVE_READER_BACKGROUND,"reader-background",READER,DISPLAY_WAVE_READER_WATER,DISPLAY_WAVE_ACTIVE,"二次裁剪、剥深色擦除、补白 / Retrim, strip dark-pair erase, boost white"),
    ENTRY(DISPLAY_WAVE_READER_IMAGE,"reader-image",READER,DISPLAY_WAVE_READER_WATER,DISPLAY_WAVE_ACTIVE,"原厂擦除与完整推黑、等灰保持 / Vendor erase with the full black run and held equals"),
    ENTRY(DISPLAY_WAVE_NORMALIZE,"normalize",NORMALIZE,DISPLAY_WAVE_READER_INK,DISPLAY_WAVE_ACTIVE,"字芯统一 18 擦白和 18 推黑 / Normalize glyph cores with 18 white and 18 black pushes"),
    ENTRY(DISPLAY_WAVE_NORMALIZE_GRAY,"normalize-gray",NORMALIZE,DISPLAY_WAVE_READER_INK,DISPLAY_WAVE_ACTIVE,"归一字芯，非黑相等对保持 / Normalize black cores, hold nonblack equal pairs"),
};
#undef ENTRY

const display_waveform_descriptor_t *display_waveform_descriptor(display_waveform_id_t id) {
    return (unsigned)id < DISPLAY_WAVE_COUNT ? &s_catalog[id] : NULL;
}

const EpdWaveform *display_waveform_get(display_waveform_id_t id) {
    switch (id) {
        case DISPLAY_WAVE_DEFAULT: return &E0470_WAVEFORM;
        case DISPLAY_WAVE_FULL: return &E0470_FULL_WAVEFORM;
        case DISPLAY_WAVE_GRAY8: return &E0470_GRAY8_WAVEFORM;
        case DISPLAY_WAVE_FOLLOW: return &E0470_FOLLOW_WAVEFORM;
        case DISPLAY_WAVE_TEXT_BASE: return &E0470_TEXTTURN_WAVEFORM;
        case DISPLAY_WAVE_CAPSULE_DRAG: return &E0470_CAPSULE_DRAG_WAVEFORM;
        case DISPLAY_WAVE_CAPSULE_WHITE: return &E0470_CAPSULE_WHITE_WAVEFORM;
        case DISPLAY_WAVE_CAPSULE_GRAY: return &E0470_CAPSULE_GRAY_WAVEFORM;
        case DISPLAY_WAVE_APPLY: return &E0470_APPLY_WAVEFORM;
        case DISPLAY_WAVE_MAIN_GRAY: return display_main_gray_waveform();
        case DISPLAY_WAVE_SHELF_EXIT: return display_main_shelf_exit_waveform();
        case DISPLAY_WAVE_MAIN_WATER: return display_main_water_waveform();
        case DISPLAY_WAVE_MAIN_BW: return display_main_bw_waveform();
        case DISPLAY_WAVE_MAIN_NORMAL: return display_main_normal_waveform();
        case DISPLAY_WAVE_SHELF_GL16: return display_main_shelf_waveform(MODE_GL16);
        case DISPLAY_WAVE_SHELF_GC16: return display_main_shelf_waveform(MODE_GC16);
        case DISPLAY_WAVE_READER_TEXT: return display_reader_text_waveform();
        case DISPLAY_WAVE_READER_INK: return display_reader_ink_waveform();
        case DISPLAY_WAVE_READER_WATER: return display_reader_water_base_waveform();
        case DISPLAY_WAVE_READER_BACKGROUND: return display_reader_bg_waveform();
        case DISPLAY_WAVE_READER_IMAGE: return display_reader_image_waveform();
        case DISPLAY_WAVE_NORMALIZE: return display_normalize_waveform(false);
        case DISPLAY_WAVE_NORMALIZE_GRAY: return display_normalize_waveform(true);
        case DISPLAY_WAVE_COUNT: return NULL;
    }
    return NULL;
}

bool display_waveform_inspect(display_waveform_id_t id, int mode, display_waveform_info_t *info) {
    if (!info) return false;
    const EpdWaveformPhases *p = e0470_waveform_phases(display_waveform_get(id), mode);
    if (!p || !p->luts) return false;
    *info = (display_waveform_info_t){.phases=p->phases};
    for (int gray=0; gray<16; ++gray) {
        bool driven=false;
        for (int f=0; f<p->phases; ++f) driven |= e0470_phase_action(p,f,gray,gray)!=0;
        info->equal_pairs_driven += driven;
    }
    for (int f=0; f<p->phases; ++f) {
        info->black_pushes_from_white += e0470_phase_action(p,f,0,15)==1;
        info->white_pushes_from_black += e0470_phase_action(p,f,15,0)==2;
    }
    return true;
}
