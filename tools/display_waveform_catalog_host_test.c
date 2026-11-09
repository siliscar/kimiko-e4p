/* 中文：检查全目录与真实表的对应关系及驱动统计；不把软件结果当光学验收。
 * English: Check the complete catalog against real tables and pulse statistics without claiming optical acceptance.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_waveform_catalog.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "display_normalize.h"
#include "e0470_epaper_waveform.h"
static const char *background="";
const char *app_settings_book_background_path(void) {return background;}
int main(void) {
    e0470_waveform_init();
    const EpdWaveform *legacy[DISPLAY_WAVE_COUNT]={
        &E0470_WAVEFORM,&E0470_FULL_WAVEFORM,&E0470_GRAY8_WAVEFORM,&E0470_FOLLOW_WAVEFORM,
        &E0470_TEXTTURN_WAVEFORM,&E0470_CAPSULE_DRAG_WAVEFORM,&E0470_CAPSULE_WHITE_WAVEFORM,&E0470_CAPSULE_GRAY_WAVEFORM,&E0470_APPLY_WAVEFORM,
        display_main_gray_waveform(),display_main_shelf_exit_waveform(),display_main_water_waveform(),display_main_bw_waveform(),display_main_normal_waveform(),display_main_shelf_waveform(MODE_GL16),display_main_shelf_waveform(MODE_GC16),
        display_reader_text_waveform(),display_reader_ink_waveform(),display_reader_water_waveform(),display_reader_bg_waveform(),display_reader_image_waveform(),display_normalize_waveform(false),display_normalize_waveform(true)};
    int reserved=0,executor=0;
    for(int i=0;i<DISPLAY_WAVE_COUNT;++i) {
        const display_waveform_descriptor_t *d=display_waveform_descriptor(i);
        assert(d&&d->name[0]&&d->source[0]&&d->recipe[0]);
        assert((int)d->parent!=i&&(unsigned)d->parent<=DISPLAY_WAVE_COUNT);
        for(int j=0;j<i;++j)assert(strcmp(d->name,display_waveform_descriptor(j)->name));
        int parent=i;
        for(int n=0;parent!=DISPLAY_WAVE_COUNT;++n) {assert(n<DISPLAY_WAVE_COUNT);parent=display_waveform_descriptor(parent)->parent;}
        reserved+=d->role==DISPLAY_WAVE_RESERVED;executor+=d->role==DISPLAY_WAVE_EXECUTOR;
        assert(display_waveform_get(i)==legacy[i]);
        // 每个实际支持模式独立扫描 LUT，与目录查询的结果核对。/ Independently scan every supported LUT mode and compare catalog statistics.
        for(int m=0;m<legacy[i]->num_modes;++m) {
            const EpdWaveformMode *mode=legacy[i]->mode_data[m];
            const EpdWaveformPhases *p=mode->range_data[0];
            display_waveform_info_t info;
            assert(display_waveform_inspect(i,mode->type,&info)&&info.phases==p->phases);
            int equal=0,black=0,white=0;
            for(int g=0;g<16;++g){bool driven=false;for(int f=0;f<p->phases;++f)driven|=e0470_phase_action(p,f,g,g)!=0;equal+=driven;}
            for(int f=0;f<p->phases;++f){black+=e0470_phase_action(p,f,0,15)==1;white+=e0470_phase_action(p,f,15,0)==2;}
            assert(info.equal_pairs_driven==equal&&info.black_pushes_from_white==black&&info.white_pushes_from_black==white);
        }
    }
    assert(DISPLAY_WAVE_COUNT==23&&reserved==3&&executor==1);
    background="/background.png";
    assert(display_waveform_get(DISPLAY_WAVE_READER_WATER)==legacy[DISPLAY_WAVE_READER_WATER]);
    display_waveform_info_t info;
    assert(!display_waveform_descriptor(-1)&&!display_waveform_descriptor(DISPLAY_WAVE_COUNT));
    assert(!display_waveform_get(-1)&&!display_waveform_get(DISPLAY_WAVE_COUNT));
    assert(!display_waveform_inspect(-1,MODE_DU,&info)&&!display_waveform_inspect(DISPLAY_WAVE_DEFAULT,63,&info));
    assert(!display_waveform_inspect(DISPLAY_WAVE_DEFAULT,MODE_DU,NULL));
    // 黑白胶囊复用完整 DU，双向推动强于跟随短表；表本身保持原样。
    // Binary capsules reuse complete DU with stronger pushes in both directions than follow DU; neither table changes.
    display_waveform_info_t complete, follow;
    assert(display_waveform_inspect(DISPLAY_WAVE_DEFAULT,MODE_DU,&complete));
    assert(display_waveform_inspect(DISPLAY_WAVE_FOLLOW,MODE_DU,&follow));
    assert(complete.black_pushes_from_white>follow.black_pushes_from_white);
    assert(complete.white_pushes_from_black>follow.white_pushes_from_black);
    puts("catalog: PASS (23 original table identities, unique acyclic descriptors, reserved/executor roles, all-mode pulse statistics and invalid inputs)");
}
