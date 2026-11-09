/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：快刷只保留 1 位封面，转码后立即释放灰阶；切换模式或对比度清理旧格式。
 * User revision: fast Home retains only a 1-bit cover, releases gray after conversion, and drops stale formats on mode/contrast changes.
 * 用户修订：首页原比例点阵独立落卡，复用灰阶缩略图；普通/水波纹复用灰阶缓存，始终预留内存。
 * User revision: persist proportional Home monochrome separately and reuse gray thumbnail files; ordinary/water reuse gray caches with a memory reserve.
 * 中文：首页使用真实续读与完整缓存书摘；绘图不读卡。
 * English: Home uses real resume data and cached complete sentences; painting never reads media.
 * 冻结（用户2026-11-04修订）：按批准设计新增当月阅读手账，不恢复旧月记录与目标；上下等高区可选最近阅读、天气、手账或留白，天气直角铺满并直接接导航。
 * Frozen (user revision 2026-11-04): add the approved current-month journal, not retired records/goals; equal regions choose reading, weather, journal or empty, retaining edge-to-edge weather.
 * 用户2026-11-03修订：最近阅读移除设置图标及其点击入口，组件配置保留长按区域或设置页入口。
 * User revision 2026-11-03: remove the Reading settings icon and tap target; retain configuration through a region hold or Settings.
 * 用户修订：缓存原位换位，只用一个条目的栈空间；保留复用速度，避免启动时复制七条占用半个主任务栈。
 * User revision: reorder the cache in place with one stack entry, retaining hot reuse without copying seven entries into half the main stack at boot.
 * 用户最新修订：续读书名复用已加载系统字体，按原40px直接生成字形；不加载阅读字体或增加内建字库。
 * Latest user revision: resume titles reuse the loaded system face at the original 40px, without loading the reader face or adding built-in fonts.
 * 用户修订：首页静止时按分钟局部更新时间，不扫描图书或重画整页；保留快刷白底。
 * User revision: locally advance the idle clock by minute without scanning books or repainting the page; retain the fast white background.
 * 用户修订：界面底色与控件统一黑、白和固定灰，续读卡改白底；封面和头像保留原灰阶。
 * User revision: UI surfaces and controls use black, white and one fixed gray; resume cards are white, while covers and avatars retain their tones.
 * 用户修订：首页题头与书架、文件和设置共用左侧起点、顶边及52px名义字号，仍使用当前系统字体。
 * User revision: Home's title matches the shelf/files/settings left edge, top and nominal 52px size in the active system face.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "app_registry.h"
#include "app_components_mode.h"
#include "weather_service.h"
#include "ui_weather.h"
#include "reading_journal.h"
#include "ui_reading_journal.h"
#include "app_reading_journal_mode.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "ui_image_dither.h"
#include "book_metadata.h"
#include "book_progress.h"
#include "book_store.h"
#include "book_file_path.h"
#include "book_title.h"
#include "book_excerpt.h"
#include "ui_wallpaper.h"
#include "ui_avatar.h"
#include "ui_builtin_image.h"
#include "board_crashlog.h"
#include "display.h"
#include "display_refresh.h"
#include "board_sd.h"
#include "settings.h"
#include "home_components.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    uint8_t *cover;
    bool cover_fast;
    uint16_t cover_width,cover_height;
    time_t modified;
    book_progress_t progress;
    bool has_progress;
} home_book_t;

static home_book_t s_current, s_recent[6];
typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    off_t size;
    time_t modified;
    uint8_t *pixels;
    bool fast;
    uint16_t width,height;
} home_cover_cache_t;
static home_cover_cache_t s_cover_cache[7];
#define HOME_COVER_RESERVE (512u * 1024u)
static int s_cover_next;
static bool s_weather_tomorrow[APP_HOME_SLOT_COUNT];
static unsigned s_widget_dirty;
#define HOME_REGION_TOP 168
#define HOME_REGION_HEIGHT ((UI_NAV_TOP - HOME_REGION_TOP) / APP_HOME_SLOT_COUNT)
_Static_assert(HOME_REGION_TOP + APP_HOME_SLOT_COUNT * HOME_REGION_HEIGHT == UI_NAV_TOP, "Home regions meet navigation");
static EpdRect home_region(unsigned slot) { return (EpdRect){0,HOME_REGION_TOP+(int)slot*HOME_REGION_HEIGHT,684,HOME_REGION_HEIGHT}; }
static bool s_scan_pending;
static bool s_books_cache_valid;
static bool s_cached_sd_present;
static bool s_cached_sd_mounted;
static unsigned s_cached_store_revision;
static char s_cached_books_dir[128];
static char s_cached_last_path[BOOK_STORE_PATH_MAX];
static EpdRect s_area;
static bool s_clock_pending;
static int64_t s_clock_minute=-2;
static char s_excerpt[BOOK_EXCERPT_CAP];
static uint8_t s_avatar[60*60];
static bool s_avatar_ready,s_avatar_pending;
static char s_avatar_path[288];
static EpdRect s_pressed;
static bool s_press_pending;
static EpdRect continue_card_rect(unsigned slot) { return home_region(slot); }
static EpdRect continue_card_hit_rect(unsigned slot) { return (EpdRect){24,home_region(slot).y+72,636,364}; }
static bool continue_card_pressed(unsigned slot) { return s_press_pending && s_pressed.y == home_region(slot).y; }
static EpdRect control_feedback_area(EpdRect control) {
    return (EpdRect){32,control.y+80,186,250};
}
static bool s_home_force_full_once;
static uint32_t s_cached_progress_revision=UINT32_MAX;
static home_book_t *book_at(int index);

static bool is_book(const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    return book_file_path_supported(name);
}
static void copy_title(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void title_for(home_book_t *item) {
    if (!book_title_from_path(item->path, item->title, sizeof(item->title)))
        copy_title(item->title, sizeof(item->title), "未命名");
}

static void consider_book(const char *path, const struct stat *st) {
    if (s_current.path[0] && !strcmp(path, s_current.path)) return;
    book_progress_t progress = {0};
    bool has_progress = st->st_size >= 0 && (uint64_t)st->st_size <= UINT32_MAX &&
                        book_progress_load(path, (uint32_t)st->st_size, &progress);
    for (int i=0;i<6;++i) if (!strcmp(path,s_recent[i].path)) return;
    uint64_t score = has_progress ? (UINT64_C(1) << 32) + progress.last_open_s : (uint64_t)st->st_mtime;
    int slot = -1;
    for (int i = 0; i < 6; ++i) {
        uint64_t other = s_recent[i].has_progress ?
            (UINT64_C(1) << 32) + s_recent[i].progress.last_open_s : (uint64_t)s_recent[i].modified;
        if (!s_recent[i].path[0] || score > other) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = 5; i > slot; --i) s_recent[i] = s_recent[i - 1];
    memset(&s_recent[slot], 0, sizeof(s_recent[slot]));
    snprintf(s_recent[slot].path, sizeof(s_recent[slot].path), "%s", path);
    s_recent[slot].modified = st->st_mtime;
    s_recent[slot].has_progress = has_progress;
    if (has_progress) s_recent[slot].progress = progress;
}

static void scan_root(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!is_book(entry->d_name)) continue;
        char path[BOOK_STORE_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= sizeof(path)) continue;
        struct stat st;
        if (!stat(path, &st) && S_ISREG(st.st_mode)) consider_book(path, &st);
    }
    closedir(dir);
}

static bool recent_visit(const char *path,const book_progress_t *progress,void *ctx) {
    (void)ctx;
    struct stat st;
    if (is_book(path) && !stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size==progress->file_size)
        consider_book(path,&st);
    return true;
}
static void scan_books(void) {
    char last[BOOK_STORE_PATH_MAX];
    // 不支持的恢复记录只跳过，不删除其他固件的缓存和阅读位置。
    // Ignore unsupported resume records without deleting another firmware's cache or progress.
    if (book_progress_last_path(last, sizeof(last)) && book_file_path_supported(last)) {
        struct stat st;
        if (!stat(last, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX) {
            snprintf(s_current.path, sizeof(s_current.path), "%s", last);
            s_current.has_progress = book_progress_load(last, (uint32_t)st.st_size, &s_current.progress);
            title_for(&s_current);
        }
    }
    (void)book_progress_list(recent_visit,NULL);
    board_sd_info_t sd = {0};
    board_sd_get_info(&sd);
    if (sd.mounted) {
        if (!strcmp(app_settings_books_dir(), "/sdcard/books")) scan_root("/sdcard/book");
        scan_root(app_settings_books_dir());
    }
    if (book_store_flash_ready()) scan_root("/flash/books");
    for (int i=0;i<7;++i) {
        home_book_t *b=book_at(i);
        if (!b->path[0]) continue;
        title_for(b);
        char metadata_title[128];
        if (book_metadata_cached(b->path,metadata_title,sizeof(metadata_title),b->author,sizeof(b->author))==ESP_OK && metadata_title[0])
            copy_title(b->title,sizeof(b->title),metadata_title);
    }
    home_book_t *featured = s_current.path[0] ? &s_current : &s_recent[0];
    if (book_metadata_supported(featured->path) && pico_boot_asset_allowed(featured->path)) {
        char metadata_title[256];
        if (book_metadata_read(featured->path, metadata_title, sizeof(metadata_title),
                               featured->author, sizeof(featured->author)) != ESP_OK)
            featured->author[0] = 0;
        else if (metadata_title[0]) copy_title(featured->title,sizeof(featured->title),metadata_title);
    }
}

static void refresh_progress(void) {
    for (int i = 0; i < 7; ++i) {
        home_book_t *item = book_at(i);
        struct stat st;
        if (!item->path[0] || stat(item->path, &st) || !S_ISREG(st.st_mode) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) continue;
        item->has_progress = book_progress_load(item->path, (uint32_t)st.st_size, &item->progress);
    }
}

static void remember_scan_state(const board_sd_info_t *sd) {
    s_books_cache_valid = true;
    s_cached_store_revision = book_store_revision();
    s_cached_sd_present = sd->present;
    s_cached_sd_mounted = sd->mounted;
    snprintf(s_cached_books_dir, sizeof(s_cached_books_dir), "%s", app_settings_books_dir());
    s_cached_last_path[0] = 0;
    (void)book_progress_last_path(s_cached_last_path, sizeof(s_cached_last_path));
}

static home_book_t *book_at(int index) { return index ? &s_recent[index - 1] : &s_current; }
static void clear_cover_cache(void) {
    for (int i = 0; i < 7; ++i) {
        free(s_cover_cache[i].pixels);
        memset(&s_cover_cache[i], 0, sizeof(s_cover_cache[i]));
    }
    s_current.cover = NULL;
    for (int i = 0; i < 6; ++i) s_recent[i].cover = NULL;
}

void app_home_cover_mode_changed(void) {
    clear_cover_cache();
    s_cover_next = 0;
}
static void restore_cover_cache(void) {
    bool used[7] = {0};
    int match[7];
    for (int i = 0; i < 7; ++i) {
        home_book_t *book = book_at(i);
        struct stat st;
        book->cover = NULL;
        match[i] = -1;
        if (!book->path[0] || stat(book->path, &st) || !S_ISREG(st.st_mode)) continue;
        for (int j = 0; j < 7; ++j) {
            home_cover_cache_t *cached = &s_cover_cache[j];
            if (used[j] || !cached->pixels || strcmp(book->path, cached->path) ||
                strcmp(book->title, cached->title) || strcmp(book->author, cached->author) ||
                st.st_size != cached->size || st.st_mtime != cached->modified) continue;
            match[i] = j;
            used[j] = true;
            break;
        }
    }
    for (int j = 0; j < 7; ++j) if (!used[j]) {
        free(s_cover_cache[j].pixels);
        memset(&s_cover_cache[j], 0, sizeof(s_cover_cache[j]));
    }
    for (int i = 0; i < 7; ++i) {
        int j = match[i];
        if (j < 0) {
            for (j = i; j < 7 && s_cover_cache[j].pixels; ++j) {}
        }
        if (j < 7 && j != i) {
            home_cover_cache_t swap = s_cover_cache[i];
            s_cover_cache[i] = s_cover_cache[j];
            s_cover_cache[j] = swap;
            for (int k = i + 1; k < 7; ++k) {
                if (match[k] == i) match[k] = j;
                else if (match[k] == j) match[k] = i;
            }
        }
        book_at(i)->cover = s_cover_cache[i].pixels;
        book_at(i)->cover_fast = s_cover_cache[i].fast;
        book_at(i)->cover_width=s_cover_cache[i].width;
        book_at(i)->cover_height=s_cover_cache[i].height;
    }
}

static unsigned s_cover_generation;
static bool s_cover_waiting;
static bool load_cover(home_book_t *item,int slot) {
    s_cover_waiting=false;
    if(!pico_boot_asset_allowed(item->path))return false;
    book_cover_job_t request={.list=true,.natural=true,.fast=app_settings_main_fast_refresh(),
        .contrast=app_settings_system_contrast(),.phase=17};
    snprintf(request.path,sizeof(request.path),"%s",item->path);
    snprintf(request.title,sizeof(request.title),"%s",item->title);
    snprintf(request.author,sizeof(request.author),"%s",item->author);
    if(!s_cover_generation)s_cover_generation=book_cover_jobs_begin();
    if(!book_cover_jobs_submit(s_cover_generation,slot,&request)){
        s_cover_generation=book_cover_jobs_begin();
        if(!book_cover_jobs_submit(s_cover_generation,slot,&request))return false;
    }
    book_cover_result_t result={0};int state=book_cover_jobs_take(s_cover_generation,slot,&result);
    if(!state){s_cover_waiting=true;return false;}if(state<0)return false;
    home_cover_cache_t *cached=&s_cover_cache[slot];free(cached->pixels);
    snprintf(cached->path,sizeof(cached->path),"%s",item->path);
    snprintf(cached->title,sizeof(cached->title),"%s",item->title);
    snprintf(cached->author,sizeof(cached->author),"%s",item->author);
    cached->size=result.size;cached->modified=result.modified;cached->pixels=result.pixels;
    cached->fast=result.fast;cached->width=result.width;cached->height=result.height;
    item->cover=result.pixels;item->cover_fast=result.fast;item->cover_width=result.width;item->cover_height=result.height;
    return true;
}

static void fit(char *text,int px,int width) {
    while(*text && ui_text_fixed_width_px(px,text)>width) {
        size_t n=strlen(text)-1;while(n&&((uint8_t)text[n]&0xc0)==0x80)--n;text[n]=0;
    }
}
static void label(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray) {
    if (gray >= 0xf0) ui_text_fixed_vc(fb,x,y,px,text,align,true);
    else ui_text_fixed_ink_vc(fb,x,y,px,text,align,gray);
}
static void label_on(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray,uint8_t background) {
    ui_text_fixed_context_aa_vc(fb,x,y,px,text,text,align,gray,background);
}
// 三像素边线均向内加粗，保持点击及局部刷新边界。/ Three-pixel rules grow inward, preserving hit and dirty bounds.
static void home_frame(uint8_t *fb,EpdRect box,int radius) {
    for(int i=0;i<3;++i)ui_draw_round_rect(fb,ui_inset_rect(box,i),radius>i?radius-i:0,0x30);
}
static home_book_t *featured(void) {return s_current.path[0]?&s_current:&s_recent[0];}
static void draw_cover(uint8_t *fb,const home_book_t *item,EpdRect bounds,bool pressed) {
    EpdRect box=bounds;
    if(item->cover&&item->cover_width&&item->cover_height) {
        unsigned w=item->cover_width,h=item->cover_height;
        if((uint64_t)w*box.height>(uint64_t)h*box.width)box.height=(uint64_t)box.width*h/w;
        else box.width=(uint64_t)box.height*w/h;
        box.x=bounds.x+(bounds.width-box.width)/2;box.y=bounds.y+bounds.height-box.height;
        for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x) {
            unsigned sx=(unsigned)x*w/box.width,sy=(unsigned)y*h/box.height;
            uint8_t g=item->cover_fast?(item->cover[sy*((w+7)/8)+sx/8]&(1u<<(sx&7))?255:0):ui_image_dither_gray(ui_contrast_gray(item->cover[sy*w+sx]),box.x+x,box.y+y);
            epd_draw_pixel(box.x+x,box.y+y,g,fb);
        }
    } else ui_fill_rect_fast(box,0xe0,fb);
    home_frame(fb,box,0);
    // 按下只改白色外缘，避免用 DU 移动灰阶封面后留下旧底边。
    // Change only the white margin on press so DU never moves gray cover pixels or leaves their old bottom edge.
    if(pressed) {
        EpdRect frame=ui_inset_rect(box,-3);
        ui_draw_round_rect(fb,frame,0,UI_GRAY_BLACK);
        ui_draw_round_rect(fb,ui_inset_rect(frame,1),0,UI_GRAY_BLACK);
    }
}

static void avatar_prepare_state(void) {
    const char *path=app_settings_avatar_path();
    if(pico_boot_recovery()||!path[0]){ui_avatar_request("",60);s_avatar_ready=s_avatar_pending=false;s_avatar_path[0]=0;return;}
    if(strcmp(path,s_avatar_path))s_avatar_ready=false;
    snprintf(s_avatar_path,sizeof(s_avatar_path),"%s",path);
    ui_avatar_request(path,60);
    ui_avatar_state_t state=ui_avatar_poll(path,60,s_avatar);
    s_avatar_ready=state==UI_AVATAR_READY;s_avatar_pending=state==UI_AVATAR_PENDING;
}

static int title_lines(uint8_t *fb,int top,const char *text,uint8_t background) {
    char lines[2][128]={{0}};unsigned row=0;int px=40;
    for(const uint8_t *p=(const uint8_t *)text;*p&&row<2;) {
        unsigned n=*p<128?1:*p<0xe0?2:*p<0xf0?3:4;
        size_t used=strlen(lines[row]);
        if(strnlen((const char *)p,n)<n||used+n>=sizeof(lines[row]))break;
        memcpy(lines[row]+used,p,n);lines[row][used+n]=0;
        if(ui_text_fixed_context_width_px(px,lines[row],text)>400) {
            lines[row][used]=0;if(!used)break;
            bool closing=strchr(",.!?)]}",*p)!=NULL || (n==3&&p[0]==0xe3&&p[1]==0x80&&(p[2]==0x81||p[2]==0x82||p[2]==0x89||p[2]==0x91)) ||
                (n==3&&p[0]==0xef&&p[1]==0xbc&&(p[2]==0x8c||p[2]==0x89||p[2]==0x81||p[2]==0x9f));
            if(row==0&&closing) {
                size_t last=used-1;while(last&&((uint8_t)lines[0][last]&0xc0)==0x80)--last;
                memcpy(lines[1],lines[0]+last,used-last);lines[1][used-last]=0;lines[0][last]=0;
            }
            if(++row==2) {
                while(lines[1][0]&&ui_text_fixed_context_width_px(px,lines[1],text)>360) {
                    size_t len=strlen(lines[1])-1;while(len&&(lines[1][len]&0xc0)==0x80)--len;lines[1][len]=0;
                }
                strlcat(lines[1],"…",sizeof(lines[1]));break;
            }
            continue;
        }
        p+=n;
    }
    for(unsigned i=0;i<2;++i)if(lines[i][0])ui_text_fixed_context_aa_vc(fb,248,top+126+44*i,px,lines[i],text,EPD_DRAW_ALIGN_LEFT,0,background);
    return lines[1][0]?2:1;
}
static void render_reading(uint8_t *fb,unsigned slot) {
    const int top=home_region(slot).y;
    label(fb,36,top+38,26,"最近阅读",EPD_DRAW_ALIGN_LEFT,0);
    home_book_t *b=featured();
    if(b->path[0]) {
        draw_cover(fb,b,(EpdRect){36,top+84,178,242},continue_card_pressed(slot));
        int rows=title_lines(fb,top,b->title,UI_GRAY_WHITE);
        if(b->author[0]) {
            char author[128];snprintf(author,sizeof(author),"%s",b->author);fit(author,25,400);
            label_on(fb,248,top+(rows==2?222:181),25,author,EPD_DRAW_ALIGN_LEFT,0,UI_GRAY_WHITE);
        }
        char value[32];snprintf(value,sizeof(value),"已读 %u%%",b->has_progress?b->progress.pct:0);
        label_on(fb,248,top+250,27,value,EPD_DRAW_ALIGN_LEFT,0,UI_GRAY_WHITE);
        EpdRect track={248,top+279,400,6};ui_fill_rect_fast(track,UI_GRAY_LIGHT,fb);
        track.width=400*(b->has_progress?b->progress.pct:0)/100;
        if(track.width)ui_fill_rect_fast(track,0,fb);
        label_on(fb,612,top+322,27,"继续阅读",EPD_DRAW_ALIGN_RIGHT,0,UI_GRAY_WHITE);
        ui_draw_icon(fb,635,top+322,25,UI_ICON_CHEVRON_RIGHT,0);
    } else label_on(fb,342,top+230,23,"打开一本书后，会在这里继续阅读",EPD_DRAW_ALIGN_CENTER,0,UI_GRAY_WHITE);
    if(s_excerpt[0]) {
        char quote[BOOK_EXCERPT_CAP+12];snprintf(quote,sizeof(quote),"“%s”",s_excerpt);
        int px=24;while(px>16&&ui_text_fixed_width_px(px,quote)>612)--px;
        if(ui_text_fixed_width_px(px,quote)<=612)label_on(fb,36,top+390,px,quote,EPD_DRAW_ALIGN_LEFT,0,UI_GRAY_WHITE);
    }
}
static void render_widget(uint8_t *fb,unsigned slot) {
    EpdRect region=home_region(slot);
    ui_fill_rect_fast(region,UI_GRAY_WHITE,fb);
    switch(app_home_component_at(slot)) {
    case APP_HOME_WIDGET_READING: render_reading(fb,slot);break;
    case APP_HOME_WIDGET_WEATHER: ui_weather_draw(fb,region,s_weather_tomorrow[slot],app_weather_view(),app_weather_now());break;
    case APP_HOME_WIDGET_JOURNAL: ui_reading_journal_widget(fb,region,app_reading_journal_model(),app_reading_journal_today(),app_reading_journal_notice());break;
    default: ui_draw_icon(fb,632,region.y+38,27,UI_ICON_SLIDERS_HORIZONTAL,0);break;
    }
}
static app_redraw_t redraw_widgets(app_home_widget_t widget) {
    s_widget_dirty=0;
    for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot)if(app_home_component_at(slot)==widget) {
        EpdRect region=home_region(slot);
        if(!s_widget_dirty)s_area=region;
        else s_area.height=region.y+region.height-s_area.y;
        s_widget_dirty|=1u<<slot;
    }
    return s_widget_dirty?APP_REDRAW_AREA:APP_REDRAW_NONE;
}
static void render_root(uint8_t *fb) {
    ui_nav_status(fb);ui_text(fb,36,91,52,"首页",EPD_DRAW_ALIGN_LEFT,false);
    if(s_avatar_ready)ui_wallpaper_draw_avatar(fb,s_avatar,(EpdRect){588,88,60,60},30);
    else ui_builtin_avatar_draw(fb,588,88,60);
    // 公共导航仅绘制实际底栏，组件直接铺到它的上沿。
    // Shared navigation paints only its actual footer; widgets meet its top edge.
    ui_nav_draw(fb,0);
    for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot)render_widget(fb,slot);
}

static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT},0xff,fb);
    render_root(fb);
}

static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw==APP_REDRAW_NONE)return true;
    int64_t minute=ui_nav_clock_minute();
    if(redraw==APP_REDRAW_AREA&&s_clock_pending) {
        s_clock_pending=false;
        ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);
        enum EpdDrawError result=display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, (EpdRect){24,18,156,44}, DISPLAY_REFRESH_DEFAULT);
        guard_draw_result(ctx->hl,result);if(result==EPD_DRAW_SUCCESS)s_clock_minute=minute;return true;
    }
    s_clock_pending=false;
    if(redraw==APP_REDRAW_AREA&&s_widget_dirty) {
        for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot)if(s_widget_dirty&(1u<<slot))render_widget(ctx->fb,slot);
    }
    else render(ctx,ctx->fb);
    s_widget_dirty=0;
    enum EpdDrawError result;
    if(redraw==APP_REDRAW_AREA)result=display_refresh_area(ctx->hl, s_press_pending ? DISPLAY_REFRESH_FEEDBACK : DISPLAY_REFRESH_DETAIL, s_area, DISPLAY_REFRESH_DEFAULT);
    else if(redraw==APP_REDRAW_FULL||s_home_force_full_once) {s_home_force_full_once=false;result=update_display_full(ctx->hl);}
    else result=update_display_fast_page(ctx->hl);
    guard_draw_result(ctx->hl,result);
    if(redraw!=APP_REDRAW_AREA&&result==EPD_DRAW_SUCCESS)s_clock_minute=minute;
    return true;
}
static void refresh_excerpt(void) {book_excerpt_load(featured()->path,s_excerpt);}
static void on_enter(app_ctx_t *ctx) {
    app_weather_init();app_home_components_init(app_weather_view()->enabled);
    if(app_home_has_widget(APP_HOME_WIDGET_JOURNAL))(void)app_reading_journal_poll();
    memset(s_weather_tomorrow,0,sizeof(s_weather_tomorrow));s_widget_dirty=0;
    if(app_home_has_widget(APP_HOME_WIDGET_WEATHER))(void)ui_weather_prepare();
    s_cover_generation=book_cover_jobs_begin();
    const uint32_t progress_revision=book_progress_revision();
    (void)ctx;s_clock_pending=false;s_press_pending=false;
    s_home_force_full_once=app_settings_home_full_refresh();avatar_prepare_state();
    board_sd_info_t sd={0};esp_err_t error=board_sd_get_info(&sd);
    bool same=s_books_cache_valid&&error!=ESP_ERR_NOT_FINISHED&&s_cached_store_revision==book_store_revision()&&
        s_cached_sd_present==sd.present&&s_cached_sd_mounted==sd.mounted&&!strcmp(s_cached_books_dir,app_settings_books_dir());
    char last[BOOK_STORE_PATH_MAX]={0};book_progress_last_path(last,sizeof(last));
    bool changed=strcmp(last,s_cached_last_path);
    if(same&&!changed){if(s_cached_progress_revision!=progress_revision)refresh_progress();s_scan_pending=false;}
    else {
        memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_scan_pending=!same;
        if(s_scan_pending) board_sd_start_probe();
        scan_books();
        if(same||error!=ESP_ERR_NOT_FINISHED)remember_scan_state(&sd);
    }
    if(!same||changed)restore_cover_cache();
    rp_boot_mark("scan");
    s_cached_progress_revision=progress_revision;
    s_cover_next=0;
    refresh_excerpt();
    rp_boot_mark("excerpt");
}
static void on_media_lost(app_ctx_t *ctx) {
    (void)ctx;clear_cover_cache();memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));
    s_books_cache_valid=false;s_excerpt[0]=0;s_avatar_ready=s_avatar_pending=false;
}
static void on_media_ready(app_ctx_t *ctx) {(void)ctx;s_books_cache_valid=false;s_scan_pending=true;avatar_prepare_state();}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    if(ctx->consumed||(ctx->touch&&ctx->touch->touched))return APP_REDRAW_NONE;
    if(app_home_has_widget(APP_HOME_WIDGET_JOURNAL)&&app_reading_journal_poll())return redraw_widgets(APP_HOME_WIDGET_JOURNAL);
    if(app_weather_poll(app_home_has_widget(APP_HOME_WIDGET_WEATHER))) {
        app_redraw_t weather=redraw_widgets(APP_HOME_WIDGET_WEATHER);
        if(weather!=APP_REDRAW_NONE)return weather;
    }
    if(s_scan_pending) {
        board_sd_info_t info={0};if(board_sd_get_info(&info)==ESP_ERR_NOT_FINISHED)return APP_REDRAW_NONE;
        s_scan_pending=false;
        if(info.mounted) {
            s_cover_generation=book_cover_jobs_begin();
            memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_cover_next=0;
            scan_books();restore_cover_cache();remember_scan_state(&info);refresh_excerpt();avatar_prepare_state();
            return APP_REDRAW_PAGE;
        }remember_scan_state(&info);
    }
    if(s_avatar_pending) {
        ui_avatar_state_t state=ui_avatar_poll(s_avatar_path,60,s_avatar);
        s_avatar_pending=state==UI_AVATAR_PENDING;
        s_avatar_ready=state==UI_AVATAR_READY;
        if(s_avatar_ready){s_area=(EpdRect){588,88,60,60};return APP_REDRAW_AREA;}
    }
    if(s_cover_next==0&&app_home_has_widget(APP_HOME_WIDGET_READING)){
        s_cover_next=1;home_book_t *b=featured();
        if(b->path[0]&&!b->cover){int slot=b==&s_current?0:(int)(b-s_recent)+1;if(load_cover(b,slot))return redraw_widgets(APP_HOME_WIDGET_READING);if(s_cover_waiting)s_cover_next=0;}
    }
    if(ui_nav_clock_minute()!=s_clock_minute) {
        s_clock_pending=true;s_area=(EpdRect){24,18,156,44};return APP_REDRAW_AREA;
    }
    return APP_REDRAW_NONE;
}
static bool hit(EpdRect r,int x,int y) {return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect control(int x,int y) {
    for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot)
        if(app_home_component_at(slot)==APP_HOME_WIDGET_READING&&featured()->path[0]&&hit(continue_card_hit_rect(slot),x,y))return continue_card_rect(slot);
    return(EpdRect){0};
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type==UI_GESTURE_PRESS) {s_pressed=control(ev->x0,ev->y0);s_press_pending=s_pressed.width>0;if(s_press_pending){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    bool restore=s_press_pending;
    if(ev->type==UI_GESTURE_MOVE) {if(s_press_pending&&!hit((EpdRect){24,s_pressed.y+72,636,364},ev->x,ev->y)){s_press_pending=false;s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    s_press_pending=false;
    if(ev->type==UI_GESTURE_LONG_PRESS&&
       hit((EpdRect){0,HOME_REGION_TOP,684,UI_NAV_TOP-HOME_REGION_TOP},ev->x0,ev->y0)) {
        app_components_open(ctx,false);return APP_REDRAW_NONE;
    }
    if(ev->type!=UI_GESTURE_TAP) {if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    int x=ev->x0,y=ev->y0;
    int tab=y>=UI_NAV_TOP?ui_nav_hit(x,y):-1;if(tab>=0){ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
    if(control(x,y).width) {
        home_book_t *b=featured();
        if(b->path[0] && app_book_request_open_from_home(b->path)){ui_nav_request(ctx,1);return APP_REDRAW_NONE;}
        if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}
        return APP_REDRAW_NONE;
    }
    if(hit((EpdRect){588,88,60,60},x,y)){ui_nav_request(ctx,3);return APP_REDRAW_NONE;}
    for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot) {
        EpdRect region=home_region(slot);
        if(!hit(region,x,y))continue;
        if(app_home_component_at(slot)==APP_HOME_WIDGET_JOURNAL) {
            if(hit(ui_reading_journal_art(region),x,y)) {
                (void)app_reading_journal_next_scene();return redraw_widgets(APP_HOME_WIDGET_JOURNAL);
            }
            app_reading_journal_open(ctx);return APP_REDRAW_NONE;
        }
        if(app_home_component_at(slot)!=APP_HOME_WIDGET_WEATHER) {
            if(app_home_component_at(slot)==APP_HOME_WIDGET_EMPTY&&hit((EpdRect){608,region.y+14,52,48},x,y)){app_components_open(ctx,false);return APP_REDRAW_NONE;}
            continue;
        }
        ui_weather_action_t action=ui_weather_hit(region,x,y);
        if(action==UI_WEATHER_SETTINGS){app_components_open(ctx,false);return APP_REDRAW_NONE;}
        if(action==UI_WEATHER_CITY){app_components_open(ctx,true);return APP_REDRAW_NONE;}
        if(action==UI_WEATHER_TODAY||action==UI_WEATHER_TOMORROW) {
            s_weather_tomorrow[slot]=action==UI_WEATHER_TOMORROW;
            s_area=region;s_widget_dirty=1u<<slot;return APP_REDRAW_AREA;
        }
        if(action==UI_WEATHER_REFRESH) {
            (void)app_weather_request(APP_WEATHER_FETCH,NULL);
            return redraw_widgets(APP_HOME_WIDGET_WEATHER);
        }
    }
    if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;
}

static bool no_menu_handle(app_ctx_t *ctx) {(void)ctx;return false;}
static bool main_page_visible(app_ctx_t *ctx) {(void)ctx;return true;}
static display_rect_t area_hint(app_ctx_t *ctx) {(void)ctx;return display_rect_from_epd(s_area);}
static void home_before_lock(app_ctx_t *ctx){(void)ctx;app_weather_cancel_join();(void)app_reading_journal_flush();}
static void home_exit(app_ctx_t *ctx){home_before_lock(ctx);ui_weather_release();book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;}
const app_desc_t app_dashboard={
    .title="首页 Home",.detail="继续阅读",.enter_full=false,.owns_keys=true,.menu_handle_enabled=no_menu_handle,
    .main_page_visible=main_page_visible,.on_enter=on_enter,.on_exit=home_exit,.on_before_lock=home_before_lock,.on_media_lost=on_media_lost,.on_media_ready=on_media_ready,
    .render=render,.present=present,.on_tick=on_tick,.on_gesture=on_gesture,.area_hint=area_hint,
};
