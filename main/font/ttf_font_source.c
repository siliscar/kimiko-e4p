/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从字体引擎提取每槽文件、内存镜像与整表映射，保持读取及释放顺序。
 * English: Per-slot file, memory image and table maps extracted from the font engine, retaining read and release ordering.
 */
#include "ttf_font_source.h"
#include <string.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

// glyf+gvar 整表映射的预留与硬顶。/ Reserve and hard cap for whole-table glyf+gvar maps.
#define TTF_MAP_RESERVE (2560u * 1024u)
#define TTF_MAP_MAX (5u * 1024u * 1024u)

static const char *TAG = "ttf_font";

bool ttf_font_source_read(ttf_font_source_t *source, uint32_t offset, void *dst, size_t n, int64_t *read_us) {
    if (source->f_mem != NULL) {
        if ((uint64_t)offset + n > source->f_mem_len) return false;
        memcpy(dst, source->f_mem + offset, n);
        return true;
    }
    if (source->f_fd < 0) return false;
    int64_t t0 = read_us != NULL ? esp_timer_get_time() : 0;
    bool ok = false;
    do {
        if (source->f_file_pos != offset) {
            if (lseek(source->f_fd, (off_t)offset, SEEK_SET) < 0) {
                source->f_file_pos = UINT32_MAX;
                break;
            }
            source->f_file_pos = offset;
        }
        uint8_t* out = dst;
        size_t done = 0;
        while (done < n) {
            size_t chunk = n - done;
            if (chunk > 16384) chunk = 16384;
            ssize_t got = read(source->f_fd, out + done, chunk);
            if (got <= 0) {
                source->f_file_pos = UINT32_MAX;
                break;
            }
            done += (size_t)got;
            source->f_file_pos = offset + (uint32_t)done;
        }
        ok = done == n;
    } while (0);
    if (read_us != NULL) *read_us += esp_timer_get_time() - t0;
    return ok;
}

void ttf_font_source_unmap(ttf_font_source_t *source) {
    heap_caps_free(source->f_glyf_ram);
    source->f_glyf_ram = NULL;
    source->f_glyf_ram_off = 0;
    source->f_glyf_ram_len = 0;
    heap_caps_free(source->f_gvar_ram);
    source->f_gvar_ram = NULL;
    source->f_gvar_ram_off = 0;
    source->f_gvar_ram_len = 0;
}

bool ttf_font_source_read_map(const ttf_font_source_t *source, uint32_t off, size_t n, void *dst) {
    if (source->f_glyf_ram != NULL && off >= source->f_glyf_ram_off
        && off + n <= source->f_glyf_ram_off + source->f_glyf_ram_len) {
        memcpy(dst, source->f_glyf_ram + (off - source->f_glyf_ram_off), n);
        return true;
    }
    if (source->f_gvar_ram != NULL && off >= source->f_gvar_ram_off
        && off + n <= source->f_gvar_ram_off + source->f_gvar_ram_len) {
        memcpy(dst, source->f_gvar_ram + (off - source->f_gvar_ram_off), n);
        return true;
    }
    return false;
}

bool ttf_font_source_covers(const ttf_font_source_t *source, uint32_t off, uint32_t len) {
    if (len == 0) return true;
    if (source->f_glyf_ram != NULL && off >= source->f_glyf_ram_off
        && off + len <= source->f_glyf_ram_off + source->f_glyf_ram_len) {
        return true;
    }
    if (source->f_gvar_ram != NULL && off >= source->f_gvar_ram_off
        && off + len <= source->f_gvar_ram_off + source->f_gvar_ram_len) {
        return true;
    }
    return false;
}

bool ttf_font_source_map_table(
    ttf_font_source_t *source, uint32_t off, uint32_t len, uint8_t **ram,
    uint32_t *ram_off, uint32_t *ram_len, const char *name, int64_t *read_us
) {
    if (*ram != NULL || len < 4096 || len > TTF_MAP_MAX) return false;
    size_t largest = heap_caps_get_largest_free_block(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (largest < (size_t)len + TTF_MAP_RESERVE) {
        ESP_LOGI(
            TAG, "%s %u KB, skip map (largest %u KB)",
            name, (unsigned)(len / 1024), (unsigned)(largest / 1024)
        );
        return false;
    }
    uint8_t* p = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) return false;
    int64_t t0 = esp_timer_get_time();
    if (!ttf_font_source_read(source, off, p, len, read_us)) {
        heap_caps_free(p);
        return false;
    }
    *ram = p;
    *ram_off = off;
    *ram_len = len;
    ESP_LOGI(
        TAG, "mapped %s %u KB in %dms",
        name, (unsigned)(len / 1024),
        (int)((esp_timer_get_time() - t0) / 1000)
    );
    return true;
}

void ttf_font_source_abandon(ttf_font_source_t *source) {
    if (source->f_fd >= 0) {
        close(source->f_fd);
        source->f_fd = -1;
    }
    // 内嵌字体的字节由槽位自己分配，关槽时要连它一起还回去。
    // An embedded face owns heap bytes; closing the slot must hand them back too.
    if (source->f_mem_owned && source->f_mem != NULL) {
        heap_caps_free((void*)source->f_mem);
    }
    source->f_mem_owned = false;
    source->f_mem = NULL;
    source->f_mem_len = 0;
}
