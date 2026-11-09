/* SPDX-License-Identifier: Apache-2.0
 * 中文：通用内存与文件流图像解码、缩放；不持有书籍或页面状态。
 * English: Generic memory/stream image decoding and scaling, without book or page state.
 */
#include "image_decode.h"
#include "image_webp.h"
#include "book_jpeg_hw.h"
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "JPEGDEC.h"
#include "stb_image.h"
#include "png.h"
#if defined(ESP_PLATFORM) && CONFIG_JD_USE_ROM
#include "rom/tjpgd.h"
typedef unsigned int stream_jpeg_result_t;
typedef unsigned int stream_jpeg_size_t;
#else
#include "tjpgd.h"
typedef int stream_jpeg_result_t;
typedef size_t stream_jpeg_size_t;
#endif
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
static void decode_yield(unsigned at) {
#ifdef ESP_PLATFORM
    if ((at & 63u) == 0) vTaskDelay(1);
#else
    (void)at;
#endif
}

// stb 整幅解码的像素预算：约 200 万像素（如 1400×1400），单通道灰度约 2 MB 峰值，
// 缩到一屏（≤684×1216）后细节仍完整；更大的源图继续走 DC-only 1/8，避免 OOM。
// stb full-frame pixel budget: ~2 Mpx (e.g. 1400×1400) peaks near 2 MB single-channel;
// anything larger keeps the DC-only 1/8 path instead of risking OOM.
#ifndef STBI_JPEG_PIXEL_BUDGET
#define STBI_JPEG_PIXEL_BUDGET (2000000u)
#endif

static void *cover_alloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static uint8_t luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((unsigned)r * 77 + (unsigned)g * 150 + (unsigned)b * 29) >> 8);
}

// 目标下标映射回源窗口坐标：中心对齐，Q8 定点，整数部分进 *out_at、小数部分 0..255 进
// *out_frac。原来每个目标像素直接取一个源像素，缩放后细节整块丢掉；中心对齐保证裁出来的
// 窗口正落在目标上，不会差半个像素。
// Map a destination index back to source-window coordinates: centre-aligned, Q8 fixed point, the
// integer part into *out_at and the 0..255 fraction into *out_frac. Sampling a single source pixel
// per destination pixel dropped detail wholesale once scaled down; centre-aligning keeps the crop
// window landing on the destination instead of half a pixel off.
static void scale_axis(unsigned at, unsigned dst_n, unsigned src_n, int *out_at, int *out_frac) {
    if (dst_n < 2 || src_n < 2) { *out_at = 0; *out_frac = 0; return; }
    // 源坐标 = ((2·at + 1)·src_n − dst_n) / (2·dst_n)。/ Source = ((2*at + 1)*src_n - dst_n) / (2*dst_n).
    int64_t q = (((2 * (int64_t)at + 1) * src_n - (int64_t)dst_n) * 256) / (2 * (int64_t)dst_n);
    if (q < 0) q = 0;
    const int64_t last = (int64_t)src_n - 1;
    if ((q >> 8) > last) q = last << 8;
    *out_at = (int)(q >> 8);
    *out_frac = (int)(q & 0xFF);
}

typedef struct { uint16_t at; uint8_t frac; } scale_step_t;

// 一根轴的映射表，放在 PSRAM：同一根轴上每个像素的权重都一样，算一次就够。
// One axis's map, kept in PSRAM: the weights repeat along an axis, so they are computed once.
static scale_step_t *scale_map(unsigned dst_n, unsigned src_n) {
    scale_step_t *map = cover_alloc(sizeof(scale_step_t) * (dst_n ? dst_n : 1));
    if (!map) return NULL;
    for (unsigned i = 0; i < dst_n; ++i) {
        int at = 0, frac = 0;
        scale_axis(i, dst_n, src_n, &at, &frac);
        map[i].at = (uint16_t)at;
        map[i].frac = (uint8_t)frac;
    }
    return map;
}

// 取相邻的下一个源下标，到边界就停住，相当于把边缘像素拉出去用。
// The next source index, clamped at the edge so the border pixel is stretched outwards.
static inline int scale_next(int at, unsigned src_n) {
    return (unsigned)(at + 1) < src_n ? at + 1 : at;
}

// 四点双线性：a b 在上、c d 在下，fx 与 fy 是 0..255 的权重。
// Four-point bilinear: a b on top, c d below, fx and fy are 0..255 weights.
static inline uint8_t bilinear4(int a, int b, int c, int d, int fx, int fy) {
    const int top = a + (((b - a) * fx + 128) >> 8);
    const int bot = c + (((d - c) * fx + 128) >> 8);
    return (uint8_t)(top + (((bot - top) * fy + 128) >> 8));
}

// RGB565 一个像素的亮度，双线性要连着取几个点所以单列。
// Luminance of one RGB565 pixel; separate because bilinear reads several per output pixel.
static inline int rgb565_gray(const uint8_t *row, unsigned at) {
    const uint8_t *p = row + (size_t)at * 2;
    const uint16_t color = (uint16_t)p[0] | (uint16_t)p[1] << 8;
    const uint8_t r = (uint8_t)((color >> 11) & 31);
    const uint8_t g = (uint8_t)((color >> 5) & 63);
    const uint8_t b = (uint8_t)(color & 31);
    return luminance((uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                     (uint8_t)((b << 3) | (b >> 2)));
}

book_crop_t book_cover_crop(unsigned src_width, unsigned src_height,
                            unsigned dst_width, unsigned dst_height) {
    book_crop_t crop = {0, 0, src_width, src_height};
    if (!src_width || !src_height || !dst_width || !dst_height) return crop;
    // 比较 dst_w/src_w 与 dst_h/src_h，用交叉相乘避免浮点。
    // Compare dst_w/src_w against dst_h/src_h by cross-multiplying, avoiding floating point.
    if ((uint64_t)dst_width * src_height >= (uint64_t)dst_height * src_width) {
        // 宽边受限：占满整宽，上下居中裁。/ Width-bound: keep the full width, crop top and bottom.
        unsigned height = (unsigned)((uint64_t)dst_height * src_width / dst_width);
        if (height < 1) height = 1;
        if (height > src_height) height = src_height;
        crop.height = height;
        crop.y = (src_height - height) / 2;
    } else {
        // 高边受限：占满整高，左右居中裁。/ Height-bound: keep the full height, crop left and right.
        unsigned width = (unsigned)((uint64_t)dst_width * src_height / dst_height);
        if (width < 1) width = 1;
        if (width > src_width) width = src_width;
        crop.width = width;
        crop.x = (src_width - width) / 2;
    }
    return crop;
}

/* ---- JPEG 帧头 / JPEG frame header ---- */

// 读 SOF 标记与帧尺寸，只走标记段、不解码。
// 0xC0 是基线，0xC2 是渐进式；ROM TJpgDec 只解基线，所以渐进式要交给 JPEGDEC。
// Walk the marker segments and read the SOF marker plus the frame size. 0xC0 is baseline and
// 0xC2 is progressive; the ROM TJpgDec decodes baseline only, so progressive frames go to
// JPEGDEC.


// 把整幅 RGB565 重采样到目标灰阶：长边铺满、居中裁剪，再双线性。
// Resample a full RGB565 frame into the target gray: fill the longer side, centre-crop, then bilinear.
static bool resample_rgb565(const uint8_t *pixels, unsigned stride, unsigned width,
                            unsigned height, unsigned out_width, unsigned out_height,
                            uint8_t *out) {
    if (!pixels || !out || !stride || !width || !height) return false;
    const book_crop_t crop = book_cover_crop(width, height, out_width, out_height);
    scale_step_t *xmap = scale_map(out_width, crop.width);
    scale_step_t *ymap = scale_map(out_height, crop.height);
    bool ok = false;
    if (xmap && ymap) {
        for (unsigned y = 0; y < out_height; ++y) {
            const int sy = ymap[y].at, fy = ymap[y].frac;
            const int sy1 = scale_next(sy, crop.height);
            const uint8_t *r0 = pixels + (size_t)(crop.y + (unsigned)sy) * stride * 2;
            const uint8_t *r1 = pixels + (size_t)(crop.y + (unsigned)sy1) * stride * 2;
            for (unsigned x = 0; x < out_width; ++x) {
                const int sx = xmap[x].at, fx = xmap[x].frac;
                const int sx1 = scale_next(sx, crop.width);
                out[y * out_width + x] = bilinear4(
                    rgb565_gray(r0, crop.x + (unsigned)sx), rgb565_gray(r0, crop.x + (unsigned)sx1),
                    rgb565_gray(r1, crop.x + (unsigned)sx), rgb565_gray(r1, crop.x + (unsigned)sx1),
                    fx, fy);
            }
            decode_yield(y);
        }
        ok = true;
    }
    free(xmap);
    free(ymap);
    return ok;
}

// 单字节灰阶平面同样按长边铺满 + 居中裁剪；硬件灰度 JPEG 与渐进式 1/8 平面共用。
// The same aspect-fill + centre crop for an 8-bit gray plane; shared by hardware gray JPEGs and progressive 1/8 planes.
static bool resample_gray8(const uint8_t *pixels, unsigned stride, unsigned width,
                           unsigned height, unsigned out_width, unsigned out_height,
                           uint8_t *out) {
    if (!pixels || !out || !stride || !width || !height) return false;
    const book_crop_t crop = book_cover_crop(width, height, out_width, out_height);
    scale_step_t *xmap = scale_map(out_width, crop.width);
    scale_step_t *ymap = scale_map(out_height, crop.height);
    bool ok = false;
    if (xmap && ymap) {
        for (unsigned y = 0; y < out_height; ++y) {
            const int sy = ymap[y].at, fy = ymap[y].frac;
            const int sy1 = scale_next(sy, crop.height);
            const uint8_t *r0 = pixels + (size_t)(crop.y + (unsigned)sy) * stride;
            const uint8_t *r1 = pixels + (size_t)(crop.y + (unsigned)sy1) * stride;
            for (unsigned x = 0; x < out_width; ++x) {
                const int sx = xmap[x].at, fx = xmap[x].frac;
                const int sx1 = scale_next(sx, crop.width);
                out[y * out_width + x] = bilinear4(
                    r0[crop.x + (unsigned)sx], r0[crop.x + (unsigned)sx1],
                    r1[crop.x + (unsigned)sx], r1[crop.x + (unsigned)sx1], fx, fy);
            }
            decode_yield(y);
        }
        ok = true;
    }
    free(xmap);
    free(ymap);
    return ok;
}

/* ---- 渐进式 JPEG / Progressive JPEG ---- */

// 渐进式 JPEG 的 DC-only 1/8 路径会把 1px 细节抹成灰团；stb_image 能按全分辨率解
// SOF2，输出单通道灰度（req_comp=1）再走同一套双线性重采样，插图/图表的细线恢复。
// The DC-only 1/8 progressive path smears 1 px detail; stb_image decodes SOF2 at full
// resolution. It emits single-channel gray (req_comp=1) which feeds the same bilinear
// resampler, so thin chart lines survive.
static bool stb_progressive_gray(const uint8_t *data, size_t size,
                                 unsigned out_width, unsigned out_height, uint8_t *out) {
    if (!data || size > INT32_MAX || !out || !out_width || !out_height) return false;
    // 先只读头拿源尺寸；超过一屏级预算就不让 stb 整幅解码，避免吃光 PSRAM 再回退。
    // Read the header first; over a screen-scale budget we refuse the full-frame decode
    // rather than exhaust PSRAM and then fall back anyway.
    int sw = 0, sh = 0, comp = 0;
    if (!stbi_info_from_memory(data, (int)size, &sw, &sh, &comp) || sw <= 0 || sh <= 0) return false;
    const uint64_t pixels = (uint64_t)sw * (uint64_t)sh;
    if (pixels > STBI_JPEG_PIXEL_BUDGET) return false;
    int out_w = 0, out_h = 0, out_c = 0;
    uint8_t *gray = stbi_load_from_memory(data, (int)size, &out_w, &out_h, &out_c, 1);
    if (!gray) return false;
    const bool ok = resample_gray8(gray, (unsigned)out_w, (unsigned)out_w,
                                   (unsigned)out_h, out_width, out_height, out);
    stbi_image_free(gray);
    return ok;
}

// EIGHT_BIT_GRAYSCALE 下 pPixels 每像素一个字节。JPEGDEC 按 MCU 块回调：块内 stride 是
// iWidth，只有 iWidthUsed 列有效，坐标落在 JPEGDEC 缩放后的空间里。
// Under EIGHT_BIT_GRAYSCALE pPixels holds one byte per pixel. JPEGDEC calls back per MCU
// block: the stride inside a block is iWidth, only iWidthUsed columns are valid, and the
// coordinates live in JPEGDEC's reduced space.
typedef struct {
    uint8_t *plane;
    unsigned stride, height;
} jpegdec_plane_t;

static int jpegdec_collect(JPEGDRAW *draw) {
    jpegdec_plane_t *plane = draw->pUser;
    if (!plane || !plane->plane || draw->iBpp != 8 || !draw->pPixels || draw->iWidth < 1) return 0;
    const uint8_t *src = (const uint8_t *)draw->pPixels;
    const int columns = draw->iWidthUsed < draw->iWidth ? draw->iWidthUsed : draw->iWidth;
    for (int row = 0; row < draw->iHeight; ++row) {
        size_t y = (size_t)draw->y + row;
        if (draw->y + row < 0 || y >= plane->height) break;
        for (int column = 0; column < columns; ++column) {
            if (draw->x + column < 0) continue;
            size_t x = (size_t)draw->x + column;
            if (x >= plane->stride) continue;
            plane->plane[y * plane->stride + x] = src[(size_t)row * draw->iWidth + column];
        }
    }
    return 1;
}

// ESP-IDF 下 JPEGDEC 只提供 C++ 类，组件里有一层 extern "C" 包装。
// Under ESP-IDF JPEGDEC only offers the C++ class; the component wraps it with C linkage.
extern int jpegdec_gray_progressive(const uint8_t *data, int size, JPEG_DRAW_CALLBACK *draw,
                                    void *user, int *drawn_width, int *drawn_height);

// 渐进式只取 DC 扫描，JPEGDEC 因此固定输出 1/8 分辨率；平面尺寸由帧头算出，
// 与小平面一起缩到目标尺寸。对封面（176×240）来说已经足够，也避开了整幅系数数组。
// Progressive decoding reads the DC scan only, so JPEGDEC always emits 1/8 resolution. The plane
// size comes from the frame header and is resampled to the requested size: plenty for a 176x240
// cover, and it never holds the full coefficient array.
static bool jpegdec_gray(const uint8_t *data, size_t size, unsigned frame_width,
                         unsigned frame_height, unsigned out_width, unsigned out_height,
                         uint8_t *out) {
    if (!data || size < 4 || size > INT32_MAX || !frame_width || !frame_height) return false;
    jpegdec_plane_t plane = {
        .stride = (frame_width + 7) / 8,
        .height = (frame_height + 7) / 8,
    };
    if (plane.stride > 4096 || plane.height > 4096) return false;
    plane.plane = cover_alloc((size_t)plane.stride * plane.height);
    if (!plane.plane) return false;
    memset(plane.plane, 0xFF, (size_t)plane.stride * plane.height);
    const bool decoded = jpegdec_gray_progressive(data, (int)size, jpegdec_collect, &plane,
                                                 NULL, NULL) != 0;
    bool ok = false;
    if (decoded) {
        // 1/8 小平面按长边铺满 + 居中裁剪映射到目标。
        // The 1/8 plane maps to the target the same way: fill the longer side, centre-crop.
        ok = resample_gray8(plane.plane, plane.stride, plane.stride, plane.height,
                            out_width, out_height, out);
    }
    free(plane.plane);
    return ok;
}

static bool jpeg_gray(const uint8_t *data, size_t size, unsigned out_width,
                      unsigned out_height, uint8_t *out) {
    if (size > UINT32_MAX) return false;
    uint8_t sof = 0;
    unsigned frame_width = 0, frame_height = 0;
    const bool framed = book_jpeg_frame(data, size, &sof, &frame_width, &frame_height);
    if (framed && sof == 0xC2)
        return stb_progressive_gray(data, size, out_width, out_height, out)
            || jpegdec_gray(data, size, frame_width, frame_height, out_width, out_height, out);
#if BOOK_JPEG_HW
    // 基线 JPEG 先试 S31 硬件整幅解码；失败或超预算时继续走下面的软件 1/1…1/8 路径。
    // Try the S31 hardware full-frame decode for baseline JPEGs; any failure or budget
    // overrun continues into the software 1/1...1/8 path below.
    if (framed && sof == 0xC0) {
        book_jpeg_hw_image_t image;
        if (book_jpeg_hw_decode(data, size, 8u * 1024u * 1024u, &image)) {
            const bool ok = image.gray
                ? resample_gray8(image.pixels, image.stride, image.width, image.height,
                                 out_width, out_height, out)
                : resample_rgb565(image.pixels, image.stride, image.width, image.height,
                                  out_width, out_height, out);
            book_jpeg_hw_free(&image);
            if (ok) return true;
        }
    }
#endif
    esp_jpeg_image_cfg_t cfg = { .indata = (uint8_t *)data, .indata_size = (uint32_t)size,
                                 .out_format = JPEG_IMAGE_FORMAT_RGB565 };
    esp_jpeg_image_output_t info = {0};
    // 保留原有小图输出的解码预算分界。/ Preserve the existing small-output decode budget threshold.
    size_t limit = out_width <= 176u && out_height <= 240u
        ? 3u * 1024u * 1024u : 6u * 1024u * 1024u;
    bool decoded = false;
    for (int scale = JPEG_IMAGE_SCALE_0; scale <= JPEG_IMAGE_SCALE_1_8; ++scale) {
        cfg.out_scale = scale;
        if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) return false;
        if (!info.width || !info.height || !info.output_len || info.output_len > limit) continue;
        uint8_t *pixels = cover_alloc(info.output_len);
        if (!pixels) continue;
        cfg.outbuf = pixels;
        cfg.outbuf_size = info.output_len;
        // 长边铺满再居中裁剪，长宽比不同也不拉伸。
        // Fill by the longer side and centre-crop, so a different aspect ratio is not stretched.
        if (esp_jpeg_decode(&cfg, &info) == ESP_OK &&
            resample_rgb565(pixels, info.width, info.width, info.height,
                            out_width, out_height, out))
            decoded = true;
        free(pixels);
        if (decoded) break;
    }
    return decoded;
}

typedef struct { const uint8_t *data; size_t size, pos; FILE *file; } png_input_t;
static void png_read_mem(png_structp png, png_bytep dst, png_size_t count) {
    png_input_t *input = png_get_io_ptr(png);
    if (count > input->size - input->pos) png_error(png, "truncated PNG");
    if (input->file) { if (fread(dst, 1, count, input->file) != count) png_error(png, "truncated PNG file"); }
    else memcpy(dst, input->data + input->pos, count);
    input->pos += count;
}
static bool png_gray_input(const uint8_t *data, size_t size, FILE *file, unsigned out_width,
                     unsigned out_height, uint8_t *out, uint8_t *alpha) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return false; }
    uint8_t *volatile row = NULL;
    // 双线性要相邻两行，所以窗口内留两行灰度：行号按奇偶分别落在两个缓冲里。
    // Bilinear needs two adjacent rows, so two window rows of gray are kept, indexed by row parity.
    //
    // 这三个必须使用 volatile 并声明在 setjmp 之前：goto done 会跳过 setjmp 之后的初始化，在那里 free()
    // 未初始化的指针会崩。
    // These must be volatile and declared before setjmp: goto done skips the initialisers that follow it, and
    // freeing an uninitialised pointer there would crash.
    scale_step_t *volatile xmap = NULL, *volatile ymap = NULL;
    uint8_t *volatile rows[2] = {NULL, NULL};
    uint8_t *volatile masks[2] = {NULL, NULL};
    png_input_t input = {data, size, 0, file};
    bool ok = false;
    if (setjmp(png_jmpbuf(png))) goto done;
    png_set_read_fn(png, &input, png_read_mem);
    png_read_info(png, info);
    png_uint_32 width = png_get_image_width(png, info), height = png_get_image_height(png, info);
    if (!width || !height || width > 8192 || height > 8192 ||
        png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) goto done;
    int color = png_get_color_type(png, info), depth = png_get_bit_depth(png, info);
    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(color & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    png_size_t stride = png_get_rowbytes(png, info);
    if (stride < width * 4 || stride > 65536) goto done;
    row = cover_alloc(stride);
    if (!row) goto done;
    unsigned next_y = 0;
    // 流式读取时裁剪窗口同时作用于行与列：窗口外的行直接丢弃，列按窗口取样。
    // The streaming reader applies the crop window to rows and columns alike: rows outside the
    // window are dropped and columns are sampled inside it.
    const book_crop_t crop = book_cover_crop(width, height, out_width, out_height);
    if (crop.width && crop.height) {
        xmap = scale_map(out_width, crop.width);
        ymap = scale_map(out_height, crop.height);
        rows[0] = cover_alloc(crop.width);
        rows[1] = cover_alloc(crop.width);
        if (alpha) { masks[0] = cover_alloc(crop.width); masks[1] = cover_alloc(crop.width); }
    }
    if (xmap && ymap && rows[0] && rows[1] && (!alpha || (masks[0] && masks[1]))) {
        for (png_uint_32 y = 0; y < height; ++y) {
            png_read_row(png, row, NULL);
            decode_yield((unsigned)y);
            const int wy = (int)y - (int)crop.y;
            if (wy < 0 || wy >= (int)crop.height) continue;
            uint8_t *cur = rows[wy & 1];
            for (unsigned x = 0; x < crop.width; ++x) {
                const uint8_t *p = row + (size_t)(crop.x + x) * 4;
                const uint8_t gray = luminance(p[0], p[1], p[2]);
                // 透明层保留预乘灰度与独立alpha，避免缩放后出现白边。/ Preserve premultiplied gray and alpha for layers, avoiding white scaling fringes.
                cur[x] = (uint8_t)(((unsigned)gray * p[3] + (alpha ? 0 : 255u * (255u - p[3])) + 127u) / 255u);
                if (alpha) masks[wy & 1][x] = p[3];
            }
            // 两行都齐了的目标行可以一次输出完：同一源行可能对应多个目标行（放大时）。
            // Every destination row whose two source rows are now present can be emitted: one source
            // row may serve several destination rows when scaling up.
            while (next_y < out_height) {
                const int sy = ymap[next_y].at, fy = ymap[next_y].frac;
                const int sy1 = scale_next(sy, crop.height);
                if (wy < sy1) break;
                const uint8_t *r0 = rows[sy & 1], *r1 = rows[sy1 & 1];
                for (unsigned x = 0; x < out_width; ++x) {
                    const int sx = xmap[x].at, fx = xmap[x].frac;
                    const int sx1 = scale_next(sx, crop.width);
                    out[next_y * out_width + x] = bilinear4(r0[sx], r0[sx1], r1[sx], r1[sx1], fx, fy);
                    if (alpha) alpha[next_y * out_width + x] = bilinear4(masks[sy & 1][sx], masks[sy & 1][sx1],
                        masks[sy1 & 1][sx], masks[sy1 & 1][sx1], fx, fy);
                }
                ++next_y;
            }
        }
    }
    ok = next_y == out_height;
done:
    free(xmap);
    free(ymap);
    free(rows[0]);
    free(rows[1]);
    free((void *)masks[0]); free((void *)masks[1]);
    free((void *)row);
    png_destroy_read_struct(&png, &info, NULL);
    return ok;
}

bool book_image_grayscale(const uint8_t *data, size_t size, bool png,
                          unsigned out_width, unsigned out_height, uint8_t *out) {
    if (!data || !size || !out || !out_width || !out_height ||
        out_width > 1216 || out_height > 1216 || (size_t)out_width * out_height > 684u * 1216u)
        return false;
    if (size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4))
        return book_webp_grayscale(data, size, out_width, out_height, out);
    return png ? png_gray_input(data, size, NULL, out_width, out_height, out, NULL)
               : jpeg_gray(data, size, out_width, out_height, out);
}



static bool file_frame(FILE *file, bool png, uint8_t *sof, unsigned *width, unsigned *height) {
    if (fseek(file, 0, SEEK_SET)) return false;
    if (png) {
        uint8_t header[24];
        return fread(header, 1, sizeof(header), file) == sizeof(header) && book_image_dimensions(header, sizeof(header), true, width, height);
    }
    if (fgetc(file) != 0xff || fgetc(file) != 0xd8) return false;
    for (unsigned segment = 0; segment < 4096; ++segment) {
        int first = fgetc(file);
        if (first == EOF) return false;
        if (first != 0xff) continue;
        int marker;
        do { marker = fgetc(file); } while (marker == 0xff);
        if (marker == EOF || marker == 0xda || marker == 0xd9) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) continue;
        int high = fgetc(file), low = fgetc(file);
        if (high < 0 || low < 0) return false;
        unsigned length = (unsigned)high * 256 + (unsigned)low;
        if (length < 2) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            uint8_t frame[5];
            if (length < 7 || fread(frame, 1, sizeof(frame), file) != sizeof(frame)) return false;
            *height = (unsigned)frame[1] * 256 + frame[2];
            *width = (unsigned)frame[3] * 256 + frame[4];
            if (sof) *sof = (uint8_t)marker;
            return *width && *height && *width <= 8192 && *height <= 8192 && frame[0] == 8;
        }
        if (fseek(file, (long)length - 2, SEEK_CUR)) return false;
    }
    return false;
}
bool book_image_file_dimensions(const char *path, bool png, unsigned *width, unsigned *height) {
    if (!path || !width || !height) return false;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    *width = *height = 0;
    bool ok = file_frame(file, png, NULL, width, height);
    fclose(file);
    return ok;
}
typedef struct {
    uint8_t *out;
    unsigned width, height, source_w, source_h;
    size_t pixels;
    unsigned blocks;
    FILE *file;
} jpeg_file_plane_t;
static int jpeg_file_collect(JPEGDRAW *draw) {
    jpeg_file_plane_t *plane = draw->pUser;
    if (!plane || !draw->pPixels || draw->iBpp != 8 || draw->iWidth <= 0 || draw->iHeight <= 0 || draw->x < 0 || draw->y < 0) return 0;
    book_crop_t crop = book_cover_crop(plane->source_w, plane->source_h, plane->width, plane->height);
    const uint8_t *block = (const uint8_t *)draw->pPixels;
    // 只绘制映射到当前 MCU 的目标采样点；输出最多一屏，不创建源图平面。
    // Sample only destination pixels mapped into this MCU, without a source-sized plane.
    decode_yield(++plane->blocks);
    unsigned y0 = (unsigned)draw->y > crop.y ? ((uint64_t)(draw->y - crop.y) * plane->height + crop.height - 1) / crop.height : 0;
    unsigned y1 = (unsigned)(draw->y + draw->iHeight) > crop.y ? ((uint64_t)(draw->y + draw->iHeight - crop.y) * plane->height + crop.height - 1) / crop.height : 0;
    unsigned x0 = (unsigned)draw->x > crop.x ? ((uint64_t)(draw->x - crop.x) * plane->width + crop.width - 1) / crop.width : 0;
    unsigned x1 = (unsigned)(draw->x + draw->iWidthUsed) > crop.x ? ((uint64_t)(draw->x + draw->iWidthUsed - crop.x) * plane->width + crop.width - 1) / crop.width : 0;
    if (y1 > plane->height) y1 = plane->height;
    if (x1 > plane->width) x1 = plane->width;
    for (unsigned y = y0; y < y1; ++y) {
        unsigned sy = crop.y + (uint64_t)y * crop.height / plane->height;
        if (sy < (unsigned)draw->y || sy >= (unsigned)(draw->y + draw->iHeight)) continue;
        for (unsigned x = x0; x < x1; ++x) {
            unsigned sx = crop.x + (uint64_t)x * crop.width / plane->width;
            if (sx < (unsigned)draw->x || sx >= (unsigned)(draw->x + draw->iWidthUsed)) continue;
            plane->out[(size_t)y * plane->width + x] = block[(size_t)(sy - draw->y) * draw->iWidth + sx - draw->x];
            ++plane->pixels;
        }
    }
    return 1;
}
static stream_jpeg_size_t jpeg_stream_read(JDEC *decoder, uint8_t *bytes, stream_jpeg_size_t count) {
    jpeg_file_plane_t *plane = decoder->device;
    if (!plane || !plane->file) return 0;
    if (!bytes) return fseek(plane->file, (long)count, SEEK_CUR) == 0 ? count : 0;
    return (stream_jpeg_size_t)fread(bytes, 1, count, plane->file);
}
static stream_jpeg_result_t jpeg_stream_collect(JDEC *decoder, void *bitmap, JRECT *rect) {
    jpeg_file_plane_t *plane = decoder->device;
    unsigned width = rect->right - rect->left + 1, height = rect->bottom - rect->top + 1;
    if (!width || !height || width * height > 256) return 0;
    uint8_t gray[256];
    const uint8_t *rgb = bitmap;
    for (unsigned i = 0; i < width * height; ++i) gray[i] = luminance(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    JPEGDRAW draw = {.x = rect->left, .y = rect->top, .iWidth = (int)width, .iHeight = (int)height,
        .iWidthUsed = (int)width, .iBpp = 8, .pPixels = (uint16_t *)gray, .pUser = plane};
    return (stream_jpeg_result_t)jpeg_file_collect(&draw);
}
static bool jpeg_baseline_file(FILE *file, unsigned source_w, unsigned source_h,
                               unsigned width, unsigned height, jpeg_file_plane_t *plane) {
    unsigned scale = 0;
    while (scale < 3 && source_w / (2u << scale) >= width && source_h / (2u << scale) >= height) ++scale;
    plane->file = file; plane->source_w = source_w >> scale; plane->source_h = source_h >> scale;
    void *workspace = cover_alloc(4096);
    if (!workspace) return false;
    JDEC decoder;
    bool ok = jd_prepare(&decoder, jpeg_stream_read, workspace, 4096, plane) == JDR_OK &&
              jd_decomp(&decoder, jpeg_stream_collect, (uint8_t)scale) == JDR_OK;
    free(workspace);
    return ok;
}
extern int jpegdec_gray_file(FILE *, int, unsigned, unsigned, unsigned, unsigned, bool,
                             JPEG_DRAW_CALLBACK *, void *, unsigned *, unsigned *);
bool book_image_file_grayscale(const char *path, bool png, unsigned width, unsigned height, uint8_t *out) {
    if (!path || !out || !width || !height || width > 1216 || height > 1216 || (uint64_t)width * height > 684u * 1216u) return false;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = false;
    if (png) ok = png_gray_input(NULL, (size_t)st.st_size, file, width, height, out, NULL);
    else {
        uint8_t sof = 0;
        unsigned source_w = 0, source_h = 0;
        if (file_frame(file, false, &sof, &source_w, &source_h) && !fseek(file, 0, SEEK_SET)) {
            jpeg_file_plane_t plane = {.out = out, .width = width, .height = height};
            memset(out, 255, (size_t)width * height);
            if (sof == 0xc2)
                ok = jpegdec_gray_file(file, (int)st.st_size, source_w, source_h, width, height, true,
                    jpeg_file_collect, &plane, &plane.source_w, &plane.source_h) != 0;
            else ok = jpeg_baseline_file(file, source_w, source_h, width, height, &plane);
            ok = ok && plane.pixels >= (size_t)width * height;
        }
    }
    fclose(file);
    return ok;
}

bool book_png_file_layer(const char *path, unsigned width, unsigned height, uint8_t *gray, uint8_t *alpha) {
    struct stat st;
    if (!path || !gray || !alpha || !width || !height || width > 684 || height > 1216 ||
        stat(path, &st) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = png_gray_input(NULL, (size_t)st.st_size, file, width, height, gray, alpha);
    fclose(file);
    return ok;
}
