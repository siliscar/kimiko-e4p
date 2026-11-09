/* SPDX-License-Identifier: Apache-2.0
 * 中文：S31 硬件 JPEG 解码封装。一个惰性引擎加互斥锁，整幅输出到 PSRAM；
 * 只接受基线 SOF0，尺寸/内存/引擎任何一项不满足都返回 false 让调用者回退。
 * English: S31 hardware JPEG decode wrapper. One lazy engine behind a mutex writes
 * a full frame into PSRAM; only baseline SOF0 is accepted and any size, memory or
 * engine failure returns false so the caller can fall back.
 *
 * 冻结：不处理渐进式/PNG/文件流；不做缩放；不在这里缓存像素。
 * Frozen: No progressive/PNG/file streaming; no scaling; no pixel caching here.
 */
#include "book_jpeg_hw.h"
#include <stdlib.h>
#include <string.h>

#if BOOK_JPEG_HW

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 单张上限 8 MB：约 4.2 Mpx 的 RGB565。再大就让软件走 1/8，别把 PSRAM 余量吃光。
// 8 MB per frame: about 4.2 Mpx of RGB565. Anything larger goes back to the software 1/8 path.
#define HW_JPEG_BUDGET_MAX (8u * 1024u * 1024u)
// 解码期间给其他 PSRAM 消费者留的余量。/ Headroom for other PSRAM consumers while decoding.
#define HW_JPEG_RESERVE_BYTES (2u * 1024u * 1024u)
#define HW_JPEG_TIMEOUT_MS 1000
// 连续超时/状态错误两次后本次开机停用，避免坏图每张卡 1 秒。
// Disable for this boot after two consecutive timeout/state errors so a bad image cannot cost 1 s each.
#define HW_JPEG_MAX_FAILURES 2

static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_lock_init = portMUX_INITIALIZER_UNLOCKED;
static jpeg_decoder_handle_t s_engine;
static unsigned s_failures;
static bool s_disabled;

// 惰性创建互斥锁；两个线程同时首次解码时只保留一个。
// Lazily create the mutex; with two threads decoding for the first time exactly one survives.
static SemaphoreHandle_t lock_get(void) {
    portENTER_CRITICAL(&s_lock_init);
    SemaphoreHandle_t lock = s_lock;
    portEXIT_CRITICAL(&s_lock_init);
    if (lock) return lock;
    SemaphoreHandle_t fresh = xSemaphoreCreateMutex();
    if (!fresh) return NULL;
    portENTER_CRITICAL(&s_lock_init);
    if (!s_lock) s_lock = fresh;
    lock = s_lock;
    portEXIT_CRITICAL(&s_lock_init);
    if (lock != fresh) vSemaphoreDelete(fresh);
    return lock;
}

// 调用者持有锁。/ Caller holds the lock.
static bool engine_ready(void) {
    if (s_engine) return true;
    jpeg_decode_engine_cfg_t cfg = {
        .timeout_ms = HW_JPEG_TIMEOUT_MS,
        // 浅睡时由驱动保存/恢复 JPEG 寄存器，避免唤醒后第一张图失败。
        // Let the driver save/restore JPEG registers across light sleep.
        .flags.allow_pd = 1,
    };
    if (jpeg_new_decoder_engine(&cfg, &s_engine) != ESP_OK) s_engine = NULL;
    return s_engine != NULL;
}

// 解码失败后丢弃引擎；下一次调用会重建，浅睡唤醒后的异常状态也能自愈。
// Drop the engine after a failed decode; the next call rebuilds it, which also heals odd post-sleep state.
static void engine_reset(void) {
    if (s_engine) jpeg_del_decoder_engine(s_engine);
    s_engine = NULL;
}

bool book_jpeg_hw_decode(const uint8_t *data, size_t size, size_t budget,
                         book_jpeg_hw_image_t *out) {
    if (!data || !size || !out || size > UINT32_MAX) return false;
    memset(out, 0, sizeof(*out));
    if (s_disabled) return false;
    if (!budget) budget = HW_JPEG_BUDGET_MAX;
    if (budget > HW_JPEG_BUDGET_MAX) budget = HW_JPEG_BUDGET_MAX;

    jpeg_decode_picture_info_t info = {0};
    if (jpeg_decoder_get_info(data, (uint32_t)size, &info) != ESP_OK) return false;
    if (!info.width || !info.height) return false;
    // 驱动拒绝像素总数不是 8 的倍数，且 2D-DMA 地址字段只有 14 位；先在这里挡掉，
    // 免得这类封面先在驱动里报错再回退。
    // The driver rejects pixel counts not divisible by 8 and 14-bit DMA dimensions;
    // filter here so such covers do not log a driver error before falling back.
    if (((uint64_t)info.width * info.height) % 8 != 0) return false;
    if (info.width > 16383 || info.height > 16383) return false;

    unsigned mcu_w, mcu_h;
    switch (info.sample_method) {
    case JPEG_DOWN_SAMPLING_YUV420: mcu_w = 16; mcu_h = 16; break;
    case JPEG_DOWN_SAMPLING_YUV422: mcu_w = 16; mcu_h = 8; break;
    case JPEG_DOWN_SAMPLING_YUV444: mcu_w = 8; mcu_h = 8; break;
    case JPEG_DOWN_SAMPLING_GRAY: mcu_w = 8; mcu_h = 8; break;
    default: return false;
    }
    const bool gray = info.sample_method == JPEG_DOWN_SAMPLING_GRAY;
    const unsigned stride = (info.width + mcu_w - 1) / mcu_w * mcu_w;
    const unsigned padded_h = (info.height + mcu_h - 1) / mcu_h * mcu_h;
    if (stride > 16383 || padded_h > 16383) return false;
    // 先按 64 位算，避免 32 位 size_t 上 width*height 溢出后误判成“够小”。
    // Compute in 64 bits first so width*height cannot wrap on a 32-bit size_t.
    const uint64_t need = (uint64_t)stride * padded_h * (gray ? 1u : 2u);
    if (need > budget || need > SIZE_MAX - HW_JPEG_RESERVE_BYTES) return false;
    const size_t bytes = (size_t)need;

    // 整幅 RGB565 很占 PSRAM，先确认余量再分配。/ A full RGB565 frame is large; check headroom first.
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (free_bytes < bytes + HW_JPEG_RESERVE_BYTES) return false;

    SemaphoreHandle_t lock = lock_get();
    if (!lock) return false;
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
    if (!engine_ready()) {
        xSemaphoreGive(lock);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t alloc_size = 0;
    uint8_t *pixels = jpeg_alloc_decoder_mem(bytes, &mem_cfg, &alloc_size);
    if (!pixels) {
        xSemaphoreGive(lock);
        return false;
    }

    jpeg_decode_cfg_t cfg = {
        .output_format = gray ? JPEG_DECODE_OUT_FORMAT_GRAY : JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_size = 0;
    esp_err_t err = jpeg_decoder_process(s_engine, &cfg, data, (uint32_t)size, pixels,
                                         (uint32_t)alloc_size, &out_size);
    // 浅睡唤醒后旧引擎可能报 INVALID_STATE；重建一次再试，避免第一张图就回退。
    // A stale engine after light sleep can report INVALID_STATE; rebuild once and retry
    // so the first cover after wake does not fall back.
    if (err == ESP_ERR_INVALID_STATE) {
        engine_reset();
        if (engine_ready()) {
            out_size = 0;
            err = jpeg_decoder_process(s_engine, &cfg, data, (uint32_t)size, pixels,
                                       (uint32_t)alloc_size, &out_size);
        }
    }

    bool ok = false;
    if (err == ESP_OK && out_size) {
        // 用返回的字节数反推真实行宽，容忍驱动按 8 或 16 对齐的差异。
        // Derive the real stride from the returned byte count so 8- or 16-pixel padding both work.
        const unsigned bytes_per_pixel = gray ? 1u : 2u;
        const size_t row_total = (size_t)out_size / bytes_per_pixel;
        if (padded_h && row_total % padded_h == 0) {
            const unsigned actual_stride = (unsigned)(row_total / padded_h);
            if (actual_stride >= info.width && actual_stride <= stride) {
                out->pixels = pixels;
                out->width = info.width;
                out->height = info.height;
                out->stride = actual_stride;
                out->gray = gray;
                ok = true;
            }
        }
    }

    if (!ok) {
        free(pixels);
        // 超时/状态错误说明引擎或总线可能卡住；重建一次，连续两次就停用。
        // Timeout/state errors may leave the engine stuck; rebuild once, disable after two.
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE ||
            (err == ESP_OK && out_size)) {
            engine_reset();
            if (++s_failures >= HW_JPEG_MAX_FAILURES) s_disabled = true;
        }
    } else {
        s_failures = 0;
    }
    xSemaphoreGive(lock);
    return ok;
}

void book_jpeg_hw_free(book_jpeg_hw_image_t *image) {
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

#else

bool book_jpeg_hw_decode(const uint8_t *data, size_t size, size_t budget,
                         book_jpeg_hw_image_t *out) {
    (void)data;
    (void)size;
    (void)budget;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

void book_jpeg_hw_free(book_jpeg_hw_image_t *image) {
    if (image) memset(image, 0, sizeof(*image));
}

#endif
