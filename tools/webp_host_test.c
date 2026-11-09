/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：实际 WebP 公共图像出口、EPUB 开书与所有引用图片的宿主回归。
 * English: Regress real WebP image entry points, EPUB opening and every referenced image.
 */
#include "book_epub.h"
#include "image_decode.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "webp/decode.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef free

static struct { void *ptr; size_t size; } allocations[4096];
static size_t live, peak, max_block;
static int fail_after = -1;
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
void *test_heap_malloc(size_t size) {
    assert(!pthread_mutex_lock(&allocation_lock));
    if (fail_after == 0) { assert(!pthread_mutex_unlock(&allocation_lock)); return NULL; }
    if (fail_after > 0) --fail_after;
    void *ptr = malloc(size);
    if (ptr) {
        size_t i = 0;
        while (i < 4096 && allocations[i].ptr) ++i;
        assert(i < 4096);
        allocations[i].ptr = ptr; allocations[i].size = size;
        live += size;
        if (live > peak) peak = live;
        if (size > max_block) max_block = size;
    }
    assert(!pthread_mutex_unlock(&allocation_lock));
    return ptr;
}
void *test_heap_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    void *ptr = test_heap_malloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}
void test_heap_free(void *ptr) {
    if (!ptr) return;
    assert(!pthread_mutex_lock(&allocation_lock));
    for (size_t i = 0; i < 4096; ++i) if (allocations[i].ptr == ptr) {
        live -= allocations[i].size; allocations[i].ptr = NULL; break;
    }
    assert(!pthread_mutex_unlock(&allocation_lock));
    free(ptr);
}
void *test_heap_realloc(void *ptr, size_t size) {
    if (!ptr) return test_heap_malloc(size);
    size_t old = 0;
    assert(!pthread_mutex_lock(&allocation_lock));
    for (size_t i = 0; i < 4096; ++i) if (allocations[i].ptr == ptr) old = allocations[i].size;
    assert(!pthread_mutex_unlock(&allocation_lock));
    assert(old);
    void *next = test_heap_malloc(size);
    if (next) { memcpy(next, ptr, old < size ? old : size); test_heap_free(ptr); }
    return next;
}
int ttf_font_open_mem(uint8_t *data, size_t size, const char *label) {
    (void)size; (void)label; test_heap_free(data); return -1;
}
void ttf_font_close_embedded(void) {}
esp_err_t esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    (void)cfg; (void)info; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t esp_jpeg_decode(esp_jpeg_image_cfg_t *cfg, esp_jpeg_image_output_t *info) {
    (void)cfg; (void)info; return ESP_ERR_NOT_SUPPORTED;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long n = ftell(file); assert(n > 0);
    rewind(file); uint8_t *data = malloc((size_t)n); assert(data);
    assert(fread(data, 1, (size_t)n, file) == (size_t)n);
    fclose(file); *size = (size_t)n; return data;
}

static void check_webp(const char *path) {
    size_t size = 0; uint8_t *data = read_file(path, &size);
    unsigned w = 0, h = 0; uint8_t out[64 + 16];
    assert(book_image_dimensions(data, size, false, &w, &h));
    assert(w == 17 && h == 13);
    for (unsigned limit = 0; limit < 30 && limit < size; ++limit) {
        assert(!book_image_grayscale(data, limit, false, 8, 8, out)); assert(!live);
    }
    memset(out, 0xa5, sizeof(out));
    assert(book_image_grayscale(data, size, false, 8, 8, out)); assert(!live);
    for (unsigned i = 64; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    unsigned expected = strstr(path, "alpha") ? 191 : 128;
    for (unsigned i = 0; i < 64; ++i) assert(abs((int)out[i] - (int)expected) <= 2);
    assert(book_image_grayscale(data, size, false, 1, 1, out)); assert(!live);
    assert(!book_image_grayscale(data, size, false, 1216, 1216, out));
    assert(!book_image_grayscale(data, size, false, 0, 1, out));
    assert(!book_image_grayscale(data, size / 2, false, 8, 8, out)); assert(!live);
    bool completed = false;
    for (int i = 0; i < 100; ++i) {
        fail_after = i;
        bool ok = book_image_grayscale(data, size, false, 8, 8, out);
        fail_after = -1; assert(!live);
        if (ok) { completed = true; break; }
    }
    assert(completed);
    free(data);
    printf("WebP decode, alpha, scaling, truncation, OOM and guards passed: %s\n", path);
}

static void check_epub(const char *path) {
    book_epub_t *book = NULL;
    esp_err_t err = book_epub_open(path, &book);
    printf("EPUB open status=%d: %s\n", err, path); fflush(stdout);
    assert(err == ESP_OK && book);
    book_epub_set_fonts_enabled(book, false);
    size_t chapters = book_epub_chapter_count(book), images = 0, webps = 0;
    assert(chapters && book_epub_navigation_count(book));
    for (size_t i = 0; i < chapters; ++i) {
        html_text_t text = {0}; assert(book_epub_load(book, i, &text) == ESP_OK);
        for (size_t j = 0; j < text.image_count; ++j) {
            uint8_t *image = NULL; size_t bytes = 0; bool png = false;
            err = book_epub_image(book, i, text.images[j], &image, &bytes, &png);
            if (err != ESP_OK) fprintf(stderr, "chapter=%zu image=%s status=%d\n", i, text.images[j], err);
            assert(err == ESP_OK);
            unsigned w = 0, h = 0;
            assert(book_epub_image_dimensions(book, i, text.images[j], &w, &h) == ESP_OK && w && h);
            if (bytes >= 12 && !memcmp(image, "RIFF", 4)) {
                uint8_t pixels[176 * 240];
                assert(book_image_grayscale(image, bytes, png, 176, 240, pixels));
                ++webps;
            }
            test_heap_free(image); ++images;
        }
        html_text_free(&text);
    }
    book_epub_close(book);
    uint8_t *cover = NULL; size_t bytes = 0; bool png = false;
    err = book_epub_cover(path, &cover, &bytes, &png);
    if (err == ESP_OK) {
        if (bytes >= 12 && !memcmp(cover, "RIFF", 4)) {
            uint8_t pixels[176 * 240];
            assert(book_image_grayscale(cover, bytes, png, 176, 240, pixels));
        }
        test_heap_free(cover);
    } else assert(err == ESP_ERR_NOT_FOUND);
    zip_reader_t *zip = NULL; assert(zip_open(path, &zip) == ESP_OK);
    size_t assets = 0;
    for (size_t i = 0; i < zip_entry_count(zip); ++i) {
        const char *name = zip_entry_name(zip, (int)i);
        if (!strstr(name, ".webp")) continue;
        size_t size = zip_entry_size(zip, (int)i);
        uint8_t *data = malloc(size); assert(data && zip_extract(zip, (int)i, data, size) == ESP_OK);
        WebPBitstreamFeatures features;
        assert(WebPGetFeatures(data, size, &features) == VP8_STATUS_OK);
        unsigned w = 0, h = 0;
        assert(book_image_dimensions(data, size, false, &w, &h));
        assert(w == (unsigned)features.width && h == (unsigned)features.height);
        size_t output_size = 684u * 1216u;
        uint8_t *pixels = malloc(output_size + 16); assert(pixels);
        memset(pixels, 0xa5, output_size + 16);
        assert(book_image_grayscale(data, size, false, 684, 1216, pixels));
        for (unsigned j = 0; j < 16; ++j) assert(pixels[output_size + j] == 0xa5);
        free(pixels); free(data); ++assets;
    }
    zip_close(zip);
    assert(!live);
    printf("EPUB passed: %zu chapters, %zu images, %zu WebP references, %zu WebP assets, cover status=%d, peak=%zu max_block=%zu\n",
        chapters, images, webps, assets, err, peak, max_block);
}

static void *concurrent_decode(void *context) {
    const char *path = context;
    size_t size = 0;
    uint8_t *data = read_file(path, &size), out[64];
    for (unsigned i = 0; i < 20; ++i)
        assert(book_image_grayscale(data, size, false, 8, 8, out));
    free(data);
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc >= 3);
    if (!strcmp(argv[1], "concurrent")) {
        pthread_t threads[4];
        assert(argc == 6);
        for (unsigned i = 0; i < 4; ++i)
            assert(!pthread_create(&threads[i], NULL, concurrent_decode, argv[i + 2]));
        for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
        assert(!live);
        puts("PASS: concurrent lossy/lossless/alpha initialization and decoding");
    } else if (!strcmp(argv[1], "epub")) {
        for (int i = 2; i < argc; ++i) check_epub(argv[i]);
    } else if (!strcmp(argv[1], "reject")) {
        for (int i = 2; i < argc; ++i) {
            size_t size = 0; uint8_t *data = read_file(argv[i], &size), out[64];
            unsigned w = 99, h = 99;
            assert(!book_image_dimensions(data, size, false, &w, &h) && !w && !h);
            assert(!book_image_grayscale(data, size, false, 8, 8, out));
            assert(!live); free(data);
        }
    } else for (int i = 2; i < argc; ++i) check_webp(argv[i]);
    return 0;
}
