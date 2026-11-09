/* SPDX-License-Identifier: MIT / public domain (stb)
 * 中文：stb_image 的编译单元。默认关闭所有可选加载器，只保留 JPEG 灰阶路径；
 * 真机上把 malloc/free 重定向到 PSRAM，避免整幅解码缓冲挤占内部 SRAM；
 * 宿主测试无 PSRAM，退回系统 malloc。
 * English: Single translation unit for stb_image. Optional loaders are disabled; only the
 * JPEG grayscale path is kept. On device the allocator is redirected to PSRAM so the
 * full-frame decode buffer never consumes internal SRAM; host tests fall back to malloc.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_ASSERT(x)         ((void)0)

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#define STBI_MALLOC(sz)        heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) \
    heap_caps_realloc((p), (newsz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p)           free(p)
#else
#include <stdlib.h>
#define STBI_MALLOC(sz)        malloc(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) realloc((p), (newsz))
#define STBI_FREE(p)           free(p)
#endif

#include "stb_image.h"
