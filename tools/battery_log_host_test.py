#!/usr/bin/env python3
"""中文：执行真实日志统计、格式与文件输出，覆盖空白、无效读数及写入失败。
English: Execute actual log accounting, formatting and file output across gaps, invalid samples and I/O failures.
"""
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
#include <sys/stat.h>
#include <unistd.h>
#include "battery_log.h"
static int fault;
static int test_fputs(const char *, FILE *);
static int test_fflush(FILE *);
static int test_fsync(int);
static int test_fclose(FILE *);
static int test_rename(const char *, const char *);
#define fputs test_fputs
#define fflush test_fflush
#define fsync test_fsync
#define fclose test_fclose
#define rename test_rename
#include "battery_log.c"
#undef fputs
#undef fflush
#undef fsync
#undef fclose
#undef rename
static int test_fputs(const char *s, FILE *f) { return fault == 1 ? EOF : fputs(s, f); }
static int test_fflush(FILE *f) { int result = fflush(f); return fault == 2 ? EOF : result; }
static int test_fsync(int fd) { return fault == 3 ? -1 : fsync(fd); }
static int test_fclose(FILE *f) { int result = fclose(f); return fault == 4 ? EOF : result; }
static int test_rename(const char *a, const char *b) { return fault == 5 ? -1 : rename(a, b); }

static batt_log_sample_t sample(int64_t us, int ma) {
    return (batt_log_sample_t){.up_us=us, .wall_sec=1793415600+us/1000000,
        .have_v=true, .have_i=true, .have_chg=true, .raw_mv=4000, .ma=ma,
        .comp_mv=4018, .ocv_mv=4017, .curve_permille=599, .disp_permille=600,
        .ic_soc=61, .ic_us=0};
}
static void observe(batt_log_state_t *s, int64_t us, int ma) {
    batt_log_sample_t value=sample(us,ma); batt_log_observe(s,&value);
}
static batt_log_record_t last(batt_log_state_t *s) {
    batt_log_record_t r={0}, next;
    while(batt_log_pop(s,&next))r=next;
    return r;
}
static void accounting(void) {
    batt_log_state_t s={0};
    observe(&s,0,-100); assert(!s.count && !s.covered_us);
    batt_log_set_enabled(&s,true,0,1793415600);
    observe(&s,0,-100);
    for(int i=1;i<=60;i++)observe(&s,i*1000000LL,-100);
    assert(s.covered_us==60000000 && !s.unknown_us && !s.in_ma_us);
    assert(s.out_ma_us==6000000000LL);
    batt_log_record_t r=last(&s);
    assert((r.events&BATT_LOG_SAMPLE) && r.out_ma_us/3600000==1666);
    // 长空白和读取失败不延长容量积分；恢复时不跨越无效端点。
    // Gaps and failed reads never extend capacity integration, including recovery across invalid endpoints.
    observe(&s,180000000,-200);
    assert(s.unknown_us==120000000 && s.covered_us==60000000);
    r=last(&s);assert(r.events&BATT_LOG_GAP);
    batt_log_sample_t bad=sample(181000000,0);bad.have_i=false;
    batt_log_observe(&s,&bad);
    r=last(&s);assert((r.events&BATT_LOG_VALIDITY) && !r.sample.have_i);
    observe(&s,182000000,-100);assert(s.unknown_us==122000000);
    observe(&s,183000000,-100);assert(s.covered_us==61000000);
    batt_log_note(&s,BATT_LOG_LOCK,183000000,1793415783);last(&s);
    for(int i=184;i<=300;i++)observe(&s,i*1000000LL,-13);
    assert(!s.count && s.locked_us==117000000 && s.covered_us==61000000);
    batt_log_note(&s,BATT_LOG_WAKE,301000000,1793415901);
    r=last(&s);assert((r.events&BATT_LOG_WAKE) && !r.locked && r.locked_us==118000000);
    observe(&s,302000000,-100);assert(s.covered_us==61000000);
    observe(&s,303000000,-100);assert(s.covered_us==62000000);
    assert(s.covered_us+s.unknown_us==303000000);
    uint32_t seq=s.seq;
    batt_log_set_enabled(&s,false,303000000,0);r=last(&s);assert(r.events&BATT_LOG_END);
    observe(&s,400000000,-100);assert(!s.count);
    batt_log_set_enabled(&s,true,500000000,0);
    assert(s.session==2 && s.seq>seq && !s.covered_us && !s.unknown_us && !s.out_ma_us);
    observe(&s,500000000,-100);observe(&s,501000000,100);
    assert(s.in_ma_us==25000000 && s.out_ma_us==25000000);
    for(int i=0;i<100;i++)batt_log_note(&s,BATT_LOG_RESTART,502000000+i,0);
    assert(s.count==BATT_LOG_QUEUE_SIZE && s.dropped>0);
    r=last(&s);assert(r.dropped==s.dropped);
    // 先发生保护事件、后发布更早采样时不得重复积分。
    // A protection event followed by an earlier sample must not double-count an interval.
    batt_log_state_t ordered={0};batt_log_set_enabled(&ordered,true,0,0);
    observe(&ordered,0,-100);observe(&ordered,1000000,-100);
    batt_log_note(&ordered,BATT_LOG_UNDERVOLTAGE,2000000,0);
    observe(&ordered,1500000,-100);
    assert(ordered.cursor_us==2000000 && !ordered.previous_valid);
    observe(&ordered,3000000,-100);
    assert(ordered.covered_us==1000000 && ordered.unknown_us==2000000);
}
static void formatting(void) {
    batt_log_state_t s={0};batt_log_set_enabled(&s,true,0,-1);
    batt_log_record_t r=last(&s);
    char line[BATT_LOG_LINE_SIZE];
    assert(batt_log_format(line,sizeof(line),&r,0x1234,1000000,0,0)>0);
    assert(strstr(line,"sample_ms=-1") && strstr(line,"ma=-1") && strstr(line,"valid=0"));
    batt_log_sample_t v=sample(1000000,-13);batt_log_observe(&s,&v);r=last(&s);
    assert(batt_log_format(line,sizeof(line),&r,0x1234,4000000,2,3)>0);
    assert(strstr(line,"sample_ms=1000 age_ms=3000 write_ms=4000"));
    assert(strstr(line,"valid=15 raw=4000 ma=-13") && strstr(line,"ic_age_ms=4000"));
    assert(strstr(line,"io_errors=2 file_missed=3"));
    v.up_us=2000000;v.have_v=v.have_i=false;batt_log_observe(&s,&v);r=last(&s);
    assert(batt_log_format(line,sizeof(line),&r,0,2000000,0,0)>0);
    assert(strstr(line,"raw=-1 ma=-1") && strstr(line,"ocv=-1 curve=-1 disp=-1"));
    assert(batt_log_format(line,20,&r,0,2000000,0,0)==-1);
}
static void read_file(const char *path,char *text,size_t cap) {
    FILE *f=fopen(path,"r");assert(f);size_t n=fread(text,1,cap-1,f);text[n]=0;assert(!fclose(f));
}
static void files(void) {
    const char *path="batt.log", *backup="batt.log.1";
    FILE *f=fopen(path,"w");assert(f);fputs("# old-format\nold-row\n",f);assert(!fclose(f));
    batt_log_file_t w={.header=true};char text[8192];
    assert(batt_log_append(&w,path,backup,4096,1,"fw=1.0.1 elf=abc","fmt=2 seq=1\n"));
    read_file(path,text,sizeof(text));
    assert(strstr(text,"old-row") && strstr(text,"# battlog fmt=2 session=1 fw=1.0.1 elf=abc"));
    assert(batt_log_append(&w,path,backup,4096,1,"fw=1.0.1","fmt=2 seq=2\n"));
    read_file(path,text,sizeof(text));assert(strstr(text,"seq=2"));
    assert(!strstr(strstr(text,"# battlog")+1,"# battlog"));
    assert(batt_log_append(&w,path,backup,1,1,"fw=1.0.1","fmt=2 seq=3\n"));
    read_file(backup,text,sizeof(text));assert(strstr(text,"old-row"));
    read_file(path,text,sizeof(text));assert(strstr(text,"# battlog") && !strstr(text,"old-row"));
    for(int i=1;i<=4;i++) {
        fault=i;assert(!batt_log_append(&w,path,backup,4096,1,"fw=x","fmt=2 failure\n"));
        assert(w.header);fault=0;
        assert(batt_log_append(&w,path,backup,4096,1,"fw=x","fmt=2 recovered\n"));assert(!w.header);
    }
    fault=5;assert(!batt_log_append(&w,path,backup,1,1,"fw=x","fmt=2 x\n"));
    fault=0;assert(!batt_log_append(&w,"missing/batt.log",backup,4096,1,"fw=x","x\n"));
    assert(batt_log_append(&w,path,backup,4096,2,"fw=x","fmt=2 session=2\n"));
    read_file(path,text,sizeof(text));assert(strstr(text,"# battlog fmt=2 session=2"));
}
int main(void) {
    accounting();formatting();files();
    puts("PASS: battery-log sample coverage, invalid values, sign crossings, lock exclusion, events, sessions, overflow, metadata, rotation and write/flush/sync/close failures");
}
'''

with tempfile.TemporaryDirectory(prefix="battery-log-") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    source, binary = work / "test.c", work / "test"
    source.write_text(UNIT)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "components/device_power"),
        str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], cwd=work, env=env, check=True)
