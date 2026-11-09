/* 中文：固定场景矩阵穿过真实策略、设置快照和分发，记录唯一硬件出口及原错误。
 * English: A fixed scene matrix traverses real policy, settings snapshot and dispatch, recording the sole hardware exit and original error.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display.h"
#include "display_refresh_policy.h"
#include "settings.h"

static int context_bits, lookups, outputs, kind, mode, pad_rows, pad_cols;
static EpdRect bounds;
static EpdWaveform wave;
static display_waveform_id_t selected;
static bool missing;
static const EpdRect area = {7, 11, 131, 149};
static EpdiyHighlevelState hl;
static const enum EpdDrawError fault = EPD_DRAW_OTHER_ERROR;
app_main_refresh_mode_t app_settings_main_refresh_mode(void) {
    return context_bits & 1 ? APP_MAIN_REFRESH_NORMAL : APP_MAIN_REFRESH_WATER;
}
bool app_settings_main_fast_refresh(void) { return (context_bits & 2) != 0; }
const char *app_settings_book_background_path(void) { return context_bits & 4 ? "/bg.png" : ""; }
const EpdWaveform *display_waveform_get(display_waveform_id_t id) {
    ++lookups; selected=id; return missing ? NULL : &wave;
}
static enum EpdDrawError record(EpdiyHighlevelState *h, const EpdWaveform *w, int m, EpdRect a, int k) {
    assert(h==&hl && w==&wave); ++outputs;mode=m;bounds=a;kind=k;return fault;
}
#define AREA(name,k) enum EpdDrawError name(EpdiyHighlevelState *h, const EpdWaveform *w, enum EpdDrawMode m, EpdRect a) {return record(h,w,m,a,k);}
AREA(update_display_area_with, DISPLAY_COVERAGE_AUTO)
AREA(update_display_area_diff_with, DISPLAY_COVERAGE_DIFF)
AREA(update_display_area_full_with, DISPLAY_COVERAGE_FULL)
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState *h,const EpdWaveform *w,enum EpdDrawMode m,EpdRect a,int r,int c) {
    pad_rows=r;pad_cols=c;return record(h,w,m,a,3);
}
enum EpdDrawError update_display_with(EpdiyHighlevelState *h,const EpdWaveform *w,enum EpdDrawMode m) {return record(h,w,m,(EpdRect){0},4);}
enum EpdDrawError update_display_mode_diff_with(EpdiyHighlevelState *h,const EpdWaveform *w,enum EpdDrawMode m) {return record(h,w,m,(EpdRect){0},5);}

// 顺序与公开意图一一对应；期望值独立于解析器。/ Expected entries follow public intents independently of the resolver.
static const display_refresh_plan_t expected[DISPLAY_REFRESH_COUNT] = {
    {DISPLAY_WAVE_DEFAULT,MODE_GL16,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_GL16,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_GL16,DISPLAY_COVERAGE_FULL,0,0},
    {DISPLAY_WAVE_FOLLOW,MODE_DU,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_FOLLOW,MODE_DU,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_DU,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_MAIN_GRAY,MODE_GL16,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_MAIN_GRAY,MODE_GL16,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_GL16,DISPLAY_COVERAGE_DIFF,1,1},
    {DISPLAY_WAVE_READER_TEXT,MODE_GL16,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_READER_INK,MODE_GL16,DISPLAY_COVERAGE_DIFF,0,0},
    {DISPLAY_WAVE_READER_INK,MODE_GL16,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_FULL,MODE_GC16,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_FULL,MODE_GC16,DISPLAY_COVERAGE_FULL,0,0},
    {DISPLAY_WAVE_FOLLOW,MODE_DU,DISPLAY_COVERAGE_AUTO,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_GL16,DISPLAY_COVERAGE_FULL,0,0},
    {DISPLAY_WAVE_NORMALIZE,MODE_GL16,DISPLAY_COVERAGE_FULL,0,0},
    {DISPLAY_WAVE_DEFAULT,MODE_DU,DISPLAY_COVERAGE_DIFF,1,1},
};
static void reset(void) {lookups=outputs=pad_rows=pad_cols=0;}
int main(void) {
    unsigned cases=0;
    for(context_bits=0;context_bits<8;++context_bits) for(int i=0;i<DISPLAY_REFRESH_COUNT;++i) for(unsigned f=0;f<8;++f) {
        display_refresh_plan_t want=expected[i],got;
        unsigned allowed=i==DISPLAY_REFRESH_READER_BODY?3:i==DISPLAY_REFRESH_READER_LAYOUT?1:i==DISPLAY_REFRESH_RESTORE?4:0;
        bool valid=(f&~allowed)==0;
        memset(&got,0xa5,sizeof(got));display_refresh_plan_t untouched=got;
        display_refresh_context_t ctx=display_refresh_context();
        assert(ctx.main_ordinary==!!(context_bits&1)&&ctx.main_fast==!!(context_bits&2)&&ctx.reader_background==!!(context_bits&4));
        assert(display_refresh_resolve(i,f,ctx,&got)==valid);
        if(!valid) assert(!memcmp(&got,&untouched,sizeof(got)));
        if(i==DISPLAY_REFRESH_MAIN_CLOCK&&(context_bits&2)) {want.waveform=DISPLAY_WAVE_MAIN_BW;want.mode=MODE_DU;}
        if(i==DISPLAY_REFRESH_MAIN_DETAIL&&(context_bits&1))want.waveform=DISPLAY_WAVE_MAIN_NORMAL;
        if(i==DISPLAY_REFRESH_MAIN_SCROLL&&(context_bits&1))want.waveform=DISPLAY_WAVE_MAIN_NORMAL;
        if(i==DISPLAY_REFRESH_READER_BODY||i==DISPLAY_REFRESH_READER_LAYOUT) {
            want.waveform=(i==DISPLAY_REFRESH_READER_BODY&&(f&2))?DISPLAY_WAVE_READER_IMAGE:(context_bits&4)?DISPLAY_WAVE_READER_BACKGROUND:DISPLAY_WAVE_READER_INK;
            if(f&1)want.mode=MODE_DU;
        }
        if(i==DISPLAY_REFRESH_RESTORE&&(f&4))want.waveform=DISPLAY_WAVE_NORMALIZE_GRAY;
        reset();enum EpdDrawError e=display_refresh_area(&hl,i,area,f);
        assert(e==(valid?fault:EPD_DRAW_INVALID_CROP));
        assert(outputs==valid&&lookups==valid);
        if(valid) {
            assert(got.waveform==want.waveform&&got.mode==want.mode&&got.coverage==want.coverage);
            assert(got.pad_lines==want.pad_lines&&got.pad_col_bytes==want.pad_col_bytes);
            assert(selected==want.waveform&&mode==want.mode&&kind==(want.pad_lines?3:(int)want.coverage));
            assert(!memcmp(&bounds,&area,sizeof(area))&&pad_rows==want.pad_lines&&pad_cols==want.pad_col_bytes);
        }
        reset();bool screen=valid&&want.coverage!=DISPLAY_COVERAGE_FULL&&!want.pad_lines;
        e=display_refresh_screen(&hl,i,f);
        assert(e==(screen?fault:EPD_DRAW_INVALID_CROP)&&outputs==screen&&lookups==screen);
        if(screen)assert(selected==want.waveform&&mode==want.mode&&kind==(want.coverage==DISPLAY_COVERAGE_DIFF?5:4));
        ++cases;
    }
    reset();missing=true;
    assert(display_refresh_area(&hl,DISPLAY_REFRESH_DETAIL,area,0)==EPD_DRAW_NO_PHASES_AVAILABLE&&outputs==0&&lookups==1);
    reset();assert(display_refresh_screen(&hl,DISPLAY_REFRESH_DETAIL,0)==EPD_DRAW_NO_PHASES_AVAILABLE&&outputs==0&&lookups==1);
    reset();
    assert(display_refresh_area(&hl,-1,area,0)==EPD_DRAW_INVALID_CROP);
    assert(display_refresh_screen(&hl,DISPLAY_REFRESH_COUNT,0)==EPD_DRAW_INVALID_CROP);
    assert(display_refresh_area(&hl,0,area,128)==EPD_DRAW_INVALID_CROP&&outputs==0&&lookups==0);
    assert(!display_refresh_resolve(0,0,(display_refresh_context_t){0},NULL));
    printf("refresh: PASS (%u intent/flag/context combinations, area/screen, rejected requests, missing tables, one output and error propagation)\n",cases);
}
