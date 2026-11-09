/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：使用 libwebp 的裁剪缩放解码，避免分配源图大小的输出像素。
 * English: Use libwebp crop/scale decoding without a source-sized output framebuffer.
 * 冻结：仅静态图片；动画或超限输入沿用加载失败回退，不改变页面或设置。
 * Frozen: Static images only; animated/oversized inputs fail without page or settings changes.
 */
#include "image_webp.h"
#include "image_decode.h"
#include "esp_heap_caps.h"
#include "webp/decode.h"
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

bool book_webp_grayscale(const uint8_t *data, size_t size, unsigned width,
                         unsigned height, uint8_t *out) {
    if (!data || !out || !size || !width || !height || width > 1216 || height > 1216 ||
        (size_t)width * height > 684u * 1216u) return false;
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) ||
        WebPGetFeatures(data, size, &config.input) != VP8_STATUS_OK ||
        config.input.has_animation || config.input.width <= 0 || config.input.height <= 0 ||
        config.input.width > 8192 || config.input.height > 8192 ||
        (uint64_t)config.input.width * config.input.height > 4194304u) return false;
    const book_crop_t crop = book_cover_crop((unsigned)config.input.width,
        (unsigned)config.input.height, width, height);
    size_t bytes = (size_t)width * height * 4;
    uint8_t *rgba = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgba) return false;
    config.options.use_cropping = 1;
    config.options.crop_left = (int)crop.x;
    config.options.crop_top = (int)crop.y;
    config.options.crop_width = (int)crop.width;
    config.options.crop_height = (int)crop.height;
    config.options.use_scaling = 1;
    config.options.scaled_width = (int)width;
    config.options.scaled_height = (int)height;
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = rgba;
    config.output.u.RGBA.stride = (int)width * 4;
    config.output.u.RGBA.size = bytes;
    bool ok = WebPDecode(data, size, &config) == VP8_STATUS_OK;
    if (ok) {
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                size_t at = (size_t)y * width + x;
                const uint8_t *pixel = rgba + at * 4;
                unsigned gray = (77u * pixel[0] + 150u * pixel[1] + 29u * pixel[2]) >> 8;
                out[at] = (uint8_t)((gray * pixel[3] + 255u * (255u - pixel[3]) + 127u) / 255u);
            }
#ifdef ESP_PLATFORM
            if ((y & 63u) == 0) vTaskDelay(1);
#endif
        }
    }
    WebPFreeDecBuffer(&config.output);
    free(rgba);
    return ok;
}
