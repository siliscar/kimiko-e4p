/* 中文：串行书籍路径迁移；仅在文件任务、HTTP 和阅读资源交还后调用。
 * English: Serialized book-path relocation, only after file workers, HTTP and reader resources return. */
#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef esp_err_t (*book_path_state_extra_fn)(const char *old_path, const char *new_path, uint32_t size);
/// 只检查源记录和目标空位，不写入；拒绝碰撞和未知格式。/ Check source ownership and empty destinations without writes; reject collisions and unknown formats.
esp_err_t book_path_state_prepare(const char *old_path, const char *new_path);
/// 目标全部提交后才清源，可在同一文件操作日志下幂等重试；不重新分配最近阅读序号。
/// Commit all destinations before clearing sources; retry only under the same file-operation journal, preserving recent sequences.
esp_err_t book_path_state_relocate(const char *old_path, const char *new_path, uint32_t size,
                                  book_path_state_extra_fn extra);
