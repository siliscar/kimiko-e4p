/*
 * 中文：书架封面的独占像素与异步状态；页面零初始化上下文，只通过接口修改。
 * English: Shelf-owned pixels and asynchronous state; pages zero-initialize the context and mutate only through this API.
 */
#pragma once
#include "book_cover_jobs.h"

#define BOOK_SHELF_COVER_SLOTS 13

typedef struct {
    int index; ///< 当前可见书目索引。/ Current visible catalog index.
    uint8_t *gray, *fast_bits; ///< 模块独占像素，调用方只借用，不释放或修改。/ Owned pixels; callers only borrow, never free or modify them.
    unsigned width, height; ///< 解码得到的尺寸。/ Decoded dimensions.
    uint8_t fast_contrast, fast_phase; ///< 黑白缓存匹配键。/ Packed-cache matching key.
    bool fast_white_edge; ///< 深色封面上的收藏白边。/ Favorite outline on dark artwork.
} book_shelf_cover_t;

typedef struct {
    book_shelf_cover_t slots[BOOK_SHELF_COVER_SLOTS]; ///< 内部资源，仅通过接口访问。/ Internal resources, accessed only through the API.
    unsigned generation, pending, warm, dirty, presenting; ///< 内部异步与显示状态。/ Internal asynchronous and presentation state.
    int64_t warm_after_ms; ///< 下一次空闲预热时间。/ Next idle warming time.
} book_shelf_covers_t;

/// 所有 row 必须小于槽位数；collect 还须小于工作队列容量且处于 pending。/ All rows must fit the slots; collect additionally requires a pending worker-capacity slot.
/// 同步只读槽位视图随上下文有效；像素借用仅到对应表示被释放。/ Synchronous read-only slot views last with the context; pixel borrows end when their representation is released.
const book_shelf_cover_t *book_shelf_covers_slot(const book_shelf_covers_t *state, unsigned row);
/// 取消后台代次，保留已加载像素与掩码供普通离页复用。/ Cancel background generation, retaining loaded pixels and masks across ordinary exits.
void book_shelf_covers_cancel(book_shelf_covers_t *state);
/// 先由调用方取消任务，再释放像素、清空掩码、将索引设为 -1。/ Callers cancel jobs first; then release pixels, clear masks and set indices to -1.
void book_shelf_covers_release(book_shelf_covers_t *state);
/// 首槽索引变化时取消旧代次并清空掩码，不提前释放槽位。/ Cancel the old generation and clear masks on a first-slot index change, without releasing slots early.
void book_shelf_covers_page(book_shelf_covers_t *state, int first_index);
/// 匹配黑白缓存的对比度和相位。/ Match packed artwork by contrast and phase.
bool book_shelf_covers_fast_matches(const book_shelf_covers_t *state, unsigned row, unsigned contrast, unsigned phase);
/// 保留有效或已失败空槽，否则替换索引并为有效书籍排队。/ Retain valid or failed empty slots; otherwise replace the index and queue valid books.
void book_shelf_covers_prepare(book_shelf_covers_t *state, unsigned row, int index, bool valid,
                               bool packed, unsigned contrast, unsigned phase, int64_t now_ms);
/// 返回尚未完成的槽位掩码。/ Return unfinished slots.
unsigned book_shelf_covers_pending(const book_shelf_covers_t *state);
/// 丢弃失效书目的待处理标记。/ Drop pending work for an invalid catalog item.
void book_shelf_covers_skip(book_shelf_covers_t *state, unsigned row);
/// 提交并收取一个槽位；提交失败重建代次重试一次，成功时转交像素给模块。/ Submit and collect one slot; retry submission once with a new generation, adopting successful pixels.
bool book_shelf_covers_collect(book_shelf_covers_t *state, unsigned row, const book_cover_job_t *request);
/// 释放黑白表示；保留灰阶。/ Release packed artwork, retaining gray.
void book_shelf_covers_drop_fast(book_shelf_covers_t *state, unsigned row);
/// 接管转换或磁盘读取的黑白缓冲；调用前先释放旧黑白表示，不释放灰阶。/ Adopt converted or disk-loaded packed pixels after dropping prior packed artwork, without releasing gray.
void book_shelf_covers_set_fast(book_shelf_covers_t *state, unsigned row, uint8_t *pixels,
                               bool white_edge, unsigned contrast, unsigned phase);
/// 释放转换后的灰阶，即使黑白分配失败也释放。/ Release converted gray even when packed allocation failed.
void book_shelf_covers_drop_gray(book_shelf_covers_t *state, unsigned row);
/// 要求整组时等待所有槽位，否则直接返回待显示标记。/ Wait for all slots when grouping is required, otherwise return dirty artwork directly.
unsigned book_shelf_covers_reveal(const book_shelf_covers_t *state, bool grouped);
/// 记录本次局部推屏的封面掩码。/ Record artwork included in this area presentation.
void book_shelf_covers_presenting(book_shelf_covers_t *state, unsigned mask);
/// 成功且整组就绪的整页或成功的封面批次才消费标记；总是清空本帧掩码。/ Acknowledge only a ready successful page or successful cover batch; always clear the frame mask.
void book_shelf_covers_ack(book_shelf_covers_t *state, bool success, bool whole_ready, bool batch);
/// 返回是否有到期的预热槽位，且可见封面已处理完。/ Report due warming only after visible artwork finishes.
bool book_shelf_covers_warm_due(const book_shelf_covers_t *state, int64_t now_ms);
/// 领取首个预热槽并延后 1000ms；调用方负责导航检查与实际文件操作。/ Take the first warm slot and defer 1000ms; callers check navigation and perform file work.
int book_shelf_covers_warm_take(book_shelf_covers_t *state, unsigned rows, int64_t now_ms);
