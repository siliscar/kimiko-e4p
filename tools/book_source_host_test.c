/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：以真实文件验证书源解析。
 * English: Exercise source parsing with real files.
 * 冻结：仅供主机验证。/ Frozen: Host validation only.
 */
#include "book_source.h"
#include "reader_session.h"
#include "gbk.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 字体引擎的宿主替身：一律拒绝，书内字体在书源测试里回退系统字体。
// 真实引擎在装载失败时也会释放调用方交出的那块内存，这里保持一致。
// Host stand-in for the font engine: always declines, so embedded faces fall back to the system
// face under test. The real engine also frees the bytes handed to it when a load fails.
int ttf_font_open_mem(uint8_t* data, size_t len, const char* label) {
    (void)len; (void)label;
    free(data);
    return -1;
}
void ttf_font_close_embedded(void) {}
static const char *path = "/tmp/book-source-test.txt";
static void write_bytes(const char *s, size_t n) {
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(s, 1, n, f) == n); fclose(f);
}
static void roundtrip(const char *s, size_t n) {
    size_t seen = 0; char *out; size_t len;
    assert(book_open(path) == ESP_OK); assert(book_total_bytes() == n);
    for (size_t i = 0; i < book_chapter_count(); ++i) {
        assert(book_chapter_byte_offset(i) == seen);
        assert(book_chapter_load(i, &out, &len) == ESP_OK);
        assert(len <= 96 * 1024); assert(memcmp(out, s + seen, len) == 0);
        assert(out[len] == 0); seen += len; free(out);
    }
    assert(seen == n); book_close();
}
static void session_roundtrip(const char *source) {
    reader_session_t session = {0}, other = {0};
    assert(reader_session_open(&session, "/tmp/missing.unknown") == ESP_ERR_NOT_SUPPORTED);
    assert(!reader_session_has_source(&session));
    assert(reader_session_open(&session, source) == ESP_OK);
    size_t chapters = book_chapter_count();
    assert(chapters && reader_session_has_source(&session));
    assert(reader_session_open(&other, source) == ESP_ERR_INVALID_STATE);
    assert(book_chapter_count() == chapters);
    for (size_t i = 0; i < chapters; ++i) {
        html_text_t next = {0};
        assert(book_chapter_load_blocks(i, &next) == ESP_OK);
        char *text = next.utf8; size_t bytes = next.len;
        reader_session_adopt_chapter(&session, &next);
        assert(next.utf8 == NULL && next.images == NULL);
        assert(reader_session_content(&session)->utf8 == text);
        assert(reader_session_content(&session)->len == bytes);
    }
    const char *held = reader_session_content(&session)->utf8;
    html_text_t failed = {0};
    assert(book_chapter_load_blocks(chapters, &failed) != ESP_OK);
    html_text_free(&failed);
    assert(reader_session_content(&session)->utf8 == held);
    reader_session_close(&other);
    assert(book_chapter_count() == chapters);
    reader_session_close(&session); reader_session_close(&session);
    assert(!reader_session_has_source(&session));
    assert(book_kind() == BOOK_KIND_TXT && book_chapter_count() == 0);
}
// 长标题、缩进和连续重复题头不能使整本书退回 Part。/ Long, indented and repeated headings must not force a whole-book Part fallback.
static void txt_heading_regressions(void) {
    const char *first = "第二十九篇 第三十五章 原始宇宙的末期";
    const char *last = "第二十九篇 第三十六章 超脱轮回（大结局）下";
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "书前说明\r\n\t%s  \r\n", first);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 20000; ++j) fputc('x', f);
        fprintf(f, "\r\n　　第%d章 中间章节\r\n　　第%d章 中间章节\r\n正文\r\n", i + 1, i + 1);
    }
    fprintf(f, "　%s\r\n正文", last); fclose(f);
    assert(book_open(path) == ESP_OK);
    assert(book_chapter_count() == 5);
    char title[256];
    assert(book_chapter_title(0, title, sizeof(title)) == ESP_OK && !strcmp(title, first));
    assert(book_chapter_title(4, title, sizeof(title)) == ESP_OK && !strcmp(title, last));
    assert(book_chapter_byte_offset(0) == 0);
    book_close();
    const char *s = "第1章 择徒\n　　第1章 择徒\n正文\n第2章 火虎牙\n　　第2章 火虎牙\n正文\n";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 2); book_close();
    s = "第一章 单章\n正文\n";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK);
    assert(book_chapter_title(0, title, sizeof(title)) == ESP_OK && !strcmp(title, "第一章 单章"));
    book_close();
    s = "第1章 开始\n正文\n第1章 开始\n另一段正文\n第2章 结束";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 3); book_close();
    s = "第一章 开始\n正文\n第一部分乃是修行之法。\n第九集团军，很强。\n第一章到！今天继续更新。\n第二章 结束\n正文";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 2); book_close();
    s = "第一篇 一夜觉醒 第一集 深夜觉醒\n第一章 罗峰\n正文\n第二篇 战神罗峰 第一章 三个月的修炼\n　　第二篇战神罗峰第一章三个月的修炼\n　　第一章 三个月的修炼\n正文";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 3); book_close();
    s = "  chapter 1 Start\rtext\r\tCHAPTER 2 End\rtext";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 2); book_close();
    s = "第〇一章 开始\n正文\n第１２章结束\n正文\n第十三章 末尾";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 3); book_close();
    s = "第1卷 第1章 开始\n第2卷 第1章 新卷\n正文";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    assert(book_open(path) == ESP_OK && book_chapter_count() == 2); book_close();
}
int main(int argc, char **argv) {
    txt_heading_regressions();
    const char *s = "Chapter 1 Start\nhello\nChapter 2 End\nworld\n";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK);
    assert(book_chapter_count() == 2); char title[256];
    assert(book_chapter_title(1, title, sizeof(title)) == ESP_OK);
    assert(strcmp(title, "Chapter 2 End") == 0); book_close(); roundtrip(s, strlen(s));
    s = "第一章 开始\n你好\n第二章 结束\n再见\n";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 2); book_close();
    char *large = malloc(300000); assert(large);
    for (size_t i = 0; i < 100000; ++i) memcpy(large + i * 3, "中", 3);
    write_bytes(large, 300000); roundtrip(large, 300000); free(large);
    const char gbk[] = { (char)0xd6,(char)0xd0,(char)0xce,(char)0xc4,'\n' };
    write_bytes(gbk, sizeof(gbk)); assert(book_open(path) == ESP_OK);
    char *out; size_t len; assert(book_chapter_load(0, &out, &len) == ESP_OK);
    assert(strcmp(out, "中文\n") == 0); free(out); book_close();
    char tiny[5] = {0};
    assert(gbk_to_utf8(gbk, sizeof(gbk), tiny, 4) == 3); assert(strcmp(tiny, "中") == 0);
    assert(gbk_to_utf8("\x81", 1, tiny, sizeof(tiny)) == 3); assert(strcmp(tiny, "\xef\xbf\xbd") == 0);
    s = "\xef\xbb\xbf" "Chapter 1\r\nx\xff\nChapter 2\r\ny";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 2);
    assert(book_chapter_load(0, &out, &len) == ESP_OK);
    assert(strcmp(out, "Chapter 1\r\nx\xef\xbf\xbd\n") == 0); free(out);
    assert(book_chapter_title(0, tiny, sizeof(tiny)) == ESP_ERR_INVALID_SIZE);
    assert(book_chapter_load(99, &out, &len) == ESP_ERR_INVALID_ARG); assert(!out && !len); book_close();
    large = malloc(70000); assert(large); memset(large, 'a', 65535);
    memcpy(large + 65535, "中\n", 4); write_bytes(large, 65539); roundtrip(large, 65539);
    large[65535] = (char)0xd6; large[65536] = (char)0xd0; large[65537] = '\n';
    write_bytes(large, 65538); assert(book_open(path) == ESP_OK);
    assert(book_chapter_load(0, &out, &len) == ESP_OK); assert(len == 65539);
    assert(!memcmp(out + 65535, "中\n", 4)); free(out); free(large); book_close();
    FILE *f = fopen(path, "wb"); assert(f);
    for (size_t i = 0; i < 2200; ++i) fprintf(f, "Chapter %zu\nbody\n", i);
    fclose(f); assert(book_open(path) == ESP_OK); assert(book_chapter_count() <= 2048); book_close();
    s = "preface\nChapter 1\nbody\nChapter 2\nend";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    write_bytes("", 0); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 1); book_close();
    assert(book_open("/tmp/missing.epub") == ESP_ERR_NOT_FOUND);
    assert(book_open("/tmp/missing.unknown") == ESP_ERR_NOT_SUPPORTED);
    assert(book_chapter_count() == 0);
    for (int a = 1; a < argc; ++a) {
        // 不支持的旧清单失败后，下一本正常书仍可解析；循环覆盖句柄/书源恢复。
        // An unsupported old manifest must not poison the next real book or leak source state.
        assert(book_open("/sdcard/.cache/catalog.wread") == ESP_ERR_NOT_SUPPORTED);
        assert(book_open("/sdcard/.cache/catolog.weread") == ESP_ERR_NOT_SUPPORTED);
        assert(book_open(argv[a]) == ESP_OK); assert(book_chapter_count() > 0);
        bool epub = strstr(argv[a], ".epub") != NULL;
        assert(book_kind() == (epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT));
        size_t sum = 0;
        for (size_t i = 0; i < book_chapter_count(); ++i) {
            assert(book_chapter_load(i, &out, &len) == ESP_OK); assert(out[len] == 0);
            html_text_t blocks = {0}; assert(book_chapter_load_blocks(i, &blocks) == ESP_OK);
            assert(blocks.len == len && !memcmp(blocks.utf8, out, len));
            assert(epub ? blocks.blocks != NULL && blocks.count > 0 : blocks.blocks == NULL && blocks.count == 0);
            if (strstr(argv[a], "河边记事_图文.epub")) {
                assert(blocks.image_count == 60);
                size_t images = 0;
                for (size_t b = 0; b < blocks.count; ++b) if (blocks.blocks[b].image >= 0) {
                    int image = blocks.blocks[b].image;
                    assert(image < (int)blocks.image_count);
                    unsigned width = 0, height = 0;
                    assert(book_chapter_image_dimensions(i, blocks.images[image], &width, &height) == ESP_OK);
                    assert(width == 384 && height == 128);
                    ++images;
                }
                assert(images == 60);
                uint8_t *data = NULL; size_t bytes = 0; bool png = false;
                assert(book_chapter_image(i, blocks.images[0], &data, &bytes, &png) == ESP_OK);
                assert(png && bytes > 32 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8));
                free(data);
            }
            html_text_free(&blocks);
            assert(book_chapter_title(i, title, sizeof(title)) == ESP_OK);
            sum += len; free(out);
        }
        printf("fixture %s: %zu chapters, %zu UTF-8 bytes\n", argv[a], book_chapter_count(), sum);
        book_close();
        assert(book_kind() == BOOK_KIND_TXT && book_chapter_count() == 0);
        session_roundtrip(argv[a]);
    }
    puts("book source host tests passed"); return 0;
}
