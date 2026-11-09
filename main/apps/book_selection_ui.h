/* 中文：书架多选与分类选书共用的控件几何和纯绘制，不持有选择状态。
 * English: Shared selection geometry and pure painting for shelf/category picking; owns no selection state. */
#pragma once
#include "ui_kit.h"
#include "assets/ui_icons.h"

typedef enum {
    BOOK_SELECTION_SEARCH, BOOK_SELECTION_CANCEL, BOOK_SELECTION_MORE,
    BOOK_SELECTION_FILTER, BOOK_SELECTION_PAGE_ALL,
    BOOK_SELECTION_REVIEW, BOOK_SELECTION_SUBMIT,
} book_selection_control_t;

/// 绘制和命中共用；底栏下沿1196，保留20px安全边。/ Shared paint/hit bounds; footer ends at 1196 with a 20px inset.
static inline EpdRect book_selection_rect(book_selection_control_t control) {
    switch (control) {
        case BOOK_SELECTION_SEARCH: return (EpdRect){420,80,80,80};
        case BOOK_SELECTION_CANCEL: return (EpdRect){524,80,124,80};
        case BOOK_SELECTION_MORE: return (EpdRect){316,80,80,80};
        case BOOK_SELECTION_FILTER: return (EpdRect){36,184,356,76};
        case BOOK_SELECTION_PAGE_ALL: return (EpdRect){416,184,232,76};
        case BOOK_SELECTION_REVIEW: return (EpdRect){36,1100,278,96};
        case BOOK_SELECTION_SUBMIT: return (EpdRect){338,1100,310,96};
    }
    return (EpdRect){0};
}

static inline void book_selection_text(uint8_t *fb, EpdRect r, int px, const char *label, bool enabled) {
    px=ui_text_effective_px(px);
    if(px>r.height-24)px=r.height-24;
    while(px>12 && ui_text_fixed_width_px(px,label)>r.width)--px;
    ui_text_fixed_vc(fb,r.x+r.width/2,r.y+r.height/2,px,label,EPD_DRAW_ALIGN_CENTER,false);
    // 读回值是4位灰阶，只改字迹，不覆盖白底。/ Readback is 4-bit gray; tint ink only, never the white ground.
    if(!enabled)for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)
        if(ui_read_pixel(fb,x,y)<9)epd_draw_pixel(x,y,0x90,fb);
}

static inline void book_selection_button(uint8_t *fb, EpdRect r, int px, const char *label, bool enabled, bool primary) {
    ui_fill_rect_fast(r,UI_GRAY_WHITE,fb);
    book_selection_text(fb,(EpdRect){r.x+16,r.y,r.width-32,r.height},px,label,enabled);
    ui_draw_control_frame(fb,r,8,enabled?(primary?0:0x50):0x90);
}

/// 分页编号沿用UI_PAGER_FIRST..LAST的0..3；不占用按钮之间的空隙。/ Pager IDs follow UI_PAGER_FIRST..LAST 0..3; gaps remain inert.
static inline EpdRect book_selection_pager_rect(int control) {
    const int x[]={36,124,488,576};
    return control>=0 && control<4?(EpdRect){x[control],1020,72,64}:(EpdRect){0};
}

static inline int book_selection_pager_hit(uint16_t x, uint16_t y) {
    for(int i=0;i<4;++i)if(ui_rect_hit(book_selection_pager_rect(i),x,y))return i;
    return -1;
}

static inline void book_selection_pager_draw(uint8_t *fb, const char *label, int page, int pages) {
    const ui_icon_t icons[]={UI_ICON_PAGER_CHEVRON_FIRST,UI_ICON_CHEVRON_LEFT,
                             UI_ICON_CHEVRON_RIGHT,UI_ICON_PAGER_CHEVRON_LAST};
    book_selection_text(fb,(EpdRect){210,1020,264,64},24,label,true);
    for(int i=0;i<4;++i){
        EpdRect r=book_selection_pager_rect(i);
        bool enabled=i<2?page>0:page+1<pages;
        ui_draw_icon(fb,r.x+r.width/2,r.y+r.height/2,34,icons[i],enabled?0:0x90);
    }
}
