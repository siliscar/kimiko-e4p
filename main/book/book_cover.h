#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../image/image_decode.h"

#define BOOK_COVER_W 176
#define BOOK_COVER_H 240

// Output is one grayscale byte per pixel (0=black, 255=white).
bool book_cover_thumbnail(const uint8_t *data, size_t size, bool png,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H]);
/// 优先读取书内封面，缺少或无法解码时按书籍标识生成并缓存灰阶封面。
/// Prefer a valid embedded cover; otherwise generate and cache a deterministic grayscale cover.
bool book_cover_load_gray(const char *path, const char *title, const char *author,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H],
                          bool allow_decode, bool *pending);

/// 列表封面缓存保留完整原比例；输出不超过176×240，宽高返回有效像素区。/ Cache uncropped list covers, bounded to 176x240, returning the valid pixel dimensions.
bool book_cover_load_list_gray(const char *path, const char *title, const char *author,
                               uint8_t out[BOOK_COVER_W * BOOK_COVER_H], bool allow_decode,
                               bool *pending, unsigned *width, unsigned *height);

/// 快刷已处理的点阵封面；按书源身份、尺寸、对比度与抖动相位失效，像素带校验。
/// Finished fast-cover pixels: invalidate by source, geometry, contrast and dither phase; validate payload.
typedef struct { unsigned width, height, stride, contrast, phase; } book_fast_cover_key_t;
bool book_cover_fast_cache_read(const char *path, const char *title, const char *author,
                                book_fast_cover_key_t key, uint8_t *out, bool *white_edge);
void book_cover_fast_cache_write(const char *path, const char *title, const char *author,
                                 book_fast_cover_key_t key, const uint8_t *out, bool white_edge);

/// 仅文件/图像工作，不调用字库；1完成、0需要UI生成封面、-1失败。
/// File/image work only, no font calls:1 ready,0 UI-generated cover needed,-1 failure.
int book_cover_load_io(const char *path,const char *title,const char *author,uint8_t *out,bool list,unsigned *width,unsigned *height,bool *auto_cache);
/// 仅读取有效列表缓存的尺寸头，点阵命中时无需分配灰阶像素。/ Read validated list dimensions without a gray allocation on a packed-cache hit.
bool book_cover_list_cache_dimensions(const char *path,const char *title,const char *author,unsigned *width,unsigned *height);
/// 后台持久化UI生成的176x240封面。/ Persist an UI-generated176x240 cover in the worker.
void book_cover_store_generated(const char *path,const char *title,const char *author,const uint8_t *pixels,bool list,int64_t size,int64_t modified);

/// 异步结果仅在来源尺寸/修改时间仍匹配时持久化，防止换书后固化旧像素。
/// Persist asynchronous pixels only under their original source identity, never a replaced book's key.
void book_cover_fast_cache_write_checked(const char *path,const char *title,const char *author,book_fast_cover_key_t key,const uint8_t *pixels,bool edge,int64_t size,int64_t modified);
