/*
 * 中文：从构建生成的无损资源逐行恢复原图，临时解压内存放 PSRAM。
 * English: Restore original pixels from build-generated lossless assets, using temporary PSRAM.
 */
#include "ui_builtin_image.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "zlib.h"
#include <string.h>

extern const uint8_t lock_start[] asm("_binary_lock_4bpp_kbz_start");
extern const uint8_t lock_end[] asm("_binary_lock_4bpp_kbz_end");
extern const uint8_t avatar_start[] asm("_binary_avatar_4bpp_bin_start");
extern const uint8_t avatar_end[] asm("_binary_avatar_4bpp_bin_end");

static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static size_t image_size(const uint8_t *data, size_t bytes) {
    if (!data || bytes <= 12 || memcmp(data, "KBI1", 4) || word(data + 8) != bytes - 12) return 0;
    return word(data + 4);
}

static const uint8_t *image_data(ui_builtin_image_t image, size_t *bytes) {
    if (image == UI_BUILTIN_IMAGE_LOCK || image == UI_BUILTIN_IMAGE_LOADING) {
        *bytes = (size_t)(lock_end - lock_start);
        return lock_start;
    }
    *bytes = 0;
    return NULL;
}

size_t ui_builtin_image_size(ui_builtin_image_t image) {
    size_t bytes;
    const uint8_t *data = image_data(image, &bytes);
    return image_size(data, bytes);
}

static voidpf image_alloc(voidpf opaque, uInt items, uInt bytes) {
    (void)opaque;
    return heap_caps_calloc(items, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void image_free(voidpf opaque, voidpf memory) {
    (void)opaque;
    heap_caps_free(memory);
}

static bool draw_image(uint8_t *framebuffer, const uint8_t *data, size_t bytes) {
    const int width = epd_rotated_display_width(), height = epd_rotated_display_height();
    if (!framebuffer || width <= 0 || height <= 0 || (width & 1)) return false;
    const size_t row_bytes = (size_t)width / 2, expected = row_bytes * (size_t)height;
    memset(framebuffer, 255, expected);
    if (image_size(data, bytes) != expected) return false;

    uint8_t *row = heap_caps_malloc(row_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!row) return false;
    z_stream stream = {.zalloc = image_alloc, .zfree = image_free};
    if (inflateInit(&stream) != Z_OK) {
        heap_caps_free(row);
        return false;
    }
    stream.next_in = (Bytef *)(data + 12);
    stream.avail_in = (uInt)(bytes - 12);
    int status = Z_OK;
    bool ok = true;
    for (int y = 0; y < height; ++y) {
        stream.next_out = row;
        stream.avail_out = (uInt)row_bytes;
        status = inflate(&stream, Z_NO_FLUSH);
        if ((status != Z_OK && status != Z_STREAM_END) || stream.avail_out) {
            ok = false;
            break;
        }
        epd_draw_rotated_image((EpdRect){.x = 0, .y = y, .width = width, .height = 1}, row, framebuffer);
    }
    // 最后一行填满后仍需读完校验尾，额外输出一个字节也视为损坏。/ Consume the checksum after the final row; even one excess output byte invalidates the resource.
    if (ok && status != Z_STREAM_END) {
        stream.next_out = row;
        stream.avail_out = 1;
        status = inflate(&stream, Z_FINISH);
    }
    ok = ok && status == Z_STREAM_END && stream.total_out == expected && stream.avail_in == 0;
    inflateEnd(&stream);
    heap_caps_free(row);
    if (!ok) memset(framebuffer, 255, expected);
    return ok;
}

bool ui_builtin_image_draw(uint8_t *framebuffer, ui_builtin_image_t image) {
    size_t bytes;
    const uint8_t *data = image_data(image, &bytes);
    return draw_image(framebuffer, data, bytes);
}

void ui_builtin_avatar_draw(uint8_t *framebuffer, int x, int y, int side) {
    if (!framebuffer || (side != 60 && side != 82) ||
        (size_t)(avatar_end - avatar_start) != (60 * 60 + 82 * 82) / 2) return;
    const uint8_t *pixels = avatar_start + (side == 82 ? 60 * 60 / 2 : 0);
    epd_draw_rotated_image((EpdRect){.x = x, .y = y, .width = side, .height = side}, pixels, framebuffer);
}
