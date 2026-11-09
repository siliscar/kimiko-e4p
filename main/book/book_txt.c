/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式 TXT 编码识别、目录与按章转码。
 * English: Streaming TXT encoding detection, indexing and chapter conversion.
 * 冻结：原始偏移用于进度；目录上限 2048；不加载整本书。
 * Frozen: Progress uses source offsets; at most 2048 entries; never load the whole book.
 */
#include "../diagnostics/reader_perf.h"
#include "book_source_internal.h"
#include "book_index_cache.h"
#include "gbk.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#define READ_BLOCK (64 * 1024)
#define PART_TARGET (48 * 1024)
#define HEADING_LINE_BYTES 256
#define TXT_CACHE_VERSION 2u
typedef struct {
    uint32_t total;
    uint32_t bom;
    uint32_t count;
    uint8_t gbk;
    uint8_t reserved[3];
} txt_cache_payload_t;
typedef struct {
    FILE *file; ///< 只读句柄 / Read-only handle
    unsigned char bytes[READ_BLOCK + 4]; ///< 跨块字符余量 / Room for a cross-block character
    size_t pos, used; ///< 缓冲游标 / Buffer cursors
    uint32_t offset; ///< 原文件游标 / Source cursor
    bool gbk; ///< 编码 / Encoding
} reader_t;

// 验证一个 UTF-8 标量；无效时仅消费首字节。/ Validate one UTF-8 scalar; consume only its first byte on error.
static size_t utf8_one(const unsigned char *s, size_t n, uint32_t *cp) {
    *cp = 0xfffd;
    if (!n) return 0;
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    size_t k = s[0] >= 0xc2 && s[0] <= 0xdf ? 2 : s[0] >= 0xe0 && s[0] <= 0xef ? 3 : s[0] >= 0xf0 && s[0] <= 0xf4 ? 4 : 0;
    if (!k || k > n) return 1;
    uint32_t c = s[0] & (0x7f >> k);
    for (size_t j = 1; j < k; ++j) { if ((s[j] & 0xc0) != 0x80) return 1; c = (c << 6) | (s[j] & 63); }
    if ((k == 2 && c < 0x80) || (k == 3 && c < 0x800) || (k == 4 && c < 0x10000) || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) return 1;
    *cp = c; return k;
}
static size_t encode(uint32_t cp, char out[4]) {
    if (!cp) cp = 0xfffd;
    if (cp < 0x80) { out[0] = cp; return 1; }
    if (cp < 0x800) { out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63); out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}
static bool next(reader_t *r, uint32_t *cp) {
    if (r->used - r->pos < 4) {
#if CONFIG_READER_PERF
        READER_PERF_SCOPE(READER_PERF_READ);
#endif
        size_t tail = r->used - r->pos;
        memmove(r->bytes, r->bytes + r->pos, tail);
        r->used = tail + fread(r->bytes + tail, 1, READ_BLOCK, r->file); r->pos = 0;
    }
    size_t n = r->used - r->pos;
    if (!n) return false;
    const unsigned char *s = r->bytes + r->pos; size_t k = 1;
    if (!r->gbk) k = utf8_one(s, n, cp);
    else if (s[0] < 0x80) *cp = s[0];
    else {
        *cp = 0xfffd;
        if (s[0] >= 0x81 && s[0] <= 0xfe && n > 1 && s[1] >= 0x40 && s[1] <= 0xfe && s[1] != 0x7f) { *cp = gbk_codepoint(s[0], s[1]); k = 2; }
    }
    r->pos += k; r->offset += k; return true;
}
static esp_err_t reset(reader_t *r, book_txt_t *b, uint32_t offset) {
    memset(r, 0, sizeof(*r)); r->file = b->file; r->gbk = b->gbk; r->offset = offset;
    clearerr(b->file);
    return fseek(b->file, (long)offset, SEEK_SET) ? ESP_FAIL : ESP_OK;
}
static bool heading_number(uint32_t cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 0xff10 && cp <= 0xff19) ||
        cp == 0x3007 || cp == 0x96f6 || cp == 0x4e00 || cp == 0x4e8c ||
        cp == 0x4e09 || cp == 0x56db || cp == 0x4e94 || cp == 0x516d ||
        cp == 0x4e03 || cp == 0x516b || cp == 0x4e5d || cp == 0x5341 ||
        cp == 0x767e || cp == 0x5343 || cp == 0x4e07 || cp == 0x4e24;
}
// 只消费编号和单位，卷名、篇名与正文标题不参与重复题头判断。
// Consume only the number and unit; volume names and chapter titles do not identify duplicates.
static size_t heading_marker(const char *s, uint32_t *unit) {
    if (strncmp(s, "第", 3)) return 0;
    const char *begin = s; s += 3; size_t digits = 0;
    while (*s) {
        uint32_t cp; size_t n = utf8_one((const unsigned char *)s, strlen(s), &cp);
        if (heading_number(cp) && ++digits <= 16) { s += n; continue; }
        if (digits && (cp == 0x7ae0 || cp == 0x8282 || cp == 0x5377 || cp == 0x56de ||
                       cp == 0x96c6 || cp == 0x90e8 || cp == 0x7bc7)) {
            *unit = cp; return (size_t)(s + n - begin);
        }
        break;
    }
    return 0;
}
static bool heading(const char *s, char key[HEADING_LINE_BYTES + 1]) {
    if (!strncasecmp(s, "Chapter", 7)) {
        const char *number = s + 7;
        while (*number == ' ') ++number;
        const char *end = number;
        while (isdigit((unsigned char)*end)) ++end;
        size_t digits = (size_t)(end - number);
        if (!digits || digits > 16 || (*end && *end != ' ' && *end != '.' && *end != ':')) return false;
        memcpy(key, number, digits); key[digits] = 0; return true;
    }
    uint32_t unit;
    size_t n = heading_marker(s, &unit);
    if (!n || strstr(s, "。")) return false;
    // 无空格的卷/篇前缀必须含后续章号，避免把“第九集团军”等正文当成目录。
    // Unspaced volume prefixes need a later chapter marker, excluding prose such as army names.
    bool boundary = !s[n] || s[n] == ' ';
    bool chapter = unit == 0x7ae0 || unit == 0x56de || unit == 0x8282;
    if (chapter && !boundary && strlen(s + n) > 40) return false;
    if (chapter && !boundary && !strncmp(s + n, "到", 3) &&
        (!s[n + 3] || strchr("!,~", s[n + 3]) || !strncmp(s + n + 3, "！", 3) ||
         !strncmp(s + n + 3, "，", 3) || !strncmp(s + n + 3, "～", 3))) return false;
    memcpy(key, s, n); size_t used = n;
    for (const char *p = strstr(s + n, "第"); p; p = strstr(p + 3, "第")) {
        uint32_t nested; size_t bytes = heading_marker(p, &nested);
        if (!bytes) continue;
        memcpy(key + used, p, bytes); used += bytes;
        if (nested == 0x7ae0 || nested == 0x56de || nested == 0x8282) chapter = true;
    }
    key[used] = 0;
    return boundary || chapter;
}
static bool add(book_txt_t *b, uint32_t offset, const char *title) {
    if (b->count == BOOK_CHAPTER_MAX) return false;
    book_entry_t *e = &b->entries[b->count++]; e->offset = offset;
    if (title) {
        size_t used = 0, bytes = strlen(title);
        while (used < bytes) {
            uint32_t cp; size_t n = utf8_one((const unsigned char *)title + used, bytes - used, &cp);
            if (used + n >= sizeof(e->title)) break;
            used += n;
        }
        memcpy(e->title, title, used); e->title[used] = 0;
    }
    else snprintf(e->title, sizeof(e->title), "Part %u", (unsigned)b->count);
    return true;
}
typedef struct {
    char key[HEADING_LINE_BYTES + 1]; ///< 上个题头的编号序列 / Previous heading's numbered markers
    bool before_body; ///< 尚未遇到正文 / No body text encountered yet
} heading_state_t;

static bool index_line(book_txt_t *b, uint32_t start, char *line, bool overflow, heading_state_t *state) {
    char key[HEADING_LINE_BYTES + 1];
    size_t len = strlen(line);
    if (len && line[len - 1] == ' ') line[len - 1] = 0;
    if (!overflow && heading(line, key)) {
        if (state->before_body) {
            if (!strcmp(key, state->key)) return true;
            // 重复题头可省略篇卷前缀；两边都带前缀时仍须区分篇卷。
            // Repeated headings may omit a volume prefix; retain distinct volumes when both specify one.
            const char *last = strstr(key, "第"), *previous = strstr(state->key, "第");
            if (last && previous) {
                const char *next;
                while ((next = strstr(last + 3, "第"))) last = next;
                while ((next = strstr(previous + 3, "第"))) previous = next;
                if ((last == key || previous == state->key) && !strcmp(last, previous)) return true;
            }
        }
        if (!add(b, start, line)) return false;
        strcpy(state->key, key); state->before_body = true;
    } else if (overflow || *line) state->before_body = false;
    return true;
}
static esp_err_t index_book(reader_t *r, book_txt_t *b) {
    esp_err_t err = reset(r, b, b->bom); if (err) return err;
    char line[HEADING_LINE_BYTES + 1] = {0}; size_t len = 0;
    bool overflow = false, too_many = false, cr = false;
    heading_state_t state = {0};
    uint32_t start = b->bom, cp;
    while (next(r, &cp)) {
        if (cp == '\n' && cr) { start = r->offset; cr = false; continue; }
        cr = cp == '\r';
        if (cp == '\n' || cr) {
            if (!index_line(b, start, line, overflow, &state)) too_many = true;
            start = r->offset; len = 0; line[0] = 0; overflow = false;
        } else {
            // 缩进不占题头预算，内部空白折成一个空格；原文件与偏移不变。
            // Indentation uses no heading budget; fold internal whitespace while preserving source offsets.
            if (cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0xa0) {
                if (!len || line[len - 1] == ' ') continue;
                cp = ' ';
            }
            char bytes[4]; size_t n = encode(cp, bytes);
            if (len + n <= HEADING_LINE_BYTES && !overflow) { memcpy(line + len, bytes, n); len += n; line[len] = 0; }
            else overflow = true;
        }
    }
    if (ferror(b->file)) return ESP_FAIL;
    if (!index_line(b, start, line, overflow, &state)) too_many = true;
    bool fallback = !b->count || too_many;
    if (b->count) b->entries[0].offset = 0;
    for (size_t i = 0; i < b->count; ++i) {
        uint32_t end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
        if (end - b->entries[i].offset > BOOK_CHAPTER_BYTES_MAX) fallback = true;
    }
    if (!fallback) return ESP_OK;
    b->count = 0; add(b, 0, NULL); err = reset(r, b, b->bom); if (err) return err;
    uint32_t part = 0;
    while (next(r, &cp)) {
        uint32_t size = r->offset - part;
        // 目标后首个换行切段；无换行时在硬上限前按字符切。/ Split at the first newline after the target, or before the hard cap at a character boundary.
        if (r->offset < b->total && ((size >= PART_TARGET && cp == '\n') || size >= BOOK_CHAPTER_BYTES_MAX - 4)) {
            if (!add(b, r->offset, NULL)) return ESP_ERR_INVALID_SIZE;
            part = r->offset;
        }
    }
    return ferror(b->file) ? ESP_FAIL : ESP_OK;
}

static bool txt_cache_load(const char* source, book_txt_t* b) {
    char path[112]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, "txt", TXT_CACHE_VERSION, path, sizeof(path), &key)) return false;
    FILE* cache = book_index_cache_open_read(path, &key);
    if (!cache) return false;
    txt_cache_payload_t payload;
    bool ok = fread(&payload, 1, sizeof(payload), cache) == sizeof(payload) &&
        payload.total == b->total && payload.bom <= 3 && payload.bom <= payload.total &&
        payload.count > 0 && payload.count <= BOOK_CHAPTER_MAX && payload.gbk <= 1 &&
        fread(b->entries, sizeof(b->entries[0]), payload.count, cache) == payload.count &&
        fgetc(cache) == EOF;
    fclose(cache);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < payload.count; ++i) {
        const book_entry_t* entry = &b->entries[i];
        ok = entry->offset >= previous && entry->offset < payload.total &&
            memchr(entry->title, 0, sizeof(entry->title)) != NULL;
        previous = entry->offset;
    }
    if (!ok) return false;
    b->bom = payload.bom; b->gbk = payload.gbk != 0; b->count = payload.count;
    return true;
}

static void txt_cache_save(const char* source, const book_txt_t* b) {
    char path[112], temp[120]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, "txt", TXT_CACHE_VERSION, path, sizeof(path), &key)) return;
    FILE* cache = book_index_cache_open_write(path, &key, temp, sizeof(temp));
    if (!cache) return;
    txt_cache_payload_t payload = {
        .total = b->total, .bom = b->bom, .count = (uint32_t)b->count, .gbk = b->gbk,
    };
    bool ok = fwrite(&payload, 1, sizeof(payload), cache) == sizeof(payload) &&
        fwrite(b->entries, sizeof(b->entries[0]), b->count, cache) == b->count;
    (void)book_index_cache_finish_write(cache, temp, path, ok);
}

esp_err_t book_txt_open(book_txt_t *b, const char *path) {
    b->file = fopen(path, "rb"); if (!b->file) return ESP_ERR_NOT_FOUND;
    if (fseek(b->file, 0, SEEK_END)) return ESP_FAIL;
    long size = ftell(b->file);
    if (size < 0 || (unsigned long)size > UINT32_MAX || size > LONG_MAX - 4) return ESP_ERR_INVALID_SIZE;
    b->total = (uint32_t)size;
    b->entries = heap_caps_malloc(sizeof(book_entry_t) * BOOK_CHAPTER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b->entries) return ESP_ERR_NO_MEM;
    if (txt_cache_load(path, b)) return ESP_OK;
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r) return ESP_ERR_NO_MEM;
    esp_err_t err = reset(r, b, 0);
    if (!err) {
        size_t n = fread(r->bytes, 1, READ_BLOCK + 3, b->file);
        if (ferror(b->file)) err = ESP_FAIL;
        else if (n >= 3 && !memcmp(r->bytes, "\xef\xbb\xbf", 3)) b->bom = 3;
        else for (size_t i = 0; i < n && i < READ_BLOCK;) {
            uint32_t cp; size_t k = utf8_one(r->bytes + i, n - i, &cp);
            if (k == 1 && r->bytes[i] >= 0x80) { b->gbk = true; break; } i += k;
        }
    }
    if (!err) err = index_book(r, b);
    if (!err) txt_cache_save(path, b);
    free(r); return err;
}
esp_err_t book_txt_load(book_txt_t *b, size_t i, char **utf8, size_t *len) {
    uint32_t start = b->entries[i].offset, end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
    if (!start) start = b->bom;
    size_t cap = (size_t)(end - start) * 3 + 1;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out || !r) { free(out); free(r); return ESP_ERR_NO_MEM; }
    esp_err_t err = reset(r, b, start); size_t used = 0; uint32_t cp;
    if (!err) while (r->offset < end && next(r, &cp)) {
        char bytes[4]; size_t n = encode(cp, bytes);
        // 文件被外部改写时拒绝越过已索引边界。/ Reject crossing indexed bounds if the file changed externally.
        if (r->offset > end || n >= cap - used) { err = ESP_ERR_INVALID_SIZE; break; }
        memcpy(out + used, bytes, n); used += n;
    }
    if (ferror(b->file) || r->offset != end) err = ESP_FAIL;
    free(r);
    if (err) { free(out); return err; }
    out[used] = 0; *utf8 = out; *len = used; return ESP_OK;
}
