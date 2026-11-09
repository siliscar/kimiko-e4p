/*
 * 中文：诊断构建的只读资源快照；调用方先交还预绘，不分配或回收资源。
 * English: Read-only diagnostic resource snapshots after prefetch handoff; never allocate or reclaim resources.
 */
#pragma once
#include "reader_perf.h"
#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
#include <stddef.h>
#include "esp_heap_caps.h"

typedef struct {
    size_t font_source; ///< 自有字体镜像，不含借用镜像 / Owned font images, excluding borrows
    size_t font_tables; ///< 自有映射与轮廓表 / Owned maps and outline tables
    size_t font_blocks; ///< 允许常驻的块缓存与连续读取暂存 / Retained block caches and read scratch
    size_t font_glyphs; ///< 字形条目与位图的分配容量 / Allocated glyph entries and bitmaps
    size_t font_work; ///< 允许常驻的变体工作区 / Retained variation workspace
    size_t font_catalog; ///< 允许常驻的字体目录 / Retained font catalog
    size_t layout; ///< 页表、行缓冲与图片位置表，不含借用章节 / Page, line and image-position buffers, excluding chapter borrows
    size_t chapter; ///< 当前会话接管的章节分配 / Chapter allocations owned by the current session
    size_t prefetch; ///< 预绘帧，不含任务与同步对象 / Prefetch frame, excluding task and synchronization objects
    size_t background; ///< 已解码的唯一背景 / Sole decoded background
    size_t page_images; ///< 当前页图片表与灰阶面 / Current-page image array and grayscale planes
    size_t shelf_ui; ///< 页面接管的书架数据与封面，不含工作队列 / Page-owned shelf data and covers, excluding work queues
    unsigned font_slots; ///< 已就绪字体槽 / Ready font slots
    unsigned prefetch_worker; ///< 预绘任务是否存在 / Whether the prefetch task exists
    unsigned prefetch_stack_free; ///< 预绘任务历史最小剩余栈字节 / Prefetch task historical minimum free stack bytes
} reader_memory_stats_t;

typedef struct {
    size_t txt_index; ///< TXT 目录容量 / TXT index capacity
    size_t mobi_state; ///< MOBI 实例 / MOBI instance
    size_t mobi_index; ///< MOBI 记录、分段和导航表 / MOBI records, segments and navigation tables
    size_t epub_state; ///< EPUB 实例 / EPUB instance
    size_t epub_index; ///< spine、导航及可见索引，不含池内字符串 / Spine, navigation and visible indices excluding pooled strings
    size_t epub_text; ///< 标题与锚点池，不重复计算内部别名 / Title and anchor pools without recounting interior aliases
    size_t epub_faces; ///< 书内字体描述，不含字体槽接管的镜像 / Embedded-face descriptors excluding slot-owned images
    size_t zip_state; ///< ZIP 实例 / ZIP instance
    size_t zip_index; ///< ZIP 条目及查找表 / ZIP entries and lookup table
    size_t zip_names; ///< ZIP 自有条目名称 / ZIP-owned entry names
    size_t zip_directory; ///< 尚存活的中央目录缓冲 / Live central-directory buffer
    unsigned open_files; ///< 当前书源持有的文件句柄，不含 FILE 内部分配 / Files held by the current source, excluding FILE internals
} reader_source_memory_stats_t;

struct book_epub;
struct book_mobi;
struct zip_reader;
/// UI 串行书源访问并交还预绘后，覆盖当前书源的全部字段；不遍历封面任务的独立书源。
/// Replace all current-source fields after serialized UI source access and prefetch handoff; exclude independent cover-job sources.
void book_source_memory_stats(reader_source_memory_stats_t *stats);
/// 只统计实例直接拥有的分配，字体槽镜像归字体统计。/ Count directly owned allocations; font-slot images belong to font stats.
void book_epub_memory_stats(const struct book_epub *book, reader_source_memory_stats_t *stats);
void book_mobi_memory_stats(const struct book_mobi *book, reader_source_memory_stats_t *stats);
/// 只在串行访问边界读取 ZIP；不读取文件或遍历 libc 内部。/ Read ZIP at serialized boundaries without file I/O or libc traversal.
void zip_reader_memory_stats(const struct zip_reader *zip, reader_source_memory_stats_t *stats);
/// 与原资源记录独立输出，不改变原日志协议；调用方先结束操作计时。
/// Emit separately from the original resource protocol after callers finish operation timing.
void reader_perf_source_memory(const char *event, const reader_source_memory_stats_t *stats);

/// 仅查询未释放的自有分配基址；静态数据、内部指针与借用指针不得传入。
/// Query live owned allocation bases only; never pass static data, interior pointers or borrows.
static inline size_t reader_memory_owned(const void *pointer) {
    return pointer ? heap_caps_get_allocated_size((void *)pointer) : 0;
}
/// UI 已排除字体和布局的并发借用；各模块仅覆盖自己负责的字段。
/// UI has excluded concurrent font/layout borrowers; each module replaces only its own fields.
void ttf_font_memory_stats(reader_memory_stats_t *stats);
/// 仅记录布局拥有的四项动态缓冲。/ Record only the four dynamic buffers owned by layout.
void book_layout_memory_stats(reader_memory_stats_t *stats);
/// 后台已交还后记录帧与任务栈；不取得新的锁。/ Record frame and task stack after worker handoff without acquiring locks.
void reader_prefetch_memory_stats(reader_memory_stats_t *stats);
/// 仅记录背景成品缓存，不包含已经交还的解码暂存。/ Record only the final background cache, excluding returned decode scratch.
void ui_reader_background_memory_stats(reader_memory_stats_t *stats);
/// 计时结束后输出独立记录；统计是已列出资源的容量视图，不是全堆分解。
/// Emit a separate record after timing stops; listed allocation capacities do not decompose the whole heap.
void reader_perf_memory(const char *event, const reader_memory_stats_t *stats);
/// 活跃计时期间跳过资源遍历与输出，避免污染该操作的耗时。/ Skip resource walks and output while timing is active.
bool reader_perf_memory_ready(void);
#endif
