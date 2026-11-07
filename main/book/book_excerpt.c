/* SPDX-License-Identifier: Apache-2.0
 * 中文：确认显示后从有界正文选句，随进度交替保存双副本；绘制与逐页观察不读卡。
 * English: Select from bounded confirmed text and save alternating copies with progress; painting and page observation perform no storage IO.
 */
#include "book_excerpt.h"
#include "book_index_cache.h"
#include "book_path.h"
#include <stddef.h>
#include <string.h>

#define EXCERPT_HISTORY 4u
#define EXCERPT_BUDGET 32768u

typedef struct { uint32_t checksum; char text[BOOK_EXCERPT_CAP]; } legacy_excerpt_t;
typedef struct {
    uint32_t checksum, sequence, positioned, chapter, begin, end, pages;
    uint32_t history[EXCERPT_HISTORY];
    char text[BOOK_EXCERPT_CAP];
} excerpt_t;

static excerpt_t s_value;
static book_index_cache_header_t s_source;
static char s_path[BOOK_STORE_PATH_MAX];
static int s_slot;
static bool s_dirty, s_visible;
static size_t s_visible_chapter, s_visible_begin, s_visible_end;
static const char *const s_tags[] = {"quote-a", "quote-b"};

static uint32_t hash_bytes(const void *data, size_t len) {
    const uint8_t *bytes = data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static uint32_t checksum(const char *text) { return hash_bytes(text, strlen(text)); }

static uint32_t record_checksum(const excerpt_t *value) {
    size_t skip = offsetof(excerpt_t, sequence);
    return hash_bytes((const uint8_t *)value + skip, sizeof(*value) - skip);
}

static bool read_record(const char *source, unsigned slot, excerpt_t *out) {
    char path[160]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, s_tags[slot], 2, path, sizeof(path), &key)) return false;
    FILE *file = book_index_cache_open_read(path, &key);
    if (!file) return false;
    bool ok = fread(out, 1, sizeof(*out), file) == sizeof(*out) && fgetc(file) == EOF;
    fclose(file);
    return ok && out->sequence && out->positioned == 1 && out->begin <= out->end &&
        out->pages <= BOOK_EXCERPT_INTERVAL && strnlen(out->text, sizeof(out->text)) < sizeof(out->text) &&
        out->checksum == record_checksum(out);
}

static bool read_latest(const char *source, excerpt_t *out, int *slot) {
    excerpt_t other;
    bool a = read_record(source, 0, out), b = read_record(source, 1, &other);
    if (b && (!a || (other.sequence != out->sequence &&
                    (uint32_t)(other.sequence - out->sequence) < UINT32_C(0x80000000)))) {
        *out = other; *slot = 1;
    } else *slot = a ? 0 : -1;
    return a || b;
}

static bool read_legacy(const char *source, char out[BOOK_EXCERPT_CAP]) {
    char path[160]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, "quote", 1, path, sizeof(path), &key)) return false;
    FILE *file = book_index_cache_open_read(path, &key);
    if (!file) return false;
    legacy_excerpt_t value;
    bool ok = fread(&value, 1, sizeof(value), file) == sizeof(value) && fgetc(file) == EOF;
    fclose(file);
    ok = ok && strnlen(value.text, sizeof(value.text)) < sizeof(value.text) && value.text[0] &&
        value.checksum == checksum(value.text);
    if (ok) memcpy(out, value.text, sizeof(value.text));
    return ok;
}

static bool source_matches(const char *source) {
    char path[160]; book_index_cache_header_t key;
    return source && s_path[0] && !strcmp(source, s_path) &&
        book_index_cache_prepare(source, s_tags[0], 2, path, sizeof(path), &key) &&
        key.source_size == s_source.source_size && key.modified == s_source.modified &&
        key.path_hash == s_source.path_hash;
}

bool book_excerpt_load(const char *source, char out[BOOK_EXCERPT_CAP]) {
    if (!out) return false;
    out[0] = 0;
    if (source_matches(source)) {
        memcpy(out, s_value.text, sizeof(s_value.text));
        return out[0] != 0;
    }
    excerpt_t value; int slot;
    if (read_latest(source, &value, &slot)) {
        memcpy(out, value.text, sizeof(value.text));
        return out[0] != 0;
    }
    return read_legacy(source, out);
}

void book_excerpt_reset(void) {
    s_value = (excerpt_t){0}; s_path[0] = 0; s_slot = -1;
    s_dirty = s_visible = false;
}

void book_excerpt_begin(const char *source) {
    book_excerpt_reset();
    char path[160];
    if (!source || strlen(source) >= sizeof(s_path) ||
        !book_index_cache_prepare(source, s_tags[0], 2, path, sizeof(path), &s_source)) return;
    memcpy(s_path, source, strlen(source) + 1);
    if (!read_latest(source, &s_value, &s_slot)) {
        s_value = (excerpt_t){0};
        if (read_legacy(source, s_value.text)) s_value.history[0] = checksum(s_value.text);
    }
}

void book_excerpt_reposition(size_t chapter, size_t begin, size_t end) {
    if (!s_path[0] || chapter > UINT32_MAX || begin > end || end > UINT32_MAX) return;
    s_value.positioned = 1; s_value.chapter = (uint32_t)chapter;
    s_value.begin = (uint32_t)begin; s_value.end = (uint32_t)end;
    s_value.pages = 0; s_visible = false; s_dirty = true;
}

static unsigned decode(const char *s, size_t remain, uint32_t *cp) {
    unsigned c=(uint8_t)s[0], n=c<128?1:c>=0xc2&&c<=0xdf?2:c<=0xef&&c>=0xe0?3:c>=0xf0&&c<=0xf4?4:0;
    if (!n || n>remain) return 0;
    *cp=c & (n==1?127:n==2?31:n==3?15:7);
    for (unsigned i=1;i<n;++i) { if (((uint8_t)s[i]&0xc0)!=0x80) return 0; *cp=(*cp<<6)|((uint8_t)s[i]&63); }
    if ((n==2&&*cp<128)||(n==3&&*cp<0x800)||(n==4&&*cp<0x10000)||(*cp>=0xd800&&*cp<=0xdfff)||*cp>0x10ffff) return 0;
    return n;
}

static bool terminator(uint32_t cp) {
    return cp == 0x3002 || cp == 0xff01 || cp == 0xff1f || cp == '!' || cp == '?';
}

static bool starts_sentence(const char *text, size_t begin) {
    if (!begin) return true;
    size_t previous = begin - 1;
    while (previous && ((uint8_t)text[previous] & 0xc0) == 0x80) --previous;
    uint32_t cp;
    return decode(text + previous, begin - previous, &cp) &&
        (terminator(cp) || cp == '\n' || cp == '\r');
}

static bool contains_ascii(const char *text, const char *needle) {
    for (; *text; ++text) {
        size_t i = 0;
        while (needle[i] && text[i]) {
            unsigned c = (uint8_t)text[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (unsigned char)needle[i]) break;
            ++i;
        }
        if (!needle[i]) return true;
    }
    return false;
}

static bool usable(const char *text) {
    const char *const notices[] = {"版权", "著作权", "仅供", "禁止转载", "copyright",
                                  "all rights reserved", "isbn", "http://", "https://", "www."};
    for (size_t i = 0; i < sizeof(notices) / sizeof(notices[0]); ++i)
        if (strstr(text, notices[i]) || contains_ascii(text, notices[i])) return false;
    uint32_t hash = checksum(text);
    for (unsigned i = 0; i < EXCERPT_HISTORY; ++i) if (s_value.history[i] == hash) return false;
    return true;
}

static void sentence(const char *s, size_t len, size_t available, bool boundary, char *out, unsigned *best) {
    size_t start = 0; unsigned units = 0, chars = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp; unsigned n = decode(s + i, len - i, &cp);
        if (!n || !cp) { ++i; start = i; units = chars = 0; boundary = false; continue; }
        if (cp == '\n' || cp == '\r') { i += n; start = i; units = chars = 0; boundary = true; continue; }
        if (!chars && (cp <= 32 || cp == 0x3000 || cp == 0x201c || cp == 0x2018 || cp == '"' || cp == '\'')) {
            i += n; start = i; continue;
        }
        units += cp < 128 ? 1 : 2; ++chars;
        // 可见范围可能截在小数点；只前瞻原段落内的一字节，不摘录未显示文字。/ Peek within the source paragraph without quoting unseen text.
        bool end = terminator(cp) || (cp == '.' && (i + n == available || (uint8_t)s[i + n] <= 32));
        i += n;
        if (!end) continue;
        if (boundary && chars >= 8 && units <= 50 && i - start < BOOK_EXCERPT_CAP) {
            char candidate[BOOK_EXCERPT_CAP];
            memcpy(candidate, s + start, i - start); candidate[i - start] = 0;
            unsigned score = units <= 34 ? units : 68 - units;
            if (score >= *best && usable(candidate)) {
                memcpy(out, candidate, i - start + 1); *best = score;
            }
        }
        start = i; units = chars = 0; boundary = true;
    }
}

static bool select_sentence(const html_text_t *text, size_t begin, size_t end, char *out) {
    if (end - begin > EXCERPT_BUDGET) begin = end - EXCERPT_BUDGET;
    unsigned best = 0; out[0] = 0;
    if (!text->count) sentence(text->utf8 + begin, end - begin, text->len - begin,
        starts_sentence(text->utf8, begin), out, &best);
    else if (text->blocks && text->count <= HTML_TEXT_MAX_BLOCKS) {
        for (size_t i = 0; i < text->count; ++i) {
            const blk_t *b = &text->blocks[i];
            if (b->heading || b->auxiliary || b->linked || b->image >= 0 || b->offset >= end) continue;
            size_t limit = b->len > text->len - b->offset ? text->len : b->offset + b->len;
            size_t lo = b->offset > begin ? b->offset : begin;
            size_t hi = limit < end ? limit : end;
            if (lo < hi) sentence(text->utf8 + lo, hi - lo, limit - lo,
                lo == b->offset || starts_sentence(text->utf8, lo), out, &best);
        }
    }
    return out[0] != 0;
}

void book_excerpt_observe(const html_text_t *text, size_t chapter, size_t begin, size_t end, bool forward) {
    if (!s_path[0] || !text || !text->utf8 || !text->len || text->len > UINT32_MAX || chapter > UINT32_MAX) return;
    if (end > text->len) end = text->len;
    if (begin >= end) return;
    bool same = s_value.positioned && chapter == s_value.chapter && begin < s_value.end && end > s_value.begin;
    bool changed = !s_value.positioned || chapter != s_value.chapter || begin != s_value.begin || end != s_value.end;
    bool was_visible = s_visible;
    bool contiguous = was_visible && chapter == s_visible_chapter && begin >= s_visible_begin && begin <= s_visible_end;
    if (!contiguous) s_visible_begin = begin;
    s_visible = true; s_visible_chapter = chapter; s_visible_end = end;
    bool advanced = s_value.positioned && forward &&
        (chapter > s_value.chapter || (chapter == s_value.chapter && begin >= s_value.end));
    if (!same) {
        if (advanced) { if (s_value.pages < BOOK_EXCERPT_INTERVAL) ++s_value.pages; }
        else s_value.pages = 0;
    }
    // 重排也更新当前边界，避免后续翻页仍与旧大页比较。/ Keep reflowed bounds current for the next real turn.
    s_value.positioned = 1; s_value.chapter = (uint32_t)chapter;
    s_value.begin = (uint32_t)begin; s_value.end = (uint32_t)end;
    if (changed) s_dirty = true;
    if (same && (s_value.text[0] || (!changed && was_visible))) return;
    if (s_value.text[0] && s_value.pages < BOOK_EXCERPT_INTERVAL) return;
    char candidate[BOOK_EXCERPT_CAP];
    if (!select_sentence(text, s_visible_begin, end, candidate)) return;
    memcpy(s_value.text, candidate, strlen(candidate) + 1);
    memmove(s_value.history + 1, s_value.history, (EXCERPT_HISTORY - 1) * sizeof(s_value.history[0]));
    s_value.history[0] = checksum(candidate); s_value.pages = 0;
    s_dirty = true;
    s_visible_begin = begin;
}

bool book_excerpt_flush(const char *source) {
    if (!s_dirty) return true;
    if (!source_matches(source)) return false;
    excerpt_t value = s_value;
    if (!++value.sequence) ++value.sequence;
    value.checksum = record_checksum(&value);
    unsigned slot = s_slot == 0 ? 1 : 0;
    char path[160], temp[176]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, s_tags[slot], 2, path, sizeof(path), &key)) return false;
    FILE *file = book_index_cache_open_write(path, &key, temp, sizeof(temp));
    if (!file || !book_index_cache_finish_write(file, temp, path, fwrite(&value, 1, sizeof(value), file) == sizeof(value))) return false;
    s_value = value; s_slot = (int)slot; s_dirty = false;
    return true;
}
