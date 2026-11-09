"""Exercise S31 USB teardown with delayed writes and injected failures.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：验证 S31 总线与模拟开关、独占存储卡、失败重试与启动失败收尾。
English: Verify S31 bus/switch setup, exclusive card ownership, retries and failed-start cleanup.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_NO_MEM 4
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(int ms);
const char *esp_err_to_name(int err);
typedef struct {int slot,max_freq_khz;} sdmmc_host_t;
typedef struct {int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags;} sdmmc_slot_config_t;
typedef struct {int marker;} sdmmc_card_t;
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.slot=1})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_24 24
#define GPIO_NUM_25 25
#define GPIO_NUM_36 36
#define GPIO_NUM_NC -1
int sdmmc_host_init(void);
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config);
int sdmmc_host_deinit(void);
int sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card);
#define CONFIG_IDF_TARGET_ESP32S31 1
#define GPIO_MODE_OUTPUT 1
int gpio_reset_pin(int pin);
int gpio_set_direction(int pin,int mode);
int gpio_set_level(int pin,int level);
typedef struct {bool mounted;} board_sd_info_t;
int board_sd_get_info(board_sd_info_t *info);
int board_sd_sync(void);
int board_sd_remount(void);
bool ttf_font_is_builtin(void);
int ttf_font_open_builtin(void);
typedef void *tinyusb_msc_storage_handle_t;
typedef struct {struct {int auto_mount_off;} user_flags;} tinyusb_msc_driver_config_t;
typedef struct {struct {sdmmc_card_t *card;} medium;int mount_point;
struct {bool do_not_format;struct {int max_files;} config;} fat_fs;} tinyusb_msc_storage_config_t;
#define TINYUSB_MSC_STORAGE_MOUNT_USB 1
typedef struct {int port;} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() ((tinyusb_config_t){0})
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,tinyusb_msc_storage_handle_t *handle);
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);
int tinyusb_msc_uninstall_driver(void);
int tinyusb_driver_install(const tinyusb_config_t *config);
int tinyusb_driver_uninstall(void);
bool tud_mounted(void);
void tud_disconnect(void);
bool book_cover_jobs_quiesce(unsigned timeout_ms);
void book_lock_collage_invalidate(void);
bool ui_avatar_quiesce(unsigned timeout_ms);
'''
UNIT = r'''
#include "usb_stub.h"
#include "usb_storage.c"
#include "storage_jobs.c"
#include <stdio.h>
static bool mounted=true,host,driver,msc,connected;
static sdmmc_card_t *borrowed;
static int stage,delete_error,usb_error,msc_error,busy,deletes,remounts;
static int switch_level=1,switch_resets,switch_directions;
static bool cover_ok=true,avatar_ok=true;
static int cover_calls,avatar_calls;
static int failed(int where){return stage==where?ESP_FAIL:ESP_OK;}
void vTaskDelay(int ms){assert(ms>0);}
const char *esp_err_to_name(int e){(void)e;return "test";}
int board_sd_get_info(board_sd_info_t *i){i->mounted=mounted;return ESP_OK;}
int board_sd_sync(void){assert(mounted);mounted=false;return ESP_OK;}
int board_sd_remount(void){assert(!driver&&!borrowed&&!msc&&!host&&switch_level==1);mounted=true;++remounts;return ESP_OK;}
bool ttf_font_is_builtin(void){return true;}
int ttf_font_open_builtin(void){return ESP_OK;}
int sdmmc_host_init(void){assert(!mounted&&!host&&switch_level==0);if(failed(1))return ESP_FAIL;host=true;return ESP_OK;}
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *c){assert(slot==1&&c->width==4&&c->clk==24&&c->cmd==25&&c->d0==20&&c->d1==21&&c->d2==22&&c->d3==23);assert(c->cd==-1&&c->wp==-1&&(c->flags&SDMMC_SLOT_FLAG_INTERNAL_PULLUP));return failed(2);}
int sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c){assert(h->max_freq_khz==20000);c->marker=47;return failed(3);}
int sdmmc_host_deinit(void){assert(!borrowed&&!driver&&!msc);host=false;return ESP_OK;}
int gpio_reset_pin(int p){assert(p==36);++switch_resets;return ESP_OK;}
int gpio_set_direction(int p,int m){assert(p==36&&m==GPIO_MODE_OUTPUT);++switch_directions;return ESP_OK;}
int gpio_set_level(int p,int v){assert(p==36&&(v==0||v==1));assert(!driver&&!borrowed&&!msc&&!host);switch_level=v;return ESP_OK;}
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *c){assert(c->user_flags.auto_mount_off&&!mounted);if(failed(4))return ESP_FAIL;msc=true;return ESP_OK;}
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *c,tinyusb_msc_storage_handle_t *h){
    assert(msc&&c->medium.card&&c->fat_fs.do_not_format);
    if(failed(5))return ESP_FAIL;borrowed=c->medium.card;*h=malloc(1);assert(*h);return ESP_OK;
}
int tinyusb_driver_install(const tinyusb_config_t *c){(void)c;assert(borrowed&&!mounted&&switch_level==0);if(failed(6))return ESP_FAIL;driver=connected=true;return ESP_OK;}
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t h){
    assert(borrowed&&borrowed->marker==47&&!mounted);++deletes;
    if(busy>0){--busy;return ESP_ERR_INVALID_STATE;}if(delete_error)return delete_error;
    free(h);borrowed=NULL;return ESP_OK;
}
int tinyusb_driver_uninstall(void){assert(!borrowed&&!connected);if(usb_error)return usb_error;driver=false;return ESP_OK;}
int tinyusb_msc_uninstall_driver(void){assert(!borrowed&&!driver);if(msc_error)return msc_error;msc=false;return ESP_OK;}
bool tud_mounted(void){return connected;}
void tud_disconnect(void){assert(driver);connected=false;}
bool book_cover_jobs_quiesce(unsigned t){assert(t==2000);++cover_calls;return cover_ok;}
bool ui_avatar_quiesce(unsigned t){assert(t==2000);++avatar_calls;return avatar_ok;}
static int collage_invalidations;
void book_lock_collage_invalidate(void){++collage_invalidations;}
static void stopped(void){assert(mounted&&!usb_storage_active()&&!usb_storage_connected());assert(!s_cleanup_pending&&!s_card&&!s_storage&&!host&&!driver&&!msc&&switch_level==1);}
static void retained(void){assert(!mounted&&usb_storage_active()&&!usb_storage_connected()&&s_cleanup_pending&&s_card&&host&&switch_level==0);assert(usb_storage_start()==ESP_ERR_INVALID_STATE);}
int main(void){
    usb_storage_phy_init();usb_storage_phy_init();assert(!switch_resets);
    cover_ok=false;assert(usb_storage_start()==ESP_ERR_NOT_FINISHED);stopped();assert(cover_calls==1&&!avatar_calls&&!remounts&&!switch_resets);
    cover_ok=true;avatar_ok=false;assert(usb_storage_start()==ESP_ERR_NOT_FINISHED);stopped();assert(cover_calls==2&&avatar_calls==1&&!remounts&&!switch_resets);
    avatar_ok=true;
    assert(usb_storage_stop()==ESP_OK);stopped();
    for(int i=0;i<3;++i){assert(usb_storage_start()==ESP_OK&&usb_storage_connected());busy=3;assert(usb_storage_stop()==ESP_OK);stopped();}
    assert(usb_storage_start()==ESP_OK);delete_error=ESP_FAIL;int before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(borrowed&&remounts==before);
    delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);busy=25;int attempts=deletes;before=remounts;
    assert(usb_storage_stop()==ESP_ERR_INVALID_STATE&&deletes-attempts==20);retained();assert(remounts==before);
    assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);usb_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(!borrowed&&driver&&remounts==before);
    usb_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);msc_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(!borrowed&&!driver&&msc&&remounts==before);
    msc_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    for(stage=1;stage<=6;++stage){assert(usb_storage_start()==ESP_FAIL);stopped();}stage=0;
    stage=6;delete_error=ESP_FAIL;before=remounts;assert(usb_storage_start()==ESP_FAIL);retained();assert(remounts==before);
    stage=delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(collage_invalidations>0);
    assert(switch_resets==1&&switch_directions==1);
    puts("USB S31: 4-bit 20MHz bus, GPIO36 switch, job guards, delayed writes, 3 stop failures, 6 start failures, exclusive remount and retry passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p/'usb_stub.h').write_text(STUB)
    for name in ('esp_err.h','driver/sdmmc_host.h','esp_log.h','driver/gpio.h',
                 'freertos/FreeRTOS.h','freertos/task.h','board_sd.h','sdmmc_cmd.h',
                 'tinyusb.h','tinyusb_default_config.h','tinyusb_msc.h','ttf_font.h',
                 'ui_avatar.h','book_cover_jobs.h','book_lock_collage.h'):
        header=p/name;header.parent.mkdir(parents=True,exist_ok=True);header.write_text('#include "usb_stub.h"\n')
    (p/'test.c').write_text(UNIT)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer',f'-I{p}',f'-I{ROOT / "main/services"}',f'-I{ROOT / "main"}',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
