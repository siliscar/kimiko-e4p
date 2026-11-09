/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：仅当前月阅读记录；不依赖TF卡，不格式化，不改票根或设置备份。
 * English: Current-month reading records, independent of SD, without formatting or changing tickets/settings backups.
 */
#include "reading_journal.h"
#include "book_store.h"
#include "device_power.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef READING_JOURNAL_STORAGE
#define READING_JOURNAL_STORAGE "/flash/reading-journal"
#endif
static reading_journal_model_t s_model;
static reading_journal_date_t s_today;
static bool s_started, s_loaded, s_dirty, s_manual, s_storage_error, s_capacity_error;
static uint64_t s_sequence;
static int s_slot = 1;
static int64_t s_retry_us, s_saved_us;

static int64_t wall_ms(void) {
    struct timeval value; gettimeofday(&value, NULL);
    int64_t now=(int64_t)value.tv_sec*1000+value.tv_usec/1000;
    if (now < INT64_C(1704067200000)) {
        const pmu_snapshot_t *pmu=device_power_get();
        if (pmu && pmu->time_synced) now=(int64_t)pmu->unix_sec*1000;
    }
    return now;
}
static int read_slot(int slot, reading_journal_model_t *out, uint64_t *sequence) {
    char path[128]; snprintf(path,sizeof(path),"%s-%d.bin",READING_JOURNAL_STORAGE,slot);
    FILE *file=fopen(path,"rb");
    if (!file) return errno==ENOENT ? 0 : -1;
    bool ok=fseek(file,0,SEEK_END)==0;
    long length=ok?ftell(file):-1;
    uint8_t *bytes=NULL;
    if (length>=32 && length<=(long)READING_JOURNAL_FILE_LIMIT)
        bytes=heap_caps_malloc((size_t)length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ok=bytes && fseek(file,0,SEEK_SET)==0 && fread(bytes,1,(size_t)length,file)==(size_t)length;
    if (fclose(file)) ok=false;
    if (ok) ok=reading_journal_decode(out,sequence,bytes,(size_t)length);
    free(bytes); return ok ? 1 : -1;
}
static bool merge_pending(reading_journal_model_t *loaded) {
    for (const reading_journal_record_t *r=s_model.records; r; r=r->next) {
        bool first=true;
        for (unsigned chapter=0; chapter<READING_JOURNAL_CHAPTERS; ++chapter) {
            if (!(r->visited[chapter/8]&(1u<<(chapter%8)))) continue;
            if (!reading_journal_add(loaded,r->day,r->book,r->title,chapter,
                                     first?r->milliseconds:0,first?r->turns:0)) return false;
            first=false;
        }
    }
    return true;
}
static bool load_storage(void) {
    if (book_store_prepare_data()!=ESP_OK) return false;
    reading_journal_model_t slots[2]={{0},{0}};
    uint64_t sequences[2]={0};
    int status[2]={read_slot(0,&slots[0],&sequences[0]),read_slot(1,&slots[1],&sequences[1])};
    int chosen=status[1]==1 && (status[0]!=1 || sequences[1]>sequences[0]) ? 1 : 0;
    if (status[chosen]!=1 && (status[0]<0 || status[1]<0)) goto failed;
    if (status[chosen]==1) {
        reading_journal_model_t *loaded=&slots[chosen];
        // 存储迟到时合并尚未保存的会话，不能用磁盘旧快照覆盖新读数。
        // Merge an unpersisted session after late storage recovery instead of overwriting it with disk state.
        bool changed=reading_journal_sync(loaded,&s_today);
        if (s_model.records && !merge_pending(loaded)) goto failed;
        if (s_manual) {
            loaded->scene=s_model.scene; loaded->scene_day=s_model.scene_day; loaded->scene_ready=true;
        }
        reading_journal_clear(&s_model); s_model=*loaded; memset(loaded,0,sizeof(*loaded));
        s_sequence=sequences[chosen]; s_slot=chosen; s_dirty |= changed;
    }
    reading_journal_clear(&slots[0]); reading_journal_clear(&slots[1]);
    s_loaded=true; return true;
failed:
    reading_journal_clear(&slots[0]); reading_journal_clear(&slots[1]); return false;
}
bool app_reading_journal_flush(void) {
    if (!s_started) return true;
    if (!s_loaded) { s_storage_error=true; return false; }
    if (!s_dirty) return true;
    size_t length=reading_journal_encode(&s_model,s_sequence+1,NULL,0);
    uint8_t *bytes=heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=bytes && reading_journal_encode(&s_model,s_sequence+1,bytes,length)==length;
    bool guarded=ok && epd_flash_begin();
    if (guarded) {
        char path[128]; snprintf(path,sizeof(path),"%s-%d.bin",READING_JOURNAL_STORAGE,1-s_slot);
        FILE *file=fopen(path,"wb");
        ok=file!=NULL;
        if (file) {
            ok=fwrite(bytes,1,length,file)==length && fflush(file)==0;
            if (ok && fsync(fileno(file))) ok=false;
            if (fclose(file)) ok=false;
        }
        epd_flash_end();
    } else ok=false;
    free(bytes); s_storage_error=!ok;
    s_saved_us=esp_timer_get_time();
    if (ok) { ++s_sequence; s_slot=1-s_slot; s_dirty=false; }
    return ok;
}
static bool poll_at(int64_t wall) {
    s_started=true;
    reading_journal_date_t today={0}; (void)reading_journal_date(wall,&today);
    bool changed=memcmp(&today,&s_today,sizeof(today))!=0;
    s_today=today;
    bool old_error=s_storage_error;
    int64_t now=esp_timer_get_time();
    if (!s_loaded && now>=s_retry_us) {
        s_retry_us=now+INT64_C(30000000);
        s_storage_error=!load_storage(); changed=true;
    }
    if (reading_journal_sync(&s_model,&s_today)) { s_dirty=true; changed=true; s_capacity_error=false; }
    if (s_dirty && s_loaded && (changed || now-s_saved_us>=INT64_C(60000000)))
        (void)app_reading_journal_flush();
    return changed || old_error!=s_storage_error;
}
bool app_reading_journal_poll(void) { return poll_at(wall_ms()); }
bool app_reading_journal_relocate(const char *old_path, const char *new_path, uint32_t size) {
    (void)app_reading_journal_poll();
    if (!s_loaded) return false;
    uint64_t old = reading_journal_book_id(old_path, size), next = reading_journal_book_id(new_path, size);
    if (old == next) return app_reading_journal_flush();
    reading_journal_record_t **p = &s_model.records;
    while (*p) {
        reading_journal_record_t *r = *p;
        if (r->book != old) { p = &r->next; continue; }
        reading_journal_record_t *match = s_model.records;
        while (match && (match->book != next || match->day != r->day)) match = match->next;
        if (match) {
            uint64_t ms = (uint64_t)match->milliseconds + r->milliseconds;
            uint64_t turns = (uint64_t)match->turns + r->turns;
            match->milliseconds = ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
            match->turns = turns > UINT32_MAX ? UINT32_MAX : (uint32_t)turns;
            match->chapters = 0;
            for (unsigned i = 0; i < sizeof(match->visited); ++i) {
                match->visited[i] |= r->visited[i];
                for (unsigned bits = match->visited[i]; bits; bits &= bits - 1) ++match->chapters;
            }
            *p = r->next; free(r); --s_model.count;
        } else { r->book = next; p = &r->next; }
        s_dirty = true;
    }
    return app_reading_journal_flush();
}
void app_reading_journal_record(const char *path, const char *title, uint32_t size,
                                unsigned chapter, uint32_t milliseconds, uint32_t turns) {
    if (!path || !*path || !title || !*title) return;
    int64_t now=wall_ms();
    (void)poll_at(now);
    if (!s_today.serial) return;
    uint64_t book=reading_journal_book_id(path,size);
    // 单次采样最多5秒；午夜的尾段属于昨天，跨月只保留本月部分。
    // Samples span at most five seconds; split midnight tails and retain only the current-month portion.
    if (milliseconds>5000) milliseconds=0;
    int64_t local_ms=(now+8*INT64_C(3600000))%INT64_C(86400000);
    uint32_t current=milliseconds;
    if ((uint64_t)current>(uint64_t)local_ms) {
        uint32_t previous=current-(uint32_t)local_ms; current=(uint32_t)local_ms;
        if (s_today.day>1 && !reading_journal_add(&s_model,s_today.day-1,book,title,chapter,previous,0)) s_capacity_error=true;
    }
    if (!reading_journal_add(&s_model,s_today.day,book,title,chapter,current,turns)) s_capacity_error=true;
    s_dirty=true;
}
bool app_reading_journal_next_scene(void) {
    (void)app_reading_journal_poll();
    reading_journal_next_scene(&s_model); s_manual=s_dirty=true;
    return app_reading_journal_flush();
}
const reading_journal_model_t *app_reading_journal_model(void) { return &s_model; }
const reading_journal_date_t *app_reading_journal_today(void) { return &s_today; }
const char *app_reading_journal_notice(void) {
    if (!s_today.serial) return "请先校准日期和时间";
    if (s_capacity_error) return "记录空间不足，部分记录未保存";
    if (s_storage_error) return "记录暂未保存，请检查内置存储";
    return "";
}
