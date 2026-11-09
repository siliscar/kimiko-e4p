/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：包含后端以核对原始记录，不向固件暴露测试接口。
 * English: Include the backend for raw-record checks without exposing firmware test APIs.
 */
#include "../main/book/book_mobi.c"
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

static int cache_mkdir(const char *path, mode_t mode) {
    if (!strcmp(path, "/sdcard/.readpico") || !strcmp(path, "/sdcard/.readpico/index")) return 0;
    return mkdir(path, mode);
}
#define mkdir cache_mkdir
#include "../main/book/book_index_cache.c"
#undef mkdir

static void codec_test(void) {
    uint8_t out[32]; size_t n;
    const uint8_t bad[][2] = {{0x80,0}, {0x80,8}, {3,'x'}};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        n = sizeof(out); assert(!mobi_codec_palmdoc(out, &n, bad[i], 2));
    }
    const uint8_t overlap[] = {'a',0x80,0x0a,0xc1};
    n = sizeof(out); assert(mobi_codec_palmdoc(out, &n, overlap, sizeof(overlap)));
    assert(n == 8 && !memcmp(out, "aaaaaa A", 8));
    n = 2; assert(!mobi_codec_palmdoc(out, &n, overlap, sizeof(overlap)));
    puts("PASS: bounded PalmDOC literals, overlap, invalid distance, truncated input and output cap");
}

static void mapping_test(void) {
    const char *sources[] = {"<p>abc\xc3\xa9 &amp; tail</p>", "<p>abc\xe9 &amp; tail</p>"};
    for (size_t encoding = 0; encoding < 2; ++encoding) {
        const char *raw = sources[encoding];
        size_t entity = (size_t)(strstr(raw, "&amp;") - raw);
        size_t positions[] = {4, 6, 7, entity + 2, entity + 4, entity + 6};
        for (size_t j = 0; j < sizeof(positions)/sizeof(*positions); ++j) {
            html_text_t text = {0}; size_t offset = SIZE_MAX;
            assert(book_mobi_markup(raw, strlen(raw), 100, encoding ? 1252 : 65001,
                                     UINT32_MAX, 5, NULL, positions[j], &offset, &text) == ESP_OK);
            assert(!strcmp(text.utf8, "abc\xc3\xa9 & tail"));
            assert(offset < text.len && book_mobi_utf8(text.utf8 + offset, text.len - offset));
            if (positions[j] >= entity && positions[j] < entity + 5) {
                const char *landed = text.utf8 + offset;
                while (isspace((unsigned char)*landed)) ++landed;
                assert(*landed == '&');
            }
            html_text_free(&text);
        }
    }
    const char *raw = "<p>abcdef</p><a filepos=4>x</a><a filepos=5>y</a><a filepos=6>z</a>";
    html_text_t text = {0};
    assert(book_mobi_markup(raw, strlen(raw), 0, 65001, UINT32_MAX, 5, NULL, SIZE_MAX, NULL, &text) == ESP_OK);
    assert(text.anchor_count == 3);
    for (size_t i = 0; i < 3; ++i) assert(text.utf8[text.anchors[i].offset] == 'b' + (int)i);
    html_text_free(&text);
    puts("PASS: original-byte targets preserve UTF-8, CP1252, entities and adjacent anchors");
}

static void cache_test(book_mobi_t *cold) {
    char path[PATH_CAP];
    assert(snprintf(path, sizeof(path), "%s.index", cold->path) < (int)sizeof(path));
    book_index_cache_header_t key = {.magic = 12345, .version = 1, .source_size = cold->size, .modified = 1, .path_hash = 42};
    for (unsigned corruption = 0; corruption < 4; ++corruption) {
        cache_write(cold, path, &key);
        if (corruption == 1 || corruption == 2) {
            FILE *file = fopen(path, "r+b"); assert(file);
            if (corruption == 1) {
                assert(!fseek(file, -1, SEEK_END)); int c = fgetc(file); assert(c != EOF);
                assert(!fseek(file, -1, SEEK_END) && fputc(c ^ 1, file) != EOF);
            } else assert(!ftruncate(fileno(file), sizeof(key) + sizeof(cache_t) + 1));
            assert(!fclose(file));
        }
        book_mobi_t *hot = NULL; assert(headers(cold->path, &hot) == ESP_OK);
        if (corruption == 3) ++hot->fingerprint;
        assert(cache_read(hot, path, &key) == !corruption);
        if (corruption) assert(build_index(hot) == ESP_OK);
        assert(cold->count == hot->count && cold->nav_count == hot->nav_count && index_checksum(cold) == index_checksum(hot));
        for (size_t i = 0; i < cold->nav_count; ++i) {
            assert(book_mobi_navigation_chapter(cold, i) == book_mobi_navigation_chapter(hot, i));
            assert(book_mobi_navigation_source_offset(cold, i) == book_mobi_navigation_source_offset(hot, i));
        }
        for (size_t i = 0; i < cold->count; ++i) {
            html_text_t a = {0}, b = {0}; size_t x, y;
            assert(book_mobi_load_target(cold, i, NULL, 0, &x, &a) == ESP_OK);
            assert(book_mobi_load_target(hot, i, NULL, 0, &y, &b) == ESP_OK);
            assert(x == y && a.len == b.len && !memcmp(a.utf8, b.utf8, a.len));
            html_text_free(&a); html_text_free(&b);
        }
        book_mobi_close(hot);
    }
    assert(!remove(path));
    cache_write(cold, "/nonexistent/mobi-index", &key);
    puts("PASS: cold/hot cache positions and content match; corrupt, truncated, stale and unavailable cache recover");
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    if (!strcmp(argv[1], "codec")) { codec_test(); mapping_test(); return 0; }
    assert(argc == 3);
    book_mobi_t *b = NULL;
    esp_err_t err = book_mobi_open(argv[2], &b);
    if (!strcmp(argv[1], "unsupported")) { assert(err == ESP_ERR_NOT_SUPPORTED && !b); return 0; }
    if (!strcmp(argv[1], "invalid")) { assert(err != ESP_OK && !b); return 0; }
    if (err != ESP_OK) fprintf(stderr, "MOBI open error %x\n", err);
    assert(err == ESP_OK && b);
    if (!strcmp(argv[1], "raw")) {
        uint8_t *in = malloc(RECORD_BYTES), *raw = malloc(RECORD_BYTES);
        assert(in && raw);
        for (size_t i = 1; i <= b->text_records; ++i) {
            size_t n;
            assert(decode_record(b, i, in, raw, &n) == ESP_OK && n >= b->record[i].len);
            assert(fwrite(raw, 1, b->record[i].len, stdout) == b->record[i].len);
        }
        free(in); free(raw); book_mobi_close(b); return 0;
    }
    bool fixture = !strcmp(argv[1], "fixture"), sample = !strcmp(argv[1], "sample");
    size_t paragraphs = 0, images = 0;
    for (size_t i = 0; i < book_mobi_chapter_count(b); ++i) {
        html_text_t text = {0};
        assert(book_mobi_load_target(b, i, NULL, SIZE_MAX, NULL, &text) == ESP_OK);
        assert(book_mobi_utf8(text.utf8, text.len));
        assert(!strstr(text.utf8, "KF8 TEXT MUST NOT APPEAR"));
        if (fixture && strstr(text.utf8, "Caf")) {
            assert(strstr(text.utf8, "Caf\xc3\xa9 \xe2\x82\xac & words"));
            assert(text.image_count == 1 && text.link_count >= 1 && text.anchor_count >= 1);
            char id[64];
            assert(text.links[0].href_len > 1 && text.links[0].href_len < sizeof(id));
            assert(text.strings[text.links[0].href] == '#');
            memcpy(id, text.strings + text.links[0].href + 1, text.links[0].href_len - 1);
            id[text.links[0].href_len - 1] = 0;
            size_t target = 0; html_text_t again = {0};
            assert(book_mobi_load_target(b, i, id, SIZE_MAX, &target, &again) == ESP_OK);
            assert(!strncmp(again.utf8 + target, "A readable footnote.", strlen("A readable footnote.")));
            uint8_t *data = NULL; size_t size = 0; bool png = false; unsigned w,h;
            assert(book_mobi_image(b, i, text.images[0], &data, &size, &png) == ESP_OK && png && size);
            free(data);
            assert(book_mobi_image_dimensions(b, i, text.images[0], &w, &h) == ESP_OK && w == 17 && h == 23);
            html_text_free(&again);
        }
        if (sample) for (size_t image = 0; image < text.image_count; ++image) {
            uint8_t *data; size_t size; bool png;
            assert(book_mobi_image(b, i, text.images[image], &data, &size, &png) == ESP_OK && !png && size == 13744);
            free(data); ++images;
        }
        for (const char *p = text.utf8; (p = strstr(p, "Repeated paragraph")); ++p) ++paragraphs;
        html_text_free(&text); html_text_free(&text);
    }
    if (!strcmp(argv[1], "long")) assert(paragraphs == 9000 && b->count > 1);
    if (sample) assert(images == 1);
    if (fixture) cache_test(b);
    if (fixture || sample) {
        size_t expected = fixture ? 2 : 8;
        assert(book_mobi_navigation_count(b) == expected);
        for (size_t i = 0; i < expected; ++i) {
            char title[128]; size_t target = SIZE_MAX; html_text_t text = {0};
            assert(book_mobi_navigation_title(b, i, title, sizeof(title)) == ESP_OK);
            size_t chapter = book_mobi_navigation_chapter(b, i);
            size_t source = book_mobi_navigation_source_offset(b, i);
            assert(chapter < b->count && source != SIZE_MAX);
            assert(book_mobi_load_target(b, chapter, NULL, source, &target, &text) == ESP_OK);
            assert(target <= text.len && !strncmp(text.utf8 + target, title, strlen(title)));
            bool heading = false;
            for (size_t j = 0; j < text.count; ++j)
                if (text.blocks[j].chapter_start && !strncmp(text.utf8 + text.blocks[j].offset, title, strlen(title))) heading = true;
            assert(heading);
            printf("NAV %zu: chapter=%zu source=%zu text=%zu %s\n", i+1, chapter, source, target, title);
            html_text_free(&text);
        }
        uint8_t *data = NULL; size_t size; bool png;
        assert(book_mobi_cover_bounded(argv[2], &data, &size, &png, 1) == ESP_ERR_NO_MEM && !data);
        assert(book_mobi_cover_bounded(argv[2], &data, &size, &png, 4*1024*1024) == ESP_OK);
        if (fixture) { unsigned w,h; assert(book_image_dimensions(data,size,png,&w,&h) && w==53 && h==71); }
        if (sample) assert(!png && size == 122380);
        free(data);
        char title[256], author[160];
        assert(book_mobi_metadata(argv[2], title,sizeof(title),author,sizeof(author)) == ESP_OK && *title && *author);
        if (fixture) assert(!strcmp(author,"Original Author"));
    }
    printf("PASS: %s records=%u segments=%zu navigation=%zu raw=%u\n", argv[1], b->records, b->count, b->nav_count, b->total);
    book_mobi_close(b); book_mobi_close(NULL);
    return 0;
}
