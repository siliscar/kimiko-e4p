/*
 * 中文：内嵌开机、默认锁屏与头像绘制；全屏图逐行解压，不推屏。
 * English: Draw embedded splash, fallback lock art and avatars; decode full-screen art by row, never present.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    UI_BUILTIN_IMAGE_LOCK, ///< 默认锁屏图 / Fallback lock art
    UI_BUILTIN_IMAGE_LOADING, ///< 开机与默认锁屏共用同一份资源 / Splash shares the fallback lock resource
} ui_builtin_image_t;

/// 返回校验过资源头的原始像素字节数，资源头无效返回零。/ Return the raw pixel byte count, or zero for an invalid resource header.
size_t ui_builtin_image_size(ui_builtin_image_t image);

/// 逐行解压绘制，保持当前旋转；失败清白并返回 false，不保留解压缓存。/ Decode rows at the current rotation; clear white on failure, return false and retain no decoder cache.
bool ui_builtin_image_draw(uint8_t *framebuffer, ui_builtin_image_t image);

/// 绘制60或82像素默认头像，不读卡或分配内存。/ Draw the 60px or 82px default avatar without card IO or allocation.
void ui_builtin_avatar_draw(uint8_t *framebuffer, int x, int y, int side);
