#!/usr/bin/env python3
"""中文：首页双区域偏好的真实NVS模块回归，不访问设备。
English: Regress the real two-region preference module against transactional NVS doubles.
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
#include <string.h>
#include "nvs.h"
static uint8_t disk,pending;
static bool disk_present,staged,flash_allowed=true,open_allowed=true,get_allowed=true,set_allowed=true,commit_allowed=true;
static int reads,writes,commits,opens,closes,begins,ends,mode;
bool epd_flash_begin(void){++begins;return flash_allowed;}
void epd_flash_end(void){++ends;}
esp_err_t nvs_open(const char *name,int access,nvs_handle_t *handle){
    assert(!strcmp(name,"read_pico"));++opens;
    if(!open_allowed)return 1;
    *handle=42;mode=access;staged=false;return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value){
    assert(handle==42&&mode==NVS_READONLY&&!strcmp(key,"home_regions"));++reads;
    if(!disk_present||!get_allowed)return 1;
    *value=disk;return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value){
    assert(handle==42&&mode==NVS_READWRITE&&!strcmp(key,"home_regions"));++writes;
    if(!set_allowed)return 1;
    pending=value;staged=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle){
    assert(handle==42&&mode==NVS_READWRITE&&staged);++commits;
    if(!commit_allowed)return 1;
    disk=pending;disk_present=true;return ESP_OK;
}
void nvs_close(nvs_handle_t handle){assert(handle==42);staged=false;++closes;}
'''
UNIT += '\n#include "' + str(ROOT / "main/settings/home_components.c") + '"\n'
UNIT += r'''
static void reboot(bool legacy){
    s_loaded=false;s_slots[0]=s_slots[1]=APP_HOME_WIDGET_EMPTY;
    app_home_components_init(legacy);
}
static void choice(app_home_widget_t top,app_home_widget_t bottom){
    assert(app_home_component_at(APP_HOME_SLOT_TOP)==top);
    assert(app_home_component_at(APP_HOME_SLOT_BOTTOM)==bottom);
    assert(app_home_components_value_valid(app_home_components_value()));
}
static void select_once(unsigned slot,app_home_widget_t widget,app_home_widget_t top,app_home_widget_t bottom){
    int old_writes=writes,old_commits=commits;
    assert(app_home_component_set(slot,widget));choice(top,bottom);
    assert(writes==old_writes+1&&commits==old_commits+1);
    reboot(false);choice(top,bottom);
}
int main(void){
    reboot(false);choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);
    assert(reads==1&&!writes&&!begins&&!ends);
    app_home_components_init(true);choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);assert(reads==1);
    assert(app_home_has_widget(APP_HOME_WIDGET_READING)&&app_home_has_widget(APP_HOME_WIDGET_EMPTY));
    assert(!app_home_has_widget(APP_HOME_WIDGET_WEATHER));
    assert(app_home_component_at(2)==APP_HOME_WIDGET_EMPTY&&app_home_component_at(UINT32_MAX)==APP_HOME_WIDGET_EMPTY);
    assert(!app_home_component_set(2,APP_HOME_WIDGET_READING));
    assert(!app_home_component_set(0,(app_home_widget_t)-1));
    assert(!app_home_component_set(0,(app_home_widget_t)4));assert(!writes&&!begins);
    assert(app_home_component_set(0,APP_HOME_WIDGET_READING)&&!writes);
    reboot(true);choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER);assert(!writes&&!begins);
    select_once(0,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING);
    assert(disk==0x11);
    select_once(1,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER);
    select_once(0,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_WEATHER);
    select_once(0,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_EMPTY);
    select_once(1,APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING);
    uint8_t saved=disk;
    int old_writes=writes,old_commits=commits,old_begins=begins,old_ends=ends,old_opens=opens;
    flash_allowed=false;assert(!app_home_component_set(0,APP_HOME_WIDGET_READING));flash_allowed=true;
    choice(APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING);
    assert(writes==old_writes&&commits==old_commits&&opens==old_opens&&begins==old_begins+1&&ends==old_ends);
    open_allowed=false;assert(!app_home_component_set(0,APP_HOME_WIDGET_READING));open_allowed=true;
    assert(writes==old_writes&&commits==old_commits&&begins==old_begins+2&&ends==old_ends+1);
    set_allowed=false;assert(!app_home_component_set(0,APP_HOME_WIDGET_READING));set_allowed=true;
    assert(writes==old_writes+1&&commits==old_commits);
    commit_allowed=false;assert(!app_home_component_set(0,APP_HOME_WIDGET_READING));commit_allowed=true;
    choice(APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING);assert(disk==saved);
    reboot(false);choice(APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_READING);
    select_once(0,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_READING);
    select_once(1,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_EMPTY);
    assert(!app_home_has_widget(APP_HOME_WIDGET_WEATHER));
    select_once(0,APP_HOME_WIDGET_READING,APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);
    select_once(1,APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_READING);
    int before=writes,valid_count=0;
    for(unsigned value=0;value<256;++value){
        disk=(uint8_t)value;
        bool valid=value>=0x10&&value<=0x1f&&
            ((value&3)!=((value>>2)&3)||(value&3)==2);
        assert(app_home_components_value_valid((uint8_t)value)==valid);
        reboot(true);
        if(valid){++valid_count;assert(app_home_components_value()==value);}
        else choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER);
        assert(writes==before);
    }
    assert(valid_count==13);
    app_home_components_apply(0x18);
    select_once(0,APP_HOME_WIDGET_JOURNAL,APP_HOME_WIDGET_JOURNAL,APP_HOME_WIDGET_EMPTY);
    select_once(1,APP_HOME_WIDGET_JOURNAL,APP_HOME_WIDGET_EMPTY,APP_HOME_WIDGET_JOURNAL);
    open_allowed=false;reboot(false);open_allowed=true;choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);
    get_allowed=false;reboot(true);get_allowed=true;choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER);
    int before_apply=writes;
    app_home_components_apply(0x18);choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);assert(writes==before_apply);
    app_home_components_apply(0x10);choice(APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY);assert(writes==before_apply);
    puts("PASS: home slot legacy migration, atomic unique-widget swaps, double Empty, reboot, all 256 encoded values and failed-commit rollback");
}
'''
with tempfile.TemporaryDirectory(prefix="home-components-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    (work / "nvs.h").write_text("""#pragma once
#include <stdint.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t,const char *,uint8_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
""")
    (work / "epdiy.h").write_text("#pragma once\n#include <stdbool.h>\nbool epd_flash_begin(void);\nvoid epd_flash_end(void);\n")
    source, binary = work / "test.c", work / "test"
    source.write_text(UNIT)
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                              "-I" + str(work), str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
