/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：批准的半屏手账及六行日历，所有数据由调用方提供；不读卡、不分配、不保存。
 * English: Approved half-screen journal and six-row calendar; caller-supplied data, without media IO, allocation or persistence.
 */
#include "ui_reading_journal.h"
#include "ui_kit.h"
#include "assets/reading_scenes.h"
#include <stdio.h>
#include <string.h>

_Static_assert(READING_SCENE_COUNT == READING_JOURNAL_SCENES, "Scene rotation must match assets");
_Static_assert(READING_SCENE_WIDTH * READING_SCENE_SCALE == 210, "Keep art width unchanged");
_Static_assert(READING_SCENE_HEIGHT * READING_SCENE_SCALE == 162, "Keep art height unchanged");

static const char *s_week[]={"周一","周二","周三","周四","周五","周六","周日"};
static void text(uint8_t *fb,int x,int y,int px,const char *value,enum EpdFontFlags align) {
    ui_text_fixed_vc(fb,x,y,px,value,align,false);
}
static void fitted(uint8_t *fb,int x,int y,int px,const char *value,int width,enum EpdFontFlags align) {
    while(px>14 && ui_text_fixed_width_px(px,value)>width) --px;
    text(fb,x,y,px,value,align);
}
static void title(uint8_t *fb,int x,int y,int px,const char *value,int width) {
    char shown[160]; snprintf(shown,sizeof(shown),"%s",value);
    bool clipped=false;
    while (*shown && ui_text_fixed_width_px(px,shown)+(clipped?ui_text_fixed_width_px(px,"..."):0)>width) {
        size_t n=strlen(shown)-1; while(n && ((uint8_t)shown[n]&192)==128) --n;
        shown[n]=0; clipped=true;
    }
    if (clipped) strcat(shown,"...");
    text(fb,x,y,px,shown,EPD_DRAW_ALIGN_LEFT);
}
static void rect(uint8_t *fb,int x,int y,int w,int h,uint8_t gray) {
    ui_fill_rect_fast((EpdRect){x,y,w,h},gray,fb);
}
EpdRect ui_reading_journal_art(EpdRect region) {
    return (EpdRect){region.x+438,region.y+78,210,162};
}
static void art(uint8_t *fb,int x,int y,const reading_journal_model_t *model) {
    unsigned index=model->scene%READING_JOURNAL_SCENES;
    const uint8_t *pixels=reading_scene_pixels[index];
    for (unsigned row=0;row<READING_SCENE_HEIGHT;++row) for(unsigned col=0;col<READING_SCENE_WIDTH;++col) {
        unsigned at=row*READING_SCENE_ROW_BYTES+col/2;
        uint8_t nibble=(pixels[at]>>((col%2)*4))&15;
        if (nibble!=15) rect(fb,x+(int)col*READING_SCENE_SCALE,y+(int)row*READING_SCENE_SCALE,
                             READING_SCENE_SCALE,READING_SCENE_SCALE,(uint8_t)(nibble*17));
    }
}
static void duration(uint8_t *fb,int x,int y,uint64_t ms,int large,int small) {
    uint64_t minutes=ms/60000;
    char hours[32],mins[32];
    snprintf(hours,sizeof(hours),"%llu",(unsigned long long)(minutes/60));
    snprintf(mins,sizeof(mins),"%llu",(unsigned long long)(minutes>=60?minutes%60:minutes));
    int width;
    do {
        width=ui_text_fixed_width_px(large,mins)+ui_text_fixed_width_px(small,minutes>=60?"分":"分钟")+10;
        if(minutes>=60)width+=ui_text_fixed_width_px(large,hours)+ui_text_fixed_width_px(small,"小时")+32;
        if(width>378)--large;
    } while(width>378 && large>30);
    if(minutes>=60) {
        text(fb,x,y,large,hours,EPD_DRAW_ALIGN_LEFT);x+=ui_text_fixed_width_px(large,hours)+8;
        text(fb,x,y+12,small,"小时",EPD_DRAW_ALIGN_LEFT);x+=ui_text_fixed_width_px(small,"小时")+16;
    }
    text(fb,x,y,large,mins,EPD_DRAW_ALIGN_LEFT);x+=ui_text_fixed_width_px(large,mins)+10;
    text(fb,x,y+12,small,minutes>=60?"分":"分钟",EPD_DRAW_ALIGN_LEFT);
}
static void book_row(uint8_t *fb,const reading_journal_record_t *r,int cy,bool detail) {
    char value[96];snprintf(value,sizeof(value),"%lu 分钟",(unsigned long)(r->milliseconds/60000));
    int px=detail?28:27,available=612-ui_text_fixed_width_px(px,value)-30;
    title(fb,36,cy,detail?32:30,r->title,available<398?available:398);
    text(fb,648,cy,px,value,EPD_DRAW_ALIGN_RIGHT);
    snprintf(value,sizeof(value),"%u 章  /  翻页 %lu 次",r->chapters,(unsigned long)r->turns);
    fitted(fb,36,cy+(detail?42:35),detail?25:24,value,612,EPD_DRAW_ALIGN_LEFT);
}
void ui_reading_journal_widget(uint8_t *fb,EpdRect region,const reading_journal_model_t *model,
                               const reading_journal_date_t *today,const char *notice) {
    if(region.x || region.width!=684 || region.height!=472 || (region.y!=168 && region.y!=640))return;
    int top=region.y;
    reading_journal_totals_t total=today->serial?reading_journal_totals(model,today->day):(reading_journal_totals_t){0};
    char value[160];rect(fb,0,top,684,472,255);rect(fb,36,top,612,1,0x90);
    text(fb,36,top+38,32,"阅读手账",EPD_DRAW_ALIGN_LEFT);
    if(today->serial) {
        snprintf(value,sizeof(value),"%02d.%02d %s",today->month,today->day,s_week[today->weekday]);
        text(fb,610,top+38,25,value,EPD_DRAW_ALIGN_RIGHT);
    }
    ui_draw_icon(fb,636,top+38,24,UI_ICON_CHEVRON_RIGHT,0);
    text(fb,36,top+100,26,"今日阅读",EPD_DRAW_ALIGN_LEFT);
    duration(fb,36,top+157,total.milliseconds,78,29);
    art(fb,438,top+78,model);
    snprintf(value,sizeof(value),"%u 本书     %lu 章     翻页 %lu 次",total.books,(unsigned long)total.chapters,(unsigned long)total.turns);
    fitted(fb,36,top+220,27,value,612,EPD_DRAW_ALIGN_LEFT);
    rect(fb,36,top+251,612,1,0x90);
    for(unsigned i=0;i<2 && today->serial;++i) {
        const reading_journal_record_t *r=reading_journal_at(model,today->day,i);
        if(r)book_row(fb,r,top+287+(int)i*87,false);
    }
    if(!total.books)text(fb,36,top+335,29,today->serial?"今天还没有阅读记录":"日期未校准",EPD_DRAW_ALIGN_LEFT);
    if(notice && *notice)fitted(fb,36,top+450,22,notice,560,EPD_DRAW_ALIGN_LEFT);
    else if(!today->serial)text(fb,36,top+450,22,"请先校准日期和时间",EPD_DRAW_ALIGN_LEFT);
    else {
        reading_journal_totals_t month=reading_journal_totals(model,0);
        snprintf(value,sizeof(value),"%d月 · 已读 %u 天",today->month,month.days);
        text(fb,36,top+450,24,value,EPD_DRAW_ALIGN_LEFT);
        if(total.books>2) {
            snprintf(value,sizeof(value),"共%u本",total.books);text(fb,610,top+450,24,value,EPD_DRAW_ALIGN_RIGHT);
        }
    }
    ui_draw_icon(fb,635,top+448,27,UI_ICON_BOOK_OPEN,0);
}
unsigned ui_reading_journal_date_hit(const reading_journal_date_t *today,int x,int y) {
    if(!today->serial || x<36 || x>=648 || y<399 || y>=699)return 0;
    int column=(x-36)*7/612, row=(y-399)/50;
    int day=row*7+column-today->first_weekday+1;
    return day>0 && day<=today->day ? (unsigned)day : 0;
}
static void calendar(uint8_t *fb,const reading_journal_model_t *model,const reading_journal_date_t *today,unsigned selected) {
    static const char *week[]={"一","二","三","四","五","六","日"};
    for(int col=0;col<7;++col)text(fb,36+(2*col+1)*612/14,377,26,week[col],EPD_DRAW_ALIGN_CENTER);
    for(int day=1;day<=today->days;++day) {
        int index=today->first_weekday+day-1,cx=36+(2*(index%7)+1)*612/14,cy=422+(index/7)*50;
        EpdRect box={cx-28,cy-23,56,46};bool chosen=(unsigned)day==selected;
        if(chosen)rect(fb,box.x,box.y,box.width,box.height,0);
        else if(day==today->day) {
            ui_draw_round_rect(fb,box,0,0);ui_draw_round_rect(fb,ui_inset_rect(box,1),0,0);
        }
        char value[16];snprintf(value,sizeof(value),"%d",day);
        if(day>today->day) {
            // 只映射禁选日期单元格的墨色，保留白底和字形抗锯齿。
            // Remap only disabled-date ink, preserving white backgrounds and antialiasing.
            text(fb,cx,cy-3,27,value,EPD_DRAW_ALIGN_CENTER);
            for(int y=box.y;y<box.y+box.height;++y)for(int x=box.x;x<box.x+box.width;++x) {
                unsigned shade=ui_read_pixel(fb,x,y);
                epd_draw_pixel(x,y,(uint8_t)((9+(shade*6+7)/15)*17),fb);
            }
        } else ui_text_fixed_vc(fb,cx,cy-3,27,value,EPD_DRAW_ALIGN_CENTER,chosen);
        if(day<=today->day && reading_journal_at(model,(unsigned)day,0))rect(fb,cx-3,cy+15,6,5,chosen?255:0);
    }
    rect(fb,37,718,7,7,0);text(fb,57,721,23,"有阅读",EPD_DRAW_ALIGN_LEFT);
    ui_draw_round_rect(fb,(EpdRect){168,713,16,16},0,0);text(fb,197,721,23,"今天",EPD_DRAW_ALIGN_LEFT);
    rect(fb,36,749,612,1,0x90);
}
void ui_reading_journal_detail(uint8_t *fb,const reading_journal_model_t *model,
                               const reading_journal_date_t *today,unsigned selected,unsigned page,const char *notice) {
    if(!selected || selected>(unsigned)today->day)selected=(unsigned)today->day;
    char value[192];text(fb,36,118,52,"阅读手账",EPD_DRAW_ALIGN_LEFT);
    reading_journal_totals_t month=today->serial?reading_journal_totals(model,0):(reading_journal_totals_t){0};
    if(today->serial)snprintf(value,sizeof(value),"%d年%d月",today->year,today->month);
    else snprintf(value,sizeof(value),"日期未校准");
    text(fb,36,190,34,value,EPD_DRAW_ALIGN_LEFT);
    duration(fb,36,252,month.milliseconds,58,27);art(fb,438,171,model);
    snprintf(value,sizeof(value),"%u 天阅读    %u 本    %lu 章    翻页 %lu 次",month.days,month.books,(unsigned long)month.chapters,(unsigned long)month.turns);
    fitted(fb,36,331,25,value,612,EPD_DRAW_ALIGN_LEFT);
    if(!today->serial) {text(fb,342,600,29,"请先校准日期和时间",EPD_DRAW_ALIGN_CENTER);return;}
    calendar(fb,model,today,selected);
    int weekday=(today->first_weekday+(int)selected-1)%7;
    snprintf(value,sizeof(value),"%d月%u日  %s",today->month,selected,s_week[weekday]);
    text(fb,36,788,32,value,EPD_DRAW_ALIGN_LEFT);
    if(selected==(unsigned)today->day)text(fb,648,788,24,"今天",EPD_DRAW_ALIGN_RIGHT);
    reading_journal_totals_t day=reading_journal_totals(model,selected);
    snprintf(value,sizeof(value),"%llu 分钟    %u 本    %lu 章    翻页 %lu 次",(unsigned long long)(day.milliseconds/60000),day.books,(unsigned long)day.chapters,(unsigned long)day.turns);
    fitted(fb,36,833,26,value,612,EPD_DRAW_ALIGN_LEFT);
    for(unsigned i=0;i<2;++i) {
        const reading_journal_record_t *r=reading_journal_at(model,selected,page*2+i);
        if(r)book_row(fb,r,894+(int)i*111,true);
        if(i==0 && day.books>1)rect(fb,36,960,612,1,0x90);
    }
    if(!day.books) {
        ui_draw_icon(fb,342,944,48,UI_ICON_BOOK_OPEN,0);
        text(fb,342,1000,29,selected==(unsigned)today->day?"今天还没有阅读记录":"这天没有阅读记录",EPD_DRAW_ALIGN_CENTER);
    }
    unsigned pages=(day.books+1)/2;
    if(pages>1) {
        if(page)ui_draw_icon(fb,54,1082,28,UI_ICON_CHEVRON_LEFT,0);
        if(page+1<pages)ui_draw_icon(fb,630,1082,28,UI_ICON_CHEVRON_RIGHT,0);
        snprintf(value,sizeof(value),"%u / %u",page+1,pages);text(fb,342,1082,23,value,EPD_DRAW_ALIGN_CENTER);
    }
    if(notice && *notice)fitted(fb,342,355,19,notice,612,EPD_DRAW_ALIGN_CENTER);
}
