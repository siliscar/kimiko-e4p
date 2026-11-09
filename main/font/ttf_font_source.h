/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从字体引擎提取每槽文件、内存镜像与整表映射，保持读取及释放顺序。
 * English: Per-slot file, memory image and table maps extracted from the font engine, retaining read and release ordering.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int f_fd; ///< 文件描述符，关闭后为 -1 / File descriptor; -1 after close
    const uint8_t* f_mem; ///< 借用或自有的字体镜像 / Borrowed or owned font image
    uint32_t f_mem_len; ///< 镜像字节数 / Image bytes
    bool f_mem_owned;      ///< f_mem 在堆上、关闭槽位时释放 / f_mem is heap, freed on close
    uint32_t f_file_pos; ///< 文件游标，未知时为 UINT32_MAX / File cursor; UINT32_MAX when unknown
    uint8_t* f_glyf_ram; ///< 槽持有的 glyf 映射 / Slot-owned glyf map
    uint32_t f_glyf_ram_off; ///< glyf 文件起点 / glyf file offset
    uint32_t f_glyf_ram_len; ///< glyf 映射长度 / glyf mapped bytes
    uint8_t* f_gvar_ram; ///< 槽持有的 gvar 映射 / Slot-owned gvar map
    uint32_t f_gvar_ram_off; ///< gvar 文件起点 / gvar file offset
    uint32_t f_gvar_ram_len; ///< gvar 映射长度 / gvar mapped bytes
} ttf_font_source_t;

/// 初始化由字体槽负责：文件为 -1、游标为 UINT32_MAX，其余清零；所有访问由调用方串行化。
/// Slots initialize fd to -1, cursor to UINT32_MAX and everything else to zero; callers serialize all access.
/// 按原有文件游标与 16 KiB 读取上限同步读；read_us 非空时累计文件 I/O 时间，内存读取不计时。
/// Read synchronously with the existing cursor and 16 KiB chunk cap; optional read_us counts file I/O only.
bool ttf_font_source_read(ttf_font_source_t *source, uint32_t offset, void *dst,
                           size_t bytes, int64_t *read_us);
/// 先检查 glyf 后检查 gvar，不触发文件读取。/ Check glyf before gvar without file I/O.
bool ttf_font_source_read_map(const ttf_font_source_t *source, uint32_t offset,
                               size_t bytes, void *dst);
/// 零长度视为已覆盖；保持原边界判断。/ Empty ranges count as covered; retain existing range checks.
bool ttf_font_source_covers(const ttf_font_source_t *source, uint32_t offset, uint32_t bytes);
/// 保留原 4 KiB 下限、5 MiB 上限和 2560 KiB 余量；读取成功后才提交映射。
/// Retain the 4 KiB floor, 5 MiB cap and 2560 KiB reserve; commit maps only after a successful read.
bool ttf_font_source_map_table(ttf_font_source_t *source, uint32_t offset, uint32_t bytes,
                                uint8_t **ram, uint32_t *ram_offset, uint32_t *ram_bytes,
                                const char *name, int64_t *read_us);
/// 归还两张整表映射并清空其范围，不关闭字节源。/ Free both table maps and clear ranges without closing the byte source.
void ttf_font_source_unmap(ttf_font_source_t *source);
/// 先关文件，再释放自有字体镜像；借用镜像不释放，映射与块缓存由原调用顺序另行处理。
/// Close files before freeing owned images; retain borrowed images, leaving maps and block caches to existing caller ordering.
void ttf_font_source_abandon(ttf_font_source_t *source);
