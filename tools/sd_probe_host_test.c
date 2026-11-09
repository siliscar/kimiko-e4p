/*
 * 首次无卡启动后插卡应可挂载；拔出已挂载卡仍须显式重挂。
 * A first insertion after empty boot mounts; removal of a mounted card still requires explicit remount.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "freertos/task.h"

static bool test_present, test_exfat, test_raw_error;
static bool detect_failure;
static int mount_calls, unmount_calls, test_fail_above_khz;
static esp_err_t test_mount_error = ESP_ERR_TIMEOUT;
static int clock_history[64], delays;
static uint64_t test_total=1024*512,test_free=768*512;
static esp_err_t test_info_error=ESP_OK;
static unsigned space_calls, invalidations;
static bool fat_locked;
static bool log_available, log_gate_blocked;
bool device_power_batt_log_storage(bool available, uint32_t timeout_ms) {
    assert(timeout_ms == 2000);
    if (log_gate_blocked) return false;
    log_available = available;
    return true;
}
bool test_dma_internal = true;
static int64_t mock_us;

int64_t esp_timer_get_time(void) { return mock_us; }

static sdmmc_card_t test_card = {
    .cid.name = "TEST64G", .csd.capacity = 1024, .csd.sector_size = 512,
};

#define mkdir(path, mode) (0)
#include "../components/board_support/board_sd.c"
#undef mkdir

static FATFS test_fat={.free_clst=1};
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *c){assert(c==&test_card);return 0;}
int f_opendir(FF_DIR *root,const char *path){assert(!strcmp(path,"0:"));root->obj.fs=&test_fat;return FR_OK;}
int f_closedir(FF_DIR *root){assert(root->obj.fs==&test_fat);return FR_OK;}
int ff_mutex_take(int drive){assert(drive==0&&!fat_locked);fat_locked=true;return 1;}
void ff_mutex_give(int drive){assert(drive==0&&fat_locked&&test_fat.free_clst==UINT32_MAX);fat_locked=false;++invalidations;}
esp_err_t test_host_init(void){return ESP_OK;}
esp_err_t test_host_deinit(void){return ESP_OK;}
esp_err_t sdmmc_host_init(void){return ESP_OK;}
esp_err_t sdmmc_host_deinit(void){return ESP_OK;}
bool sdmmc_host_check_buffer_alignment(int slot,const void *buffer,size_t size){
    (void)slot;
    return ((uintptr_t)buffer % 64) == 0 && size % 4 == 0;
}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){(void)slot;(void)config;return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *out){(void)host;*out=test_card;return test_raw_error?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *c,void *buffer,size_t start,size_t count){(void)c;(void)start;assert(count==1);if(test_raw_error)return ESP_ERR_TIMEOUT;memset(buffer,0x5a,512);if(test_exfat)memcpy((char*)buffer+3,"EXFAT   ",8);return ESP_OK;}
bool board_sd_present(void) { return test_present; }
// 宿主按「有 CD 脚」跑：软件卡检测的任务与 CMD13 不进入这些用例。
// The host pretends the board has a CD pin, so the soft-detect task and CMD13 stay out of these cases.
bool board_sd_has_cd(void) { return true; }
esp_err_t sdmmc_get_status(sdmmc_card_t *c) { (void)c; return ESP_OK; }
esp_err_t board_sd_detect(bool* present) {
    if (detect_failure) return ESP_ERR_TIMEOUT;
    *present = test_present;
    return ESP_OK;
}
const char* esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
BaseType_t xTaskCreate(void (*fn)(void*), const char* name, int stack,
                       void* arg, int priority, void* handle) {
    (void)name; (void)stack; (void)priority; (void)handle;
    fn(arg);
    return pdPASS;
}
void vTaskDelay(int ticks) { assert(ticks == 250); ++delays; }
void vTaskDelete(void* handle) { (void)handle; }
esp_err_t esp_vfs_fat_sdmmc_mount(const char* path, const sdmmc_host_t* host,
                                  const sdmmc_slot_config_t* slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t* config,
                                  sdmmc_card_t** out) {
    (void)path; (void)slot; (void)config;
    assert(host->dma_aligned_buffer && (uintptr_t)host->dma_aligned_buffer % 64 == 0);
    assert(host->unaligned_multi_block_rw_max_chunk_size == 8);
    assert(host->check_buffer_alignment(host->slot,host->dma_aligned_buffer,512));
    assert(!host->check_buffer_alignment(host->slot,(char *)host->dma_aligned_buffer+1,512));
    assert(!host->check_buffer_alignment(host->slot,host->dma_aligned_buffer,511));
    test_dma_internal=false;
    assert(!host->check_buffer_alignment(host->slot,host->dma_aligned_buffer,512));
    test_dma_internal=true;
    assert(mount_calls < 64);
    clock_history[mount_calls++] = host->max_freq_khz;
    assert(test_present);
    assert(!config->format_if_mount_failed);
    if(test_fail_above_khz && host->max_freq_khz > test_fail_above_khz){*out=NULL;return test_mount_error;}
    *out = &test_card;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char* path, sdmmc_card_t* mounted) {
    (void)path;
    assert(!log_available);
    assert(mounted == &test_card);
    ++unmount_calls;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_format(const char* path, sdmmc_card_t* mounted) {
    (void)path; (void)mounted;
    assert(!log_available);
    return ESP_OK;
}
esp_err_t esp_vfs_fat_info(const char* path, uint64_t* total, uint64_t* free_bytes) {
    (void)path;
    ++space_calls;
    assert(!fat_locked);
    // 模拟真实 f_getfree：作废 FSInfo 后扫描，后续直接复用随写入变化的计数。
    // Like f_getfree, unknown FSInfo scans once; later counts follow allocation changes.
    if (test_fat.free_clst==UINT32_MAX) test_fat.free_clst=123;
    *total=test_total;*free_bytes=test_free;
    return test_info_error;
}

int main(void) {
    board_sd_info_t info = {0};
    assert(board_sd_start_probe() == ESP_ERR_NOT_FOUND);
    assert(board_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    assert(!info.present && !info.mounted && mount_calls == 0);

    test_present = true;
    assert(board_sd_get_info(&info) == ESP_ERR_INVALID_STATE);
    assert(info.present && !info.mounted);
    assert(board_sd_start_probe() == ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info) == ESP_OK);
    assert(info.present && info.mounted && mount_calls == 1);
    assert(log_available);
    log_gate_blocked = true;
    assert(close_card() == ESP_ERR_TIMEOUT && unmount_calls == 0);
    assert(board_sd_get_info(&info) == ESP_OK && info.mounted);
    assert(board_sd_format() == ESP_ERR_TIMEOUT && unmount_calls == 0);
    log_gate_blocked = false;

    detect_failure = true;
    assert(board_sd_get_info(&info) == ESP_OK && info.mounted && info.capacity_bytes);
    assert(board_sd_start_probe() == ESP_OK && mount_calls == 1);
    detect_failure = false;

    // 一次 CD 读错不能判掉卡，连续确认才失效。
    // One bad CD read must not drop the card; a sustained absence does.
    test_present = false;
    mock_us += 60000;
    assert(board_sd_get_info(&info) == ESP_OK && info.mounted);
    mock_us += 60000;
    assert(board_sd_get_info(&info) == ESP_OK && info.mounted);
    mock_us += 60000;
    assert(board_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    test_present = true;
    assert(board_sd_start_probe() == ESP_ERR_INVALID_STATE);
    assert(board_sd_remount() == ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info) == ESP_OK);
    assert(info.mounted && mount_calls == 2 && unmount_calls == 1);
    test_card.csd.capacity = 536870912u; // 256 GiB / 512 byte sectors, exceeds 32-bit byte range.
    test_fail_above_khz=10000;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_OK&&info.mounted);
    assert(info.capacity_bytes==256ull*1024*1024*1024);
    assert(mount_calls==6 && delays==3);
    assert(clock_history[2]==40000 && clock_history[3]==40000 && clock_history[4]==20000 && clock_history[5]==10000);

    // CMD6 忙不能终止降速；默认频率跳过高速切换。/ CMD6 busy must fall back; default speed skips HS negotiation.
    test_mount_error=ESP_ERR_INVALID_STATE;
    test_fail_above_khz=20000;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted && mount_calls==9);
    assert(clock_history[6]==40000 && clock_history[7]==40000 && clock_history[8]==20000);

    // 资源错误立即失败，初始化失败不建议格式化。/ Resource errors fail immediately; init failures never suggest format.
    test_mount_error=ESP_ERR_NO_MEM;
    test_fail_above_khz=1;
    int before=mount_calls, previous_delays=delays;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_ERR_NO_MEM && !info.mounted && !info.needs_format);
    assert(mount_calls==before+1 && delays==previous_delays);
    test_mount_error=ESP_ERR_INVALID_STATE;
    before=mount_calls;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.needs_format);
    assert(mount_calls==before+4 && delays==previous_delays+3);
    test_mount_error=ESP_FAIL;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_FAIL && !info.needs_format);

    test_exfat=true;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_FAIL && info.needs_format);
    test_raw_error=true;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_FAIL && !info.needs_format);
    test_raw_error=test_exfat=false;

    // CD 通信未知时仍尝试实际挂载，不能假设拔卡。/ Unknown CD still permits an actual mount, not assumed removal.
    detect_failure=true;
    test_fail_above_khz=0;
    assert(board_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted && info.present);
    detect_failure=false;
    assert(board_sd_get_info(&info)==ESP_OK && info.mounted);
    assert(info.space_valid && info.filesystem_bytes==test_total && info.free_bytes==test_free);
    assert(invalidations>0);
    // 多分区卡只统计挂载分区；256GiB卡不能截断，挂载状态与统计错误独立。
    test_total=128ull*1024*1024*1024; test_free=100ull*1024*1024*1024;
    unsigned before_space=space_calls;
    assert(board_sd_refresh_space()==ESP_OK && space_calls==before_space+1);
    assert(board_sd_get_info(&info)==ESP_OK && info.capacity_bytes==256ull*1024*1024*1024 &&
           info.filesystem_bytes==test_total && info.filesystem_bytes-info.free_bytes==28ull*1024*1024*1024);
    test_free-=512*1024;
    assert(board_sd_refresh_space()==ESP_OK);
    assert(board_sd_get_info(&info)==ESP_OK&&info.free_bytes==test_free);
    test_info_error=ESP_FAIL;
    assert(board_sd_refresh_space()==ESP_FAIL);
    assert(board_sd_get_info(&info)==ESP_OK&&info.mounted&&!info.space_valid&&info.space_error==ESP_FAIL&&info.free_bytes==0);
    test_info_error=ESP_OK;test_free=test_total+1;
    assert(board_sd_refresh_space()==ESP_FAIL);
    assert(board_sd_get_info(&info)==ESP_OK&&!info.space_valid);
    test_free=0;
    assert(board_sd_refresh_space()==ESP_OK);
    assert(board_sd_get_info(&info)==ESP_OK&&info.space_valid&&info.free_bytes==0);
    // 连续确认后才判掉卡；之前空间统计仍走已挂载的卡。
    // The card drops only after sustained confirmation; until then space still reads as mounted.
    test_present=false;before_space=space_calls;
    mock_us+=60000;
    assert(board_sd_refresh_space()==ESP_OK&&space_calls==before_space+1);
    mock_us+=60000;
    assert(board_sd_refresh_space()==ESP_OK&&space_calls==before_space+2);
    mock_us+=60000;
    assert(board_sd_refresh_space()==ESP_ERR_NOT_FOUND&&space_calls==before_space+2);
    assert(board_sd_get_info(&info)==ESP_ERR_NOT_FOUND&&!info.mounted&&!info.space_valid);
    puts("sd_probe: empty boot/insertion, checked CD, bounded settle/fallback, CMD6 busy, OOM, no format suggestion, 256 GiB passed");
    return 0;
}
