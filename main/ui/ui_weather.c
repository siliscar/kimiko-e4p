/*
 * 中文：已确认的全宽版画天气组件，纯绘制；生命周期只分配一个组件区的4bpp暂存。
 * English: Approved full-width print-style weather widget, paint only; lifecycle allocates one region-sized 4bpp scratch.
 */
#include "ui_weather.h"
#include "ui_kit.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define WEATHER_WIDTH 612
#define WEATHER_HEIGHT 448
#define WEATHER_REGION_WIDTH 684
#define WEATHER_REGION_HEIGHT 472
#define WEATHER_BACKGROUND_INK 0x50
#define WEATHER_BACKGROUND_ROW_BYTES ((WEATHER_WIDTH+7)/8)
extern const uint8_t weather_backgrounds_asset[(WEATHER_SCENE_COUNT-1)*WEATHER_BACKGROUND_ROW_BYTES*WEATHER_HEIGHT]
    asm("_binary_weather_backgrounds_bin_start");
static uint8_t *s_scratch;
bool ui_weather_prepare(void) {
    if (!s_scratch) s_scratch = heap_caps_malloc(WEATHER_REGION_WIDTH * WEATHER_REGION_HEIGHT / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_scratch != NULL;
}
void ui_weather_release(void) { heap_caps_free(s_scratch); s_scratch = NULL; }
static void text(uint8_t *f,int x,int y,int px,const char *s,int align,int ink,int bg) {
    ui_text_fixed_context_aa_vc(f,x,y,px,s,s,align,ink<<4,bg<<4);
}
static void tx(uint8_t *f,int x,int y,int px,const char *s){text(f,x,y,px,s,EPD_DRAW_ALIGN_LEFT,0,15);}
static void tc(uint8_t *f,int x,int y,int px,const char *s){text(f,x,y,px,s,EPD_DRAW_ALIGN_CENTER,0,15);}
static void stroke(uint8_t *f,double ax,double ay,double bx,double by,int width,int gray) {
    int n=(int)ceil(hypot(bx-ax,by-ay)*2);
    for(int i=0;i<=n;i++){
        double t=n?(double)i/n:0;
        epd_fill_circle((int)round(ax+(bx-ax)*t),(int)round(ay+(by-ay)*t),width/2,gray,f);
    }
}
static void draw_background(uint8_t *f,EpdRect region,weather_scene_t scene){
    if(scene<WEATHER_SCENE_SUN||scene>=WEATHER_SCENE_COUNT)return;
    const uint8_t *mask=weather_backgrounds_asset+(scene-WEATHER_SCENE_SUN)*WEATHER_BACKGROUND_ROW_BYTES*WEATHER_HEIGHT;
    for(int y=0;y<region.height;++y)for(int x=0;x<region.width;++x){
        int sx=x*WEATHER_WIDTH/region.width,sy=y*WEATHER_HEIGHT/region.height;
        if(mask[sy*WEATHER_BACKGROUND_ROW_BYTES+sx/8]&(1u<<(sx%8)))
            epd_draw_pixel(region.x+x,region.y+y,WEATHER_BACKGROUND_INK,f);
    }
}

static void solar_event(uint8_t *f,int cx,int cy,bool rising){
    // 日出仅半轮太阳露出地平线；日落用半空的完整太阳与向地面落下的箭头。
    // Sunrise is a half sun at the horizon; sunset is a full airborne sun and a downward ground arrow.
    const double pi=3.141592653589793;
    stroke(f,cx-24,cy+10,cx+24,cy+10,2,0);
    if(rising){
        for(int i=0;i<64;i++){
            double a=pi+i*pi/64,b=pi+(i+1)*pi/64;
            stroke(f,cx+cos(a)*17,cy+10+sin(a)*17,cx+cos(b)*17,cy+10+sin(b)*17,2,0);
        }
    }else{
        for(int i=0;i<96;i++){
            double a=i*2*pi/96,b=(i+1)*2*pi/96;
            stroke(f,cx+cos(a)*8,cy-17+sin(a)*8,cx+cos(b)*8,cy-17+sin(b)*8,2,0);
        }
        stroke(f,cx-13,cy-17,cx-17,cy-17,2,0);
        stroke(f,cx+13,cy-17,cx+17,cy-17,2,0);
        stroke(f,cx,cy-30,cx,cy-34,2,0);
        stroke(f,cx-10,cy-27,cx-13,cy-30,2,0);
        stroke(f,cx+10,cy-27,cx+13,cy-30,2,0);
        stroke(f,cx,cy-3,cx,cy+6,2,0);
        stroke(f,cx,cy+6,cx-4,cy+2,2,0);
        stroke(f,cx,cy+6,cx+4,cy+2,2,0);
    }
}

static void fit_label(char *text_value, int px, int width) {
    while (*text_value && ui_text_fixed_width_px(px, text_value) > width) {
        size_t n = strlen(text_value) - 1;
        while (n && ((unsigned char)text_value[n] & 192) == 128) --n;
        text_value[n] = 0;
    }
}
static uint8_t scratch_pixel(int x, int y) {
    size_t i = (size_t)y * WEATHER_REGION_WIDTH + x;
    return ((i & 1) ? s_scratch[i / 2] >> 4 : s_scratch[i / 2] & 15) << 4;
}
static void clear_foreground_halo(uint8_t *f,EpdRect region){
    // 同一行连续笔画的圆形留白合并为七条横线，避免反复覆盖同一像素。
    // Union the round paper halos of contiguous ink into seven spans instead of repeated pixel writes.
    static const int pads[7]={0,2,2,3,2,2,0};
    for(int y=0;y<region.height;++y)for(int x=0;x<region.width;){
        if(scratch_pixel(x,y)>=224){++x;continue;}
        int start=x;
        do{++x;}while(x<region.width&&scratch_pixel(x,y)<224);
        for(int dy=-3;dy<=3;++dy){
            int yy=y+dy;
            if(yy<0||yy>=region.height)continue;
            int left=start-pads[dy+3],right=x+pads[dy+3];
            if(left<0)left=0;
            if(right>region.width)right=region.width;
            epd_draw_hline(left+region.x,yy+region.y,right-left,255,f);
        }
    }
}
static bool valid_region(EpdRect region) {
    return region.width==WEATHER_REGION_WIDTH&&region.height==WEATHER_REGION_HEIGHT&&
        region.x>=0&&region.y>=0&&region.x<=epd_rotated_display_width()-region.width&&
        region.y<=epd_rotated_display_height()-region.height;
}
static EpdRect refresh_button(EpdRect region) {
    // 底部预留16px，放大后的点击范围不进入导航扩展热区。
    // Keep 16px below the enlarged hit area so it stays outside navigation's expanded touch zone.
    return (EpdRect){region.x+600,region.y+region.height-80,64,64};
}
void ui_weather_draw(uint8_t *f, EpdRect region, bool tomorrow, const app_weather_view_t *view, int64_t now) {
    if (!f || !view || !valid_region(region)) return;
    const int left=region.x,top=region.y;
    const weather_day_t *day = view->has_forecast ? weather_day_for(&view->forecast, now, tomorrow) : NULL;
    EpdRect tabs = {left+448,top+20,200,44};
    epd_fill_rect(region,255,f);
    char value[128];
    snprintf(value,sizeof(value),"%s",view->has_city ? view->city.name : "选择城市");
    fit_label(value,30,360);
    tx(f,left+36,top+42,30,value);
    int arrow = left+46 + ui_text_fixed_width_px(30,value);
    ui_draw_icon(f,arrow,top+42,24,UI_ICON_CHEVRON_RIGHT,0);
    epd_fill_rect(tabs,255,f);
    epd_fill_rect((EpdRect){left+450+(tomorrow?98:0),top+22,96,40},0,f);
    text(f,left+498,top+42,24,"今日",EPD_DRAW_ALIGN_CENTER,tomorrow?0:15,tomorrow?15:0);
    text(f,left+596,top+42,24,"明日",EPD_DRAW_ALIGN_CENTER,tomorrow?15:0,tomorrow?0:15);
    if (weather_clock_valid(now)) weather_date_text(weather_local_day(now)+tomorrow,value,sizeof(value));
    else snprintf(value,sizeof(value),"请先校准日期与时间");
    tx(f,left+36,top+101,24,value);
    if (day) {
        snprintf(value,sizeof(value),"%.0f / %.0f°",day->min_c,day->max_c);
        int px=84;while(px>58&&ui_text_fixed_width_px(px,value)>612)--px;
        tx(f,left+32,top+178,px,value);
        snprintf(value,sizeof(value),"%s",day->condition);fit_label(value,28,612);
        tx(f,left+36,top+243,28,value);
    } else {
        tx(f,left+32,top+178,84,"--°");
        const char *message = view->state == APP_WEATHER_LOADING ? "正在获取天气…" :
            view->notice[0] ? view->notice : !view->has_city ? "自动识别或手动选择城市" : "暂无这一天的有效预报";
        snprintf(value,sizeof(value),"%s",message);fit_label(value,24,612);
        tx(f,left+36,top+243,24,value);
    }
    tx(f,left+36,top+282,21,"最低 / 最高 · °C");
    tc(f,left+126,top+326,22,"白天湿度");
    solar_event(f,left+342,top+329,true);solar_event(f,left+558,top+329,false);
    if(day)snprintf(value,sizeof(value),"%d%%",day->humidity);else snprintf(value,sizeof(value),"--%%");
    tc(f,left+126,top+369,34,value);
    weather_time_text(day?day->sunrise:0,value,sizeof(value));tc(f,left+342,top+369,34,value);
    weather_time_text(day?day->sunset:0,value,sizeof(value));tc(f,left+558,top+369,34,value);
    if(view->state==APP_WEATHER_LOADING)snprintf(value,sizeof(value),"正在更新…");
    else if(day) {
        char updated[16];weather_time_text(view->forecast.fetched,updated,sizeof(updated));
        snprintf(value,sizeof(value),"%s 更新%s%s",updated,
            view->forecast.stale||now>=view->forecast.fresh?" · 缓存":"",
            view->notice[0]?" · 更新失败":"");
    } else snprintf(value,sizeof(value),"点城市名称设置，或重试刷新");
    const EpdRect refresh=refresh_button(region);
    const int footer=refresh.y+refresh.height/2;
    fit_label(value,21,400);tx(f,left+36,footer,21,value);
    ui_draw_icon(f,refresh.x+refresh.width/2,footer,36,UI_ICON_REFRESH_CW,0);
    if (!day || !s_scratch) return;
    // 按逻辑坐标暂存，兼容面板旋转；不额外分配整屏缓冲。
    // Save logical coordinates to support panel rotation without another full-screen buffer.
    for(int y=0;y<region.height;y++)for(int x=0;x<region.width;x+=2)
        s_scratch[((size_t)y*WEATHER_REGION_WIDTH+x)/2] =
            ui_read_pixel(f,left+x,y+top) | (ui_read_pixel(f,left+x+1,y+top)<<4);
    epd_fill_rect(region,255,f);
    draw_background(f,region,weather_scene(day->code));
    epd_fill_rect(tabs,255,f);
    clear_foreground_halo(f,region);
    for(int y=0;y<region.height;y++)for(int x=0;x<region.width;x+=2){
        uint8_t packed=s_scratch[((size_t)y*WEATHER_REGION_WIDTH+x)/2];
        if(packed==255)continue;
        uint8_t low=(packed&15)<<4,high=packed&240;
        if(low<240)epd_draw_pixel(left+x,y+top,low,f);
        if(high<240)epd_draw_pixel(left+x+1,y+top,high,f);
    }
}
ui_weather_action_t ui_weather_hit(EpdRect region, int x, int y) {
    if (!valid_region(region)||x<region.x||x>=region.x+region.width||
        y<region.y||y>=region.y+region.height) return UI_WEATHER_NONE;
    EpdRect refresh=refresh_button(region);
    if(x>=refresh.x&&x<refresh.x+refresh.width&&y>=refresh.y&&y<refresh.y+refresh.height)
        return UI_WEATHER_REFRESH;
    x-=region.x;y-=region.y;
    if (y >= 16 && y < 70) {
        if (x >= 448 && x < 648) return x < 548 ? UI_WEATHER_TODAY : UI_WEATHER_TOMORROW;
        if (x >= 24 && x < 432) return UI_WEATHER_CITY;
    }
    return UI_WEATHER_NONE;
}
