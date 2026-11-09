#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Mock driver regression for the real SD lifecycle / 实际 SD 生命周期的模拟驱动回归。"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/sd-media-test'
out.mkdir(parents=True, exist_ok=True)
for name in ('driver/sdmmc_host.h', 'esp_log.h', 'esp_vfs_fat.h', 'esp_timer.h',
             'freertos/FreeRTOS.h', 'freertos/task.h', 'board_hardware.h',
             'sdmmc_cmd.h', 'esp_err.h', 'esp_heap_caps.h', 'esp_memory_utils.h',
             'esp_private/esp_cache_private.h'):
    p = out / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('#pragma once\n', encoding='utf-8')
for name in ('ff.h','diskio_sdmmc.h','device_power.h'):
    (out/name).write_text((root/'tools/sd_probe_stubs'/name).read_text())
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_NO_MEM 6
#define CONFIG_IDF_TARGET_ESP32S31 1
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_24 24
#define GPIO_NUM_25 25
#define GPIO_NUM_38 38
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
#define SDMMC_FREQ_HIGHSPEED 40000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.init=mock_host_init,.deinit=mock_host_deinit})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
static esp_err_t mock_host_init(void){return ESP_OK;}
static esp_err_t mock_host_deinit(void){return ESP_OK;}
typedef struct {
    int max_freq_khz,slot;
    esp_err_t (*init)(void),(*deinit)(void);
    void *dma_aligned_buffer;
    bool (*check_buffer_alignment)(int, const void *, size_t);
    size_t unaligned_multi_block_rw_max_chunk_size;
} sdmmc_host_t;
typedef struct { int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files,allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { struct { char name[8]; int mfg_id,oem_id,serial,date; } cid; struct { uint32_t capacity,sector_size; } csd; } sdmmc_card_t;
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static void heap_caps_free(void *p){free(p);}
static void *heap_caps_aligned_alloc(size_t a,size_t n,int caps){(void)a;(void)caps;return malloc(n);}
static bool esp_ptr_internal(const void *p){return p!=NULL;}
static bool sdmmc_host_check_buffer_alignment(int slot,const void *p,size_t size){(void)slot;(void)size;return p!=NULL;}
static esp_err_t esp_cache_get_alignment(int caps,size_t *a){(void)caps;*a=8;return ESP_OK;}
static esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){(void)slot;(void)config;return ESP_OK;}
static esp_err_t sdmmc_host_init(void){return ESP_OK;}
static esp_err_t sdmmc_host_deinit(void){return ESP_OK;}
static esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card);
static esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *buffer,size_t start,size_t count){(void)card;(void)buffer;(void)start;(void)count;return ESP_ERR_TIMEOUT;}
static esp_err_t sdmmc_get_status(sdmmc_card_t *card);
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(pthread_mutex_lock(p)==0)
#define portEXIT_CRITICAL(p) assert(pthread_mutex_unlock(p)==0)
typedef int BaseType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static bool present=true, drop_during_mount=false, unmount_fail=false, detect_fail=false;
static bool has_cd=true, health_started;
static int mounts, unmounts, formats;
static bool made_books, made_fonts, made_pictures;
static sdmmc_card_t mock_card={.cid={.name="MOCK",.mfg_id=0xab,.oem_id=0x1234,.serial=-1,.date=0x0123},.csd={2048,512}};
static void (*pending)(void*);
esp_err_t board_sd_detect(bool* detected);
static void vTaskDelay(int n) {(void)n;}
static void vTaskDelete(void* p) {(void)p;}
static int xTaskCreate(void (*f)(void*), const char* n,int z,void* a,int pr,void* h) {
    (void)z;(void)a;(void)pr;(void)h;
    if(!strcmp(n,"sd_health")){assert(!has_cd);health_started=true;return pdPASS;}
    assert(!pending);pending=f;return pdPASS;
}
static int esp_vfs_fat_info(const char* p,uint64_t* total,uint64_t* freeb) {(void)p;*total=1048576;*freeb=524288;return 0;}
static int esp_vfs_fat_sdmmc_mount(const char* p,const sdmmc_host_t* h,const sdmmc_slot_config_t* s,const esp_vfs_fat_sdmmc_mount_config_t* c,sdmmc_card_t** card) {
    (void)p;(void)h;(void)s;assert(!c->format_if_mount_failed);mounts++;*card=&mock_card;if(drop_during_mount)present=false;return 0;
}
static int esp_vfs_fat_sdcard_unmount(const char* p,sdmmc_card_t* c){(void)p;assert(c==&mock_card);unmounts++;return unmount_fail ? ESP_FAIL : ESP_OK;}
static int esp_vfs_fat_sdcard_format(const char* p,sdmmc_card_t* c){(void)p;(void)c;formats++;return 0;}
static int mock_mkdir(const char* p,int mode){
    (void)mode;
    if(!strcmp(p,"/sdcard/books")) made_books=true;
    if(!strcmp(p,"/sdcard/fonts")) made_fonts=true;
    if(!strcmp(p,"/sdcard/pictures")) made_pictures=true;
    return 0;
}
#define mkdir mock_mkdir
// 板级接口在包含实现文件之前先声明：生成的头文件只占位。
// Declare the board hook before the implementation include; the generated headers are placeholders.
bool board_sd_has_cd(void);
static int64_t mock_us;
bool device_power_batt_log_storage(bool available,uint32_t timeout_ms) {(void)available;assert(timeout_ms==2000);return true;}
static int64_t esp_timer_get_time(void){return mock_us;}
#include "../../components/board_support/board_sd.c"
// 此测试验证拔卡生命周期；空间计数由 sd_probe_host_test 独立覆盖。
// This fixture checks media lifecycle; allocation counting is covered by sd_probe_host_test.
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *c){(void)c;return has_cd?0xff:0;}
int f_opendir(FF_DIR *root,const char *path){(void)root;(void)path;return 1;}
int f_closedir(FF_DIR *root){(void)root;return FR_OK;}
int ff_mutex_take(int drive){(void)drive;return 1;}
void ff_mutex_give(int drive){(void)drive;}
// CD 故障由边界替身注入；当前 S31 无 CD 时由真实 CMD13 检测生命周期。
// Inject CD faults at the board boundary; real CMD13 lifecycle checks cover the S31 without a CD pin.
esp_err_t sdmmc_get_status(sdmmc_card_t *c){(void)c;return present?ESP_OK:ESP_ERR_TIMEOUT;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c){(void)h;(void)c;return present?ESP_OK:ESP_ERR_TIMEOUT;}
bool board_sd_has_cd(void){return has_cd;}
esp_err_t board_sd_detect(bool *detected){
    if(detected==NULL)return ESP_ERR_INVALID_ARG;
    if(detect_fail)return ESP_ERR_TIMEOUT;
    *detected=has_cd?present:true;return ESP_OK;
}
static void finish_probe(void) {assert(pending);void(*f)(void*)=pending;pending=NULL;f(NULL);}
static void* snapshot_reader(void* arg) {
    (void)arg;
    for(int i=0;i<20000;i++) {
        board_sd_info_t info;
        int err=board_sd_get_info(&info);
        if(err==ESP_OK) assert(info.mounted && info.capacity_bytes==1048576 && info.free_bytes==524288);
    }
    return NULL;
}
int main(void) {
    board_sd_info_t info;
    assert(board_sd_detect(NULL)==ESP_ERR_INVALID_ARG);
    bool detected=true;
    detect_fail=true;
    assert(board_sd_detect(&detected)==ESP_ERR_TIMEOUT && detected);
    assert(board_sd_get_info(&info)==ESP_ERR_TIMEOUT && !info.mounted);
    detect_fail=false;
    assert(board_sd_get_info(NULL)==ESP_ERR_INVALID_ARG);
    assert(board_sd_start_probe()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_sync()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted && info.capacity_bytes);
    assert(!strcmp(info.identity,"ab-1234-ffffffff-0123"));
    assert(made_books && made_fonts && made_pictures);
    detect_fail=true;
    for(int i=0;i<20;i++) assert(board_sd_get_info(&info)==ESP_OK && info.mounted && info.capacity_bytes);
    assert(board_sd_start_probe()==ESP_OK && mounts==1 && unmounts==0);
    detect_fail=false;
    present=false;
    // 已挂载时一次 CD 抖动不判掉卡；连续确认才失效。
    // One CD glitch must not drop a mounted card; a sustained absence does.
    mock_us+=60000; assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    mock_us+=60000; assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    mock_us+=60000; assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    assert(!info.mounted && !info.needs_format && !info.capacity_bytes && !info.free_bytes && !info.name[0]);
    assert(unmounts==0 && card==&mock_card);
    present=true;
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && info.present && !info.mounted);
    assert(board_sd_start_probe()==ESP_ERR_INVALID_STATE && mounts==1);
    assert(board_sd_format()==ESP_ERR_INVALID_STATE && formats==0);
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED && unmounts==1);
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted && mounts==2);
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    present=false;
    // 挂载途中拔卡同样要连续确认，单次观测不锁存。
    // Removal during a mount also needs sustained confirmation; one sample must not latch.
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000;
    assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    present=true;
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.needs_format);
    // 真拔卡之后的检测读错不能让探测结果绕过失效锁存。
    // A detection fault after real removal must not let a probe bypass the removal latch.
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    present=false;
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000;
    assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    present=true;
    detect_fail=true;
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.capacity_bytes);
    assert(board_sd_format()==ESP_ERR_INVALID_STATE && formats==0);
    detect_fail=false;
    drop_during_mount=true;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    // 挂载过程中卡掉了同样要连续确认，单次观测不算。
    // A card lost during mount also needs the sustained confirmation; one sample is not enough.
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000; (void)board_sd_get_info(&info);
    mock_us+=60000;
    assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND && !info.mounted);
    present=true;
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE);
    drop_during_mount=false;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK);
    detect_fail=true;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    detect_fail=false;
    assert(board_sd_sync()==ESP_OK);
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.capacity_bytes);
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    unmount_fail=true;
    assert(board_sd_sync()==ESP_FAIL && card==&mock_card);
    assert(board_sd_get_info(&info)==ESP_FAIL && info.mounted);
    unmount_fail=false;
    assert(board_sd_sync()==ESP_OK && card==NULL);
    // USB 导出期间 health tick 必须跳过：没有本机卡可探测，也不能占用忙状态挡住退出后的重挂。
    // While the card is exported to USB the health tick must skip: nothing to probe, and it must
    // not claim the busy state that would refuse the post-USB remount.
    for(int i=0;i<3;i++) sd_health_tick();
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    pthread_t reader;
    assert(pthread_create(&reader,NULL,snapshot_reader,NULL)==0);
    for(int i=0;i<100;i++) {
        assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
        finish_probe();
    }
    assert(pthread_join(reader,NULL)==0);
    // 当前 S31：无卡座 CD，连续 CMD13 失败后失效；插回只恢复在位，须显式重挂。
    // Current S31: repeated CMD13 failures invalidate media; reinsertion restores presence but requires explicit remount.
    assert(board_sd_sync()==ESP_OK);
    has_cd=false;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);finish_probe();
    assert(health_started && board_sd_get_info(&info)==ESP_OK);
    present=false;
    sd_health_tick();assert(!health_absent);
    present=true;sd_health_tick();assert(!health_fails);
    present=false;sd_health_tick();sd_health_tick();assert(health_absent);
    mock_us+=60000;(void)board_sd_get_info(&info);
    mock_us+=60000;(void)board_sd_get_info(&info);
    mock_us+=60000;assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND && !info.mounted);
    present=true;sd_health_tick();assert(!health_absent);
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && info.present && !info.mounted);
    assert(board_sd_start_probe()==ESP_ERR_INVALID_STATE);
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    assert(board_sd_sync()==ESP_OK);
    for(int i=0;i<3;i++)sd_health_tick();
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);finish_probe();
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    assert(formats==0);
    puts("PASS: real SD lifecycle, CD faults, S31 no-CD/CMD13 removal and reinsertion, explicit remount, USB handoff, concurrent snapshots, no autoformat");
}
'''
(out / 'test.c').write_text(source, encoding='utf-8')
subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-variable', '-fsanitize=address,undefined', '-g', '-pthread',
                '-I'+str(out), '-I'+str(root/'components/board_support/include'),
                str(out/'test.c'), '-o', str(out/'test')], check=True)
subprocess.run([str(out/'test')], check=True)
