/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：浅睡唤醒后恢复 CACHE_L1 配置。浅睡（CPU retention 路径）唤醒过程
 * 会把 0x2C000000 一组 CACHE_L1 寄存器清零而 IDF 的 spi_flash_restore_cache()
 * 不恢复它们：L1_ICACHE_CTRL / L1_DCACHE_CTRL 的 undef_op 位域（[15:8]，
 * bits[1:0] 总线仲裁模式、bit[5] 自动时钟门控）从随机仲裁被清成「独占总线
 * 直到取数完成」，双核并发逐行预取的 EPD 喂行产能掉约 20%，20 MHz 扫描
 * 第一刷欠载后 display.c 把像素钟永久降到 12 MHz。ACS_FAIL_CTRL/INT_ENA
 * 被清则关闭访问失败中断。这里在首次浅睡前抓快照、每次浅睡返回后写回
 * 四个稳定配置字；PRELOAD_ADDR/SIZE 与 INT_RAW 是操作/状态类寄存器，
 * 每次预取都会重写，不写回。
 *
 * English: restore the CACHE_L1 configuration after a light-sleep wake. The
 * wake path (CPU retention) clears a group of CACHE_L1 registers at
 * 0x2C000000 that IDF's spi_flash_restore_cache() never puts back: the
 * undef_op field of L1_ICACHE_CTRL / L1_DCACHE_CTRL ([15:8]; bits[1:0] bus
 * arbitration mode, bit[5] auto clock gating) drops from random arbitration
 * to hold-bus-until-complete, cutting the two-core per-line-preload EPD feed
 * throughput by ~20%, so the first 20 MHz scan underruns and display.c pins
 * the pixel clock at 12 MHz for good. The cleared ACS_FAIL_CTRL/INT_ENA also
 * silence access-failure interrupts. This module snapshots the words before
 * the first light sleep and writes the four stable configuration words back
 * after every wake; PRELOAD_ADDR/SIZE and INT_RAW are operation/status
 * registers rewritten by each preload and are left alone.
 *
 * 冻结：只写回浅睡前的稳定配置快照，不改任何 cache 行为参数；快照必须
 * 在清醒态抓取。
 * Frozen: only write back the pre-sleep stable configuration snapshot; never
 * change cache behavior parameters. The snapshot must be taken while awake.
 */

#include "cache_wake_restore.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_IDF_TARGET_ESP32S31)
#include "soc/cache_reg.h"

static const char *TAG = "cache_fix";

// 稳定配置字的偏移；值在首次浅睡前的清醒态抓取。
// Offsets of the stable configuration words; values are snapshotted awake
// before the first light sleep.
typedef struct {
    size_t offset;
    uint32_t value;
} cache_word_t;

static cache_word_t s_words[] = {
    {0x00, 0}, // L1_ICACHE_CTRL：undef_op 仲裁/门控 / arbitration + gating
    {0x04, 0}, // L1_DCACHE_CTRL：同上 / ditto
    {0x16c, 0}, // CACHE_ACS_FAIL_CTRL：访问失败控制 / access-failure control
    {0x170, 0}, // CACHE_ACS_FAIL_INT_ENA：访问失败中断使能 / failure interrupt enables
};
static bool s_have_snapshot;

static inline volatile uint32_t *cache_reg(size_t offset) {
    return (volatile uint32_t *)(DR_REG_CACHE_BASE + offset);
}

void cache_wake_restore_snapshot(void) {
    if (s_have_snapshot) return;
    for (size_t i = 0; i < sizeof(s_words) / sizeof(s_words[0]); i++) {
        s_words[i].value = *cache_reg(s_words[i].offset);
    }
    s_have_snapshot = true;
    ESP_LOGI(TAG, "snapshot icache_ctrl=%08lx dcache_ctrl=%08lx acs_fail=%08lx acs_ena=%08lx",
             (unsigned long)s_words[0].value, (unsigned long)s_words[1].value,
             (unsigned long)s_words[2].value, (unsigned long)s_words[3].value);
}

void cache_wake_restore_apply(void) {
    if (!s_have_snapshot) return;
    // 浅睡返回即写回；唤醒路径不会自己恢复这些配置。
    // Write back right after the light sleep returns; the wake path never
    // restores these by itself.
    for (size_t i = 0; i < sizeof(s_words) / sizeof(s_words[0]); i++) {
        *cache_reg(s_words[i].offset) = s_words[i].value;
    }
}

bool cache_wake_restore_active(void) {
    return s_have_snapshot;
}
#else
void cache_wake_restore_snapshot(void) {}
void cache_wake_restore_apply(void) {}
bool cache_wake_restore_active(void) {
    return false;
}
#endif
