#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实手账模型、双快照故障及午夜归属回归。/ English: Regress the real journal model, snapshot failures and midnight attribution."""
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "reading_journal.h"
#include "device_power.h"
static bool allocation_ok=true,mount_ok=true,guard_ok=true,write_ok=true;
static int guards,ends,wall_reads;
static int64_t clock_ms,monotonic_us,clock_step;
void *heap_caps_malloc(size_t n,int caps){(void)caps;return allocation_ok?malloc(n):NULL;}
void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;return allocation_ok?calloc(n,size):NULL;}
int book_store_prepare_data(void){return mount_ok?0:1;}
bool epd_flash_begin(void){++guards;return guard_ok;}
void epd_flash_end(void){++ends;}
int64_t esp_timer_get_time(void){return monotonic_us;}
const pmu_snapshot_t *device_power_get(void){return NULL;}
static int test_gettimeofday(struct timeval *t,void *zone){(void)zone;++wall_reads;t->tv_sec=clock_ms/1000;t->tv_usec=(clock_ms%1000)*1000;clock_ms+=clock_step;return 0;}
static size_t test_fwrite(const void *p,size_t n,size_t count,FILE *f){
    if(write_ok)return fwrite(p,n,count,f);
    if(n*count>5)(void)fwrite(p,1,5,f);
    return 0;
}
#define gettimeofday test_gettimeofday
#define fwrite test_fwrite
#include "reading_journal_model.c"
#include "reading_journal.c"
#undef fwrite
#undef gettimeofday
static int64_t civil(int year,int month,int day,int hour,int minute,int second){
    struct tm date={.tm_year=year-1900,.tm_mon=month-1,.tm_mday=day,.tm_hour=hour,.tm_min=minute,.tm_sec=second};
    return ((int64_t)timegm(&date)-8*3600)*1000;
}
static reading_journal_date_t date(int year,int month,int day){
    reading_journal_date_t d;assert(reading_journal_date(civil(year,month,day,12,0,0),&d));return d;
}
static void model_cases(void){
    reading_journal_model_t m={0},decoded={0};reading_journal_date_t d=date(2026,11,4);
    assert(d.day==4&&d.weekday==2&&d.first_weekday==6&&d.days==30);
    assert(date(2028,2,1).days==29&&date(2027,2,1).days==28);
    assert(!reading_journal_date(0,&d)&&!d.serial);d=date(2026,11,4);
    assert(reading_journal_sync(&m,&d)&&!reading_journal_sync(&m,&d));
    unsigned initial=m.scene;reading_journal_next_scene(&m);assert(m.scene==(initial+1)%6);
    assert(!reading_journal_sync(&m,&d));d=date(2026,11,5);reading_journal_sync(&m,&d);assert(m.scene==(initial+2)%6);
    d=date(2026,11,9);reading_journal_sync(&m,&d);assert(m.scene==initial);
    d=date(2026,11,4);reading_journal_sync(&m,&d);assert(m.scene==initial);
    uint64_t book=reading_journal_book_id("/a.epub",100);
    assert(book!=reading_journal_book_id("/b.epub",100)&&book!=reading_journal_book_id("/a.epub",101));
    assert(reading_journal_add(&m,4,book,"A",1,60000,10));
    assert(reading_journal_add(&m,4,book,"A",1,60000,1));
    assert(reading_journal_add(&m,4,book,"A",8191,0,1));
    assert(!reading_journal_add(&m,4,book,"A",8192,0,0));
    assert(!reading_journal_add(&m,31,book,"A",2,0,0));
    assert(reading_journal_add(&m,3,book,"A",1,120000,2));
    assert(reading_journal_add(&m,4,book+1,"B",0,180000,3));
    reading_journal_totals_t total=reading_journal_totals(&m,4);
    assert(total.milliseconds==300000&&total.books==2&&total.chapters==3&&total.turns==15&&total.days==1);
    total=reading_journal_totals(&m,0);assert(total.milliseconds==420000&&total.books==2&&total.chapters==4&&total.days==2);
    assert(reading_journal_at(&m,4,0)->book==book+1&&reading_journal_at(&m,4,1)->book==book&&!reading_journal_at(&m,4,2));
    size_t length=reading_journal_encode(&m,42,NULL,0);uint8_t *data=malloc(length);
    assert(!reading_journal_encode(&m,42,data,length-1));assert(reading_journal_encode(&m,42,data,length)==length);
    uint64_t seq=0;assert(reading_journal_decode(&decoded,&seq,data,length)&&seq==42&&decoded.count==3);
    assert(reading_journal_totals(&decoded,0).milliseconds==420000);
    for(size_t i=0;i<length;++i){data[i]^=1;assert(!reading_journal_decode(&decoded,NULL,data,length)&&decoded.count==3);data[i]^=1;}
    for(size_t i=0;i<length;++i)assert(!reading_journal_decode(&decoded,NULL,data,i));
    allocation_ok=false;assert(!reading_journal_decode(&decoded,NULL,data,length));assert(decoded.count==3);
    assert(!reading_journal_add(&m,4,book+2,"C",0,0,0));allocation_ok=true;free(data);
    d=date(2026,12,1);unsigned previous=m.scene;reading_journal_sync(&m,&d);
    assert(!m.count&&m.month==202612&&m.scene==(previous+27)%6);
    for(unsigned i=0;i<READING_JOURNAL_RECORDS;++i)assert(reading_journal_add(&m,1,i+1,"Capacity",i%8192,1,1));
    assert(!reading_journal_add(&m,1,9999,"Full",0,1,1)&&m.count==512);
    assert(reading_journal_add(&m,1,1,"Capacity",12,1,1));
    reading_journal_clear(&m);reading_journal_clear(&decoded);
    puts("PASS: civil dates, leap years, six-row months, scene sequence/rollback, identity, chapter dedup, totals, limits, allocation failure and every-byte snapshot corruption");
}
static void simulate_restart(void){
    reading_journal_clear(&s_model);memset(&s_model,0,sizeof(s_model));memset(&s_today,0,sizeof(s_today));
    s_started=s_loaded=s_dirty=s_manual=s_storage_error=s_capacity_error=false;
    s_sequence=0;s_slot=1;s_retry_us=s_saved_us=0;monotonic_us+=1000000;
}
static void remove_slots(void){char p[256];for(int i=0;i<2;++i){snprintf(p,sizeof(p),"%s-%d.bin",READING_JOURNAL_STORAGE,i);unlink(p);}}
static void record(unsigned chapter,uint32_t ms,uint32_t turns){app_reading_journal_record("/sdcard/books/A.epub","A",10,chapter,ms,turns);}
static void service_cases(void){
    remove_slots();clock_ms=civil(2026,11,4,12,0,0);simulate_restart();
    assert(app_reading_journal_poll()&&s_loaded&&!s_storage_error);
    record(1,1000,1);assert(app_reading_journal_flush());uint64_t first=s_sequence;
    simulate_restart();app_reading_journal_poll();assert(s_sequence==first&&reading_journal_totals(&s_model,4).milliseconds==1000);
    record(1,1000,1);assert(app_reading_journal_flush());
    unsigned scene=s_model.scene;assert(app_reading_journal_next_scene());scene=(scene+1)%6;
    simulate_restart();app_reading_journal_poll();assert(s_model.scene==scene&&reading_journal_totals(&s_model,4).turns==2);
    int old_ends=ends;guard_ok=false;record(2,1000,1);assert(!app_reading_journal_flush()&&s_dirty&&s_storage_error);
    assert(ends==old_ends);guard_ok=true;assert(app_reading_journal_flush());
    record(3,1000,1);write_ok=false;assert(!app_reading_journal_flush());write_ok=true;
    simulate_restart();app_reading_journal_poll();assert(s_loaded&&!s_storage_error&&reading_journal_totals(&s_model,4).milliseconds==3000);
    // 延迟挂载必须把新会话叠加到已保存数据上。/ Late mounting must merge the new session with saved data.
    simulate_restart();mount_ok=false;record(4,2000,1);assert(s_model.count==1&&!s_loaded&&s_storage_error);
    mount_ok=true;monotonic_us+=31000000;app_reading_journal_poll();
    assert(s_loaded&&reading_journal_totals(&s_model,4).milliseconds==5000&&reading_journal_totals(&s_model,4).chapters==3);
    assert(app_reading_journal_flush());
    clock_ms=civil(2026,11,5,0,0,0)+200;record(5,500,0);
    assert(reading_journal_totals(&s_model,4).milliseconds==5300&&reading_journal_totals(&s_model,5).milliseconds==200);
    assert(s_model.scene==(scene+1)%6);
    clock_ms=civil(2026,12,1,0,0,0)+200;record(0,500,0);
    assert(s_model.month==202612&&s_model.count==1&&reading_journal_totals(&s_model,1).milliseconds==200);
    assert(app_reading_journal_flush());
    clock_ms=0;record(3,5000,1);assert(!s_today.serial&&s_model.count==1&&reading_journal_totals(&s_model,0).milliseconds==200);
    assert(strstr(app_reading_journal_notice(),"校准"));
    simulate_restart();remove_slots();
    clock_ms=civil(2026,11,5,23,59,59)+900;clock_step=200;wall_reads=0;
    record(0,500,0);
    assert(wall_reads==1&&s_today.day==5&&reading_journal_totals(&s_model,5).milliseconds==500);
    assert(!reading_journal_at(&s_model,4,0));clock_step=0;
    monotonic_us+=61000000;app_reading_journal_poll();assert(!s_dirty);
    simulate_restart();remove_slots();clock_ms=civil(2026,11,4,12,0,0);app_reading_journal_poll();
    record(1,1000,1);
    app_reading_journal_record("/sdcard/books/分类/A.epub","A",10,2,500,2);
    write_ok=false;assert(!app_reading_journal_relocate("/sdcard/books/A.epub","/sdcard/books/分类/A.epub",10));
    write_ok=true;assert(app_reading_journal_relocate("/sdcard/books/A.epub","/sdcard/books/分类/A.epub",10));
    assert(s_model.count==1&&reading_journal_totals(&s_model,4).milliseconds==1500);
    assert(reading_journal_totals(&s_model,4).chapters==2&&reading_journal_totals(&s_model,4).turns==3);
    simulate_restart();app_reading_journal_poll();assert(s_model.count==1);
    assert(app_reading_journal_relocate("/sdcard/books/A.epub","/sdcard/books/分类/A.epub",10));
    assert(reading_journal_totals(&s_model,4).milliseconds==1500);
    simulate_restart();remove_slots();
    puts("PASS: reboot persistence, shared manual scenes, flash guard/write failure, partial-new-slot fallback, late-mount merge, midnight/month split, invalid time and idempotent category identity merge");
}
int main(void){model_cases();service_cases();assert(guards>=ends);return 0;}
'''

with tempfile.TemporaryDirectory(prefix="reading-journal-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    (work / "esp_heap_caps.h").write_text("#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\nvoid *heap_caps_calloc(size_t,size_t,int);\n")
    (work / "book_store.h").write_text("#pragma once\n#define ESP_OK 0\nint book_store_prepare_data(void);\n")
    (work / "epdiy.h").write_text("#pragma once\n#include <stdbool.h>\nbool epd_flash_begin(void);\nvoid epd_flash_end(void);\n")
    (work / "esp_timer.h").write_text("#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    (work / "device_power.h").write_text("#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef struct {bool time_synced;uint32_t unix_sec;} pmu_snapshot_t;\nconst pmu_snapshot_t *device_power_get(void);\n")
    source, binary = work / "test.c", work / "test"
    source.write_text(UNIT)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined",
                              '-DREADING_JOURNAL_STORAGE="' + str(work / "journal") + '"',
                              "-I" + str(work), "-I" + str(ROOT / "main/services"), str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
