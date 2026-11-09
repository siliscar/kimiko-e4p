/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMMC 探测、挂载、格式化。
 *
 * SDMMC probe, mount, and format.
 */

#include "board_sd.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_hardware.h"
#include "device_power.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_sdmmc.h"

#define SD_MOUNT_POINT "/sdcard"
#if defined(CONFIG_IDF_TARGET_ESP32S31)
// Metalio：SDMMC 4-bit（CLK 24 / CMD 25 / D0-D3 20-23）。
// Metalio: SDMMC 4-bit (CLK 24 / CMD 25 / D0-D3 20-23).
#define SD_PIN_CLK GPIO_NUM_24
#define SD_PIN_CMD GPIO_NUM_25
#define SD_PIN_D0 GPIO_NUM_20
#define SD_PIN_D1 GPIO_NUM_21
#define SD_PIN_D2 GPIO_NUM_22
#define SD_PIN_D3 GPIO_NUM_23
#define SD_BUS_WIDTH 4
#else
#define SD_PIN_CLK GPIO_NUM_38
#define SD_PIN_CMD GPIO_NUM_42
#define SD_PIN_D0 GPIO_NUM_44
#define SD_PIN_D1 GPIO_NUM_NC
#define SD_PIN_D2 GPIO_NUM_NC
#define SD_PIN_D3 GPIO_NUM_NC
#define SD_BUS_WIDTH 1
#endif

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static int probe_state;
static bool filesystem_unreadable;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static board_sd_info_t cached_info;

// 已挂载或正在挂载时，一次「不在位」多半是 I2C 读错或触点抖动：连续确认够久才判掉卡。
// While mounted or mounting, a single "absent" is usually a bad I2C read or contact bounce;
// latch only after a sustained absence.
#define SD_CD_CONFIRM_READS 3
#define SD_CD_CONFIRM_US (120 * 1000)
static unsigned cd_absent_reads;
static int64_t cd_absent_since_us;

// 没有卡座 CD 的板子用 CMD13 当软件卡检测：连续失败才判掉卡，成功一次立刻恢复。
// Boards without a slot CD poll CMD13 as a soft card detect: latch after repeated failures and
// clear on the first success.
#define SD_HEALTH_INTERVAL_MS 2000
#define SD_HEALTH_FAILS 2
static bool health_absent;
static unsigned health_fails;
static bool health_task_started;
static sdmmc_host_t health_host;
static sdmmc_slot_config_t health_slot;
static bool health_host_valid;

// S31 的 SDMMC 中转资格判定：只允许内部 RAM 且满足主机对齐的缓冲直接 DMA。
// Relay eligibility for S31 SDMMC: only aligned internal-RAM buffers may DMA directly.
static bool sd_dma_relay_check(int slot, const void* buffer, size_t size) {
    return esp_ptr_internal(buffer) && sdmmc_host_check_buffer_alignment(slot, buffer, size);
}
static void* sd_dma_relay;

// 失效只影响快照，消费者释放句柄前不得卸载卡。/ Invalidation changes snapshots only; consumers must close handles before unmount.
// 已挂载或正在挂载的卡才需要失效锁存：探测没挂上不算，否则空卡开机后插卡会被锁死。
// Only a mounted or mounting card needs the removal latch; a failed probe must not latch,
// or a card inserted after an empty boot would stay locked out.
static void observe_media_locked(bool present) {
    if (present) {
        cd_absent_reads = 0;
    } else if (cached_info.mounted || probe_state == 1) {
        if (cd_absent_reads == 0) cd_absent_since_us = esp_timer_get_time();
        if (++cd_absent_reads < SD_CD_CONFIRM_READS ||
            esp_timer_get_time() - cd_absent_since_us < SD_CD_CONFIRM_US) return;
    }
    bool inserted = present && !cached_info.present;
    if (!present && (cached_info.mounted || probe_state == 1)) media_invalidated = true;

    // 首次开机无卡时没有文件句柄；后插卡可重新探测。
    // A card inserted after an empty boot has no stale handles and can be probed.
    if (inserted && probe_state == 2 && !media_invalidated && !cached_info.mounted) {
        probe_state = 0;
        cached_info.error = ESP_ERR_INVALID_STATE;
    }
    cached_info.present = present;
    if (!present || media_invalidated) {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = present;
        cached_info.error = present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
}

// 卷锁内发 CMD13，和文件读写串行；锁被占或超时就不判断，绝不阻塞调用方以外的东西。
// Issue CMD13 under the volume lock so it serializes with file I/O; when the lock is busy or
// times out, judge nothing rather than stall anyone.
// 卡重新插入后是未初始化状态，旧 RCA 的 CMD13 必然超时：判掉卡之后改用一次轻量初始化确认它回来了。
// A reinserted card is uninitialized, so CMD13 on the old RCA always times out; once the card is
// considered gone, confirm its return with one lightweight card init instead.
// 探测期间借用 probe_state 占住忙状态，重挂和探测不会同时动主机。
// The probe claims probe_state while it runs, so a remount and a probe never touch the host together.
static void sd_health_tick(void) {
    sdmmc_card_t* probe = NULL;
    portENTER_CRITICAL(&state_lock);
    // 卡已交给 USB 导出（sync 后 card 为空）或挂载失败时不得占用忙状态，否则提前返回会把它永远卡在 1，退出 USB 后重挂就被拒。
    // Never claim the busy state when there is no local card (exported to USB after sync, or a failed
    // mount): the early return would leave probe_state stuck at 1 and refuse the post-USB remount.
    if (probe_state == 2 && card) { probe = card; probe_state = 1; }
    portEXIT_CRITICAL(&state_lock);
    if (probe == NULL) { health_fails = 0; return; }
    BYTE pdrv = ff_diskio_get_pdrv_card(probe);
    if (pdrv != 0xff) {
        bool locked = true;
#if FF_FS_REENTRANT
        locked = ff_mutex_take(pdrv) != 0;
#endif
        if (locked) {
            esp_err_t err = sdmmc_get_status(probe);
            if (err != ESP_OK && health_absent && health_host_valid) {
                // 拔卡会让 SDMMC 主机停在失败状态：先重开主机再做轻量初始化，否则新插入的卡也问不到。
                // A yank leaves the SDMMC host in a failed state: reopen it before the light init, or a
                // freshly inserted card cannot answer either.
                (void)sdmmc_host_deinit();
                if (sdmmc_host_init() == ESP_OK &&
                    sdmmc_host_init_slot(health_host.slot, &health_slot) == ESP_OK) {
                    sdmmc_card_t scratch;
                    err = sdmmc_card_init(&health_host, &scratch);
                }
            }
#if FF_FS_REENTRANT
            ff_mutex_give(pdrv);
#endif
            if (err == ESP_OK) {
                health_fails = 0;
                health_absent = false;
            } else if (++health_fails >= SD_HEALTH_FAILS) {
                health_absent = true;
            }
        }
    }
    portENTER_CRITICAL(&state_lock);
    if (probe_state == 1) probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
}

static void sd_health_task(void* arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SD_HEALTH_INTERVAL_MS));
        sd_health_tick();
    }
}

// 只在没有 CD 脚的板子上起一次；有真实卡座的板子继续走中断式的 checked detect。
// Start once on boards without a CD pin; boards with a real slot keep using checked detect.
static void start_health_task(void) {
    if (health_task_started || board_sd_has_cd()) return;
    if (xTaskCreate(sd_health_task, "sd_health", 3072, NULL, 2, NULL) == pdPASS)
        health_task_started = true;
}

static void publish_info(const board_sd_info_t* info) {
    bool present = false;
    esp_err_t detect_err = board_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    cached_info = *info;
    // 已经确认的拔卡失效不会被后续通信错误或探测成功覆盖。
    // A confirmed removal latch survives later detection faults and probe success.
    if (detect_err == ESP_OK || media_invalidated)
        observe_media_locked(detect_err == ESP_OK ? present : cached_info.present);
    probe_state = 2;
    const bool log_available = cached_info.mounted && !media_invalidated && info->error == ESP_OK;
    portEXIT_CRITICAL(&state_lock);
    (void)device_power_batt_log_storage(log_available, 2000);
}

static esp_err_t fill_space(board_sd_info_t *info) {
    uint64_t total = 0, available = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &available);
    // 出错或损坏的计数不等于「卡已满」，也不能发生无符号减法下溢。
    // Failed/corrupt counters never mean a full card, and must not underflow subtraction.
    if (err == ESP_OK && (!total || available > total || total > info->capacity_bytes)) err = ESP_FAIL;
    info->space_valid = err == ESP_OK;
    info->space_error = err;
    info->filesystem_bytes = err == ESP_OK ? total : 0;
    info->free_bytes = err == ESP_OK ? available : 0;
    return err;
}

// FAT32 的 FSInfo 可能仍是电脑上旧的计数。每次挂载仅一次作废它，首轮统计只读扫描 FAT。
// Invalidate possibly stale FAT32 FSInfo once per mount; the first read-only FAT scan then counts allocations.
static void invalidate_mount_space(void) {
    BYTE drive = ff_diskio_get_pdrv_card(card);
    if (drive == 0xff) return;
    char path[12]; snprintf(path,sizeof(path),"%u:",(unsigned)drive);
    FF_DIR root;
    if (f_opendir(&root,path) != FR_OK) return;
    FATFS *fs = root.obj.fs;
    (void)f_closedir(&root);
    if (!fs) return;
#if FF_FS_REENTRANT
    if (!ff_mutex_take(fs->ldrv)) return;
#endif
    fs->free_clst = UINT32_MAX;
#if FF_FS_REENTRANT
    ff_mutex_give(fs->ldrv);
#endif
}

static void fill_info(board_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    bool present = false;
    esp_err_t detect_err = board_sd_detect(&present);
    // 成功挂载是有卡的证据；检测通信失败不覆盖挂载结果。
    // A successful mount proves presence; failed detection must not override it.
    info->present = detect_err == ESP_OK ? present : mount_err != ESP_ERR_NOT_FOUND;
    info->error = mount_err;
    // 挂载错误本身不证明文件系统问题；仅只读识别到 exFAT 或空白卡才提示。
    // Mount errors alone are inconclusive; only read-only confirmation of exFAT/blank media enables the hint.
    info->needs_format = info->present && filesystem_unreadable;
    if (mount_err != ESP_OK || card == NULL) return;

    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    snprintf(info->identity, sizeof(info->identity), "%02x-%04x-%08" PRIx32 "-%04x", (unsigned)card->cid.mfg_id,
             (unsigned)card->cid.oem_id, (uint32_t)card->cid.serial, (unsigned)card->cid.date);
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    // 空间统计失败不覆盖「挂载成功」，仍允许正常读取文件。
    // Allocation-query failure must not turn a mounted filesystem into a mount failure.
    (void)fill_space(info);
}

static void ensure_media_dirs(void) {
    if (mkdir("/sdcard/assets", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets: %d", errno);
    }
    if (mkdir("/sdcard/assets/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets/fonts: %d", errno);
    }
    if (mkdir("/sdcard/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir fonts: %d", errno);
    }
    if (mkdir("/sdcard/books", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir books: %d", errno);
    }
    if (mkdir("/sdcard/pictures", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir pictures: %d", errno);
    }
}

// 只识别明确不支持的文件系统，不写卡，也不根据通用 ESP_FAIL 猜测。
// Identify known unsupported media without writing or interpreting generic ESP_FAIL as a filesystem error.
static bool unsupported_boot_sector(const uint8_t *sector) {
    if (!memcmp(sector + 3, "EXFAT   ", 8)) return true;
    for (unsigned i = 0; i < 512; ++i) if (sector[i]) return false;
    return true;
}
static bool probe_unsupported_filesystem(sdmmc_host_t host, const sdmmc_slot_config_t *slot) {
    sdmmc_card_t *probe = calloc(1, sizeof(*probe));
    uint8_t *sector = heap_caps_malloc(512, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    bool owned = false, unsupported = false;
    if (!probe || !sector) goto done;
    host.max_freq_khz = 10000;
    if (host.init() != ESP_OK) goto done; // 不接管其他已初始化会话。/ Never take over another initialized session.
    owned = true;
    if (sdmmc_host_init_slot(host.slot, slot) != ESP_OK || sdmmc_card_init(&host, probe) != ESP_OK ||
        probe->csd.sector_size != 512 || sdmmc_read_sectors(probe, sector, 0, 1) != ESP_OK) goto done;
    unsupported = unsupported_boot_sector(sector);
    if (!unsupported && sector[510] == 0x55 && sector[511] == 0xaa) {
        uint32_t starts[4] = {0};
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *entry = sector + 446 + 16 * i;
            if (!entry[4]) continue;
            starts[i] = (uint32_t)entry[8] | (uint32_t)entry[9] << 8 | (uint32_t)entry[10] << 16 | (uint32_t)entry[11] << 24;
        }
        for (unsigned i = 0; i < 4 && !unsupported; ++i) {
            if (!starts[i] || starts[i] >= probe->csd.capacity) continue;
            if (sdmmc_read_sectors(probe, sector, starts[i], 1) != ESP_OK) goto done;
            // 分区必须明确标识 exFAT；普通 FAT 或未知损坏不建议格式化。
            // A partition must explicitly identify exFAT; do not suggest formatting FAT or unknown corruption.
            unsupported = !memcmp(sector + 3, "EXFAT   ", 8);
        }
    }
done:
    if (owned) host.deinit();
    free(probe); heap_caps_free(sector);
    return unsupported;
}

static esp_err_t mount_card(bool format_if_failed) {
    filesystem_unreadable = false;
    if (card != NULL) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // S31 走 4-bit，40 MHz 有足够带宽；S3 原板 1-bit 只能靠提时钟换带宽。
    // S31 uses 4-bit at 40 MHz; the original S3 board had to trade clock for 1-bit bandwidth.
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // S31 的 SDMMC 拒绝 PSRAM 直接 DMA：准备常驻 4 KiB 内部中转缓冲，SDK 按 8 扇区
    // 分块搬运，隔离外部 DMA/cache 路径，也省去每次写入的临时 DMA 分配（对齐厂商
    // PrepareDmaBuffer）。分配失败同样判挂载失败；常驻不释放是因为 health_host 会
    // 长期复用这份主机配置做软检测探测，释放会留下悬空指针。
    // S31 SDMMC refuses direct PSRAM DMA: stage through a resident 4 KiB internal buffer
    // in 8-sector chunks, isolating the external DMA/cache path and removing the per-write
    // scratch allocation (mirrors the vendor PrepareDmaBuffer). Like the vendor, allocation
    // failure fails the mount; the buffer stays resident because health_host keeps reusing
    // this host copy for soft-detect probes, and freeing it would leave a dangling pointer.
    if (!sd_dma_relay) {
        size_t alignment = 0;
        if (esp_cache_get_alignment(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL, &alignment) != ESP_OK || alignment < 4)
            alignment = 4;
        sd_dma_relay = heap_caps_aligned_alloc(alignment, 4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!sd_dma_relay) {
            ESP_LOGE(TAG, "SD transfer buffer alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }
    host.dma_aligned_buffer = sd_dma_relay;
    host.check_buffer_alignment = sd_dma_relay_check;
    host.unaligned_multi_block_rw_max_chunk_size = 4096 / 512;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = SD_BUS_WIDTH;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.d1 = SD_PIN_D1;
    slot.d2 = SD_PIN_D2;
    slot.d3 = SD_PIN_D3;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    // 冷启动先等待并重试高速，再降速；CMD6 忙也可能返回 INVALID_STATE，不得直接放弃。
    // Settle/retry high speed before lowering clocks; CMD6 busy may return INVALID_STATE and must retry.
    const int clocks[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_HIGHSPEED, 20000, 10000};
    esp_err_t err = ESP_FAIL;
    for (size_t attempt = 0; attempt < sizeof(clocks) / sizeof(clocks[0]); ++attempt) {
        bool present = false;
        esp_err_t detect_err = board_sd_detect(&present);
        if (detect_err == ESP_OK && !present) return ESP_ERR_NOT_FOUND;
        if (detect_err != ESP_OK) ESP_LOGW(TAG, "SD detect unavailable: %s; try card I/O", esp_err_to_name(detect_err));
        host.max_freq_khz = clocks[attempt];
        card = NULL;
        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
        if (err == ESP_OK) break;
        if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG) break;
        ESP_LOGW(TAG, "SD probe %u at %d kHz failed: %s", (unsigned)attempt + 1,
                 clocks[attempt], esp_err_to_name(err));
        card = NULL;
        // VFS mount 在失败时只清理自己建立的 host；不额外 deinit 别的存储会话。
        // Failed VFS mount cleans up its own host; never forcibly deinit another storage session.
        if (attempt + 1 < sizeof(clocks) / sizeof(clocks[0])) vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
        if (!format_if_failed && err == ESP_FAIL) filesystem_unreadable = probe_unsupported_filesystem(host, &slot);
    } else {
        invalidate_mount_space();
        ensure_media_dirs();
        // 记下挂载用的主机配置，软件卡检测靠它做「卡回来了」的轻量初始化。
        // Remember the mount host so the soft detect can probe a returning card with a light init.
        health_host = host;
        health_slot = slot;
        health_host_valid = true;
    }
    return err;
}

static esp_err_t close_card(void) {
    if (!device_power_batt_log_storage(false, 2000)) return ESP_ERR_TIMEOUT;
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
            return err;
        }
        card = NULL;
    }
    return ESP_OK;
}

static void probe_task(void* arg) {
    (void)arg;
    board_sd_info_t info = { 0 };
    esp_err_t detect_err = board_sd_detect(&info.present);
    if (detect_err == ESP_OK && !info.present) {
        info.error = ESP_ERR_NOT_FOUND;
        publish_info(&info);
        ESP_LOGI(TAG, "SD_CD absent, skip mount");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = mount_card(false);
    fill_info(&info, err);
    if (err == ESP_OK) {
        // 刚挂上的卡一定在位，清掉上一轮软件检测留下的判断再起后台探测。
        // A freshly mounted card is present: clear the previous soft-detect verdict before the
        // background probe starts.
        health_absent = false;
        health_fails = 0;
        start_health_task();
        ESP_LOGI(
            TAG,
            "Mounted %s, capacity=%llu MB, free=%llu MB",
            info.name,
            (unsigned long long)(info.capacity_bytes / (1024 * 1024)),
            (unsigned long long)(info.free_bytes / (1024 * 1024))
        );
    }
    publish_info(&info);
    vTaskDelete(NULL);
}

esp_err_t board_sd_start_probe(void) {
    bool present = true;
    esp_err_t detect_err = board_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detect_err == ESP_OK) observe_media_locked(present);
    esp_err_t err = ESP_OK;
    if (probe_state == 1) err = ESP_ERR_NOT_FINISHED;
    else if (media_invalidated) err = cached_info.error;
    else if (probe_state == 2) err = cached_info.mounted ? ESP_OK :
        cached_info.error != ESP_OK ? cached_info.error : ESP_FAIL;
    else if (!present) {
        probe_state = 2;
        err = ESP_ERR_NOT_FOUND;
    } else {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = true;
        cached_info.error = ESP_ERR_NOT_FINISHED;
        probe_state = 1;
        cd_absent_reads = 0;
    }
    bool start = err == ESP_OK && probe_state == 1;
    portEXIT_CRITICAL(&state_lock);
    if (!start) return err;
    BaseType_t created = xTaskCreate(
        probe_task, "sd_probe", 4096, NULL, 5, NULL
    );
    if (created != pdPASS) {
        portENTER_CRITICAL(&state_lock);
        probe_state = 0;
        cached_info.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t board_sd_get_info(board_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    bool present = false;
    esp_err_t detect_err = board_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    // 没有 CD 脚的板子把软件检测结果并进在位判断，后续去抖与锁存完全复用同一条路径。
    // On boards without a CD pin, fold the soft-detect result into presence so the existing
    // debounce and latch path stays the only one.
    if (detect_err == ESP_OK && present && health_absent) present = false;
    if (detect_err == ESP_OK) observe_media_locked(present);
    *info = cached_info;
    esp_err_t err = probe_state == 0 && detect_err != ESP_OK ? detect_err :
        !info->present || media_invalidated ? info->error :
        probe_state == 0 ? ESP_ERR_INVALID_STATE :
        probe_state == 1 ? ESP_ERR_NOT_FINISHED : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

// 驱动操作期间占用忙状态，不在临界区执行 I/O。/ Reserve busy state across driver I/O outside the critical section.
static bool begin_operation(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = probe_state != 1;
    if (ready) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

esp_err_t board_sd_refresh_space(void) {
    board_sd_info_t info;
    esp_err_t err = board_sd_get_info(&info);
    if (err != ESP_OK || !info.mounted) return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    err = fill_space(&info);
    publish_info(&info);
    return err;
}

esp_err_t board_sd_remount(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        board_sd_info_t info = { .present = true, .error = err };
        (void)board_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    portENTER_CRITICAL(&state_lock);
    memset(&cached_info, 0, sizeof(cached_info));
    media_invalidated = false;
    probe_state = 0;
    cd_absent_reads = 0;
    portEXIT_CRITICAL(&state_lock);
    return board_sd_start_probe();
}

esp_err_t board_sd_sync(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        board_sd_info_t info = { .present = true, .mounted = true, .error = err };
        (void)board_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    board_sd_info_t info = { .error = ESP_ERR_INVALID_STATE };
    publish_info(&info);
    return ESP_OK;
}

esp_err_t board_sd_format(void) {
    board_sd_info_t current;
    (void)board_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    bool busy = probe_state == 1;
    if (!invalid && !busy) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    if (busy) return ESP_ERR_NOT_FINISHED;
    if (!device_power_batt_log_storage(false, 2000)) {
        publish_info(&current);
        return ESP_ERR_TIMEOUT;
    }
    bool present = false;
    if (board_sd_detect(&present) == ESP_OK && !present) {
        board_sd_info_t info = { .error = ESP_ERR_NOT_FOUND };
        publish_info(&info);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    if (card == NULL) {
        err = mount_card(true);
    } else {
        err = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "format %s", esp_err_to_name(err));
        }
    }

    board_sd_info_t info = { 0 };
    fill_info(&info, err);
    if (err == ESP_OK) {
        ensure_media_dirs();
        fill_info(&info, ESP_OK);
        ESP_LOGI(TAG, "formatted %s", info.name);
    }
    publish_info(&info);
    return err;
}
