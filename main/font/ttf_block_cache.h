/*
 * 中文：每个字体槽持有的分块缓存；只负责文件块，不持有字体、映射表或字形。
 * English: Per-font-slot block cache; owns file blocks, not faces, mapped tables or glyphs.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTF_IO_BLOCK 4096
#define TTF_IO_SLOTS 32
#define TTF_IO_RUN_MAX 8
#define TTF_PREFETCH_MAX 96

typedef struct {
    uint8_t *f_io_data; ///< 槽内块数据，按需分配 / Lazily allocated per-slot block data
    uint8_t *f_io_run_buf; ///< 连续块读取暂存 / Scratch for contiguous block reads
    uint32_t f_io_base[TTF_IO_SLOTS]; ///< 文件块起点 / File block offsets
    uint16_t f_io_fill[TTF_IO_SLOTS]; ///< 各块有效长度 / Valid bytes per block
    uint16_t f_io_age[TTF_IO_SLOTS]; ///< 原有淘汰年龄 / Original eviction ages
    uint16_t f_io_clock; ///< 原有年龄时钟 / Original age clock
    uint32_t f_touch_blocks[TTF_PREFETCH_MAX]; ///< 待预取的块 / Pending prefetch blocks
    int f_touch_n; ///< 待预取数量 / Pending prefetch count
} ttf_block_cache_t;

typedef struct {
    int fd; ///< 同步借用的文件描述符 / Synchronously borrowed file descriptor
    uint32_t *file_pos; ///< 调用方维护的文件游标 / Caller-owned file cursor
    int64_t *read_us; ///< 可选读取耗时累计 / Optional read-time accumulator
    bool (*read_raw)(uint32_t offset, void *dst, size_t bytes); ///< 绑定当前字体的原始读取 / Raw read bound to the current face
} ttf_block_source_t;

/// 缓存随字体槽存活；调用方串行化所有访问，关闭或换源前重置元数据。
/// Cache lives with its font slot; caller serializes access and resets metadata before closing or replacing a source.
/// 沿用原行为：重置保留已分配缓冲，首次分配 data 时也会清空预取列表。
/// Preserve existing behavior: reset retains allocated buffers; first data allocation also clears pending prefetch.
void ttf_block_cache_reset(ttf_block_cache_t *cache);
/// 连续读取暂存分配失败不影响单块缓存可用性。/ Failure to allocate run scratch leaves single-block caching usable.
bool ttf_block_cache_ensure(ttf_block_cache_t *cache);
/// source 和回调只在本次同步调用中借用；调用方先处理整表映射与内存字体。
/// Borrow source and callback only for this synchronous call; caller handles mapped tables and memory-backed faces first.
bool ttf_block_cache_read(ttf_block_cache_t *cache, const ttf_block_source_t *source,
                          uint32_t offset, void *dst, size_t bytes);
/// 调用方已排除整表映射覆盖的范围；容量和溢出行为沿用原实现。
/// Caller has excluded ranges covered by mapped tables; capacity and overflow behavior retain the original implementation.
void ttf_block_cache_touch(ttf_block_cache_t *cache, uint32_t offset, uint32_t bytes);
/// 原有排序、去重、合并读取和失败后逐块回退。/ Original sorting, deduplication, coalesced reads and per-block failure fallback.
void ttf_block_cache_flush(ttf_block_cache_t *cache, const ttf_block_source_t *source);
