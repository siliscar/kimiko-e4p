/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：不执行脚本、不访问路径；所有 filepos 均保持原始编码的字节坐标。
 * English: No scripts or path access; filepos always uses original encoded byte coordinates.
 */
#include "book_mobi_markup.h"
#include "esp_heap_caps.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

size_t book_mobi_token(const char *data, size_t len, bool final, mobi_token_t *t) {
    *t = (mobi_token_t){.bytes = data};
    if (!len) return 0;
    size_t end = 0;
    if (data[0] != '<') {
        while (end < len && data[end] != '<') ++end;
        t->len = end;
        return end;
    }
    t->tag = true;
    t->comment = len >= 4 && !memcmp(data, "<!--", 4);
    char quote = 0;
    for (end = 1; end < len; ++end) {
        char c = data[end];
        if (t->comment) {
            if (end >= 2 && c == '>' && data[end-1] == '-' && data[end-2] == '-') break;
        } else if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') quote = c;
        else if (c == '>') break;
    }
    if (end == len) {
        if (!final) return 0;
        t->len = len;
        t->comment = true;
        return len;
    }
    t->len = end + 1;
    size_t i = 1, n = 0;
    while (i < end && isspace((unsigned char)data[i])) ++i;
    if (i < end && data[i] == '/') { t->closing = true; ++i; }
    while (i < end && n + 1 < sizeof(t->name) &&
           (isalnum((unsigned char)data[i]) || data[i] == ':' || data[i] == '-'))
        t->name[n++] = (char)tolower((unsigned char)data[i++]);
    return t->len;
}

bool book_mobi_attr(const mobi_token_t *t, const char *name, const char **value, size_t *len) {
    if (!t->tag || t->comment || t->closing) return false;
    size_t at = 1;
    while (at < t->len && isspace((unsigned char)t->bytes[at])) ++at;
    while (at < t->len && !isspace((unsigned char)t->bytes[at]) && t->bytes[at] != '>') ++at;
    while (at < t->len) {
        while (at < t->len && (isspace((unsigned char)t->bytes[at]) || t->bytes[at] == '/')) ++at;
        size_t begin = at;
        while (at < t->len && (isalnum((unsigned char)t->bytes[at]) || strchr("_:-", t->bytes[at]))) ++at;
        size_t key_len = at - begin;
        if (!key_len) break;
        while (at < t->len && isspace((unsigned char)t->bytes[at])) ++at;
        if (at == t->len || t->bytes[at] != '=') continue;
        ++at;
        while (at < t->len && isspace((unsigned char)t->bytes[at])) ++at;
        char quote = at < t->len && (t->bytes[at] == '\'' || t->bytes[at] == '"') ? t->bytes[at++] : 0;
        size_t start = at;
        while (at < t->len && (quote ? t->bytes[at] != quote :
               !isspace((unsigned char)t->bytes[at]) && t->bytes[at] != '>')) ++at;
        if (strlen(name) == key_len && !strncasecmp(t->bytes + begin, name, key_len)) {
            *value = t->bytes + start; *len = at - start;
            return true;
        }
        if (quote && at < t->len) ++at;
    }
    return false;
}

bool book_mobi_number(const char *s, size_t n, uint32_t *v) {
    if (!n || n > 10) return false;
    uint32_t result = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9' || result > (UINT32_MAX - (unsigned)(s[i] - '0')) / 10) return false;
        result = result * 10 + (unsigned)(s[i] - '0');
    }
    *v = result;
    return true;
}

bool book_mobi_utf8(const char *data, size_t len) {
    for (size_t i = 0; i < len;) {
        uint8_t c = (uint8_t)data[i++];
        if (!c) return false;
        if (c < 0x80) continue;
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || count > len - i) return false;
        uint32_t cp = c & ((1u << (6 - count)) - 1);
        for (unsigned j = 0; j < count; ++j) {
            c = (uint8_t)data[i++];
            if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3f);
        }
        if (cp < (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

esp_err_t book_mobi_decode(const char *data, size_t len, uint32_t enc, char **out, size_t *size) {
    *out = NULL; *size = 0;
    if (enc != 65001 && enc != 1252) return ESP_ERR_NOT_SUPPORTED;
    if (enc == 65001 && !book_mobi_utf8(data, len)) return ESP_ERR_INVALID_RESPONSE;
    char *s = heap_caps_malloc(len * (enc == 1252 ? 3 : 1) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    static const uint16_t cp1252[32] = {
        0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
        0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
    };
    size_t used = 0;
    if (enc == 65001) { memcpy(s, data, len); used = len; }
    else for (size_t i = 0; i < len; ++i) {
        uint32_t c = (uint8_t)data[i];
        if (!c) { free(s); return ESP_ERR_INVALID_RESPONSE; }
        if (c >= 0x80 && c < 0xa0) c = cp1252[c - 0x80];
        if (c < 0x80) s[used++] = (char)c;
        else {
            if (c >= 0x800) { s[used++] = (char)(0xe0 | c >> 12); s[used++] = (char)(0x80 | ((c >> 6) & 63)); }
            else s[used++] = (char)(0xc0 | c >> 6);
            s[used++] = (char)(0x80 | (c & 63));
        }
    }
    s[used] = 0; *out = s; *size = used;
    return ESP_OK;
}

typedef struct { char *s; size_t len, cap; } output_t;
static bool append(output_t *out, const char *s, size_t len) {
    if (len > HTML_TEXT_MAX_BYTES - out->len) return false;
    size_t need = out->len + len + 1;
    if (need > out->cap) {
        size_t cap = need + 4096;
        char *p = heap_caps_realloc(out->s, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) return false;
        out->s = p; out->cap = cap;
    }
    memcpy(out->s + out->len, s, len); out->len += len; out->s[out->len] = 0;
    return true;
}

typedef struct { uint32_t raw, effective; } target_t;
static int compare_target(const void *a, const void *b) {
    uint32_t x = ((const target_t *)a)->raw, y = ((const target_t *)b)->raw;
    return (x > y) - (x < y);
}

esp_err_t book_mobi_markup(const char *raw, size_t len, uint32_t start, uint32_t enc,
                           uint32_t first_image, uint32_t records, const char *anchor,
                           size_t source_offset, size_t *text_offset, html_text_t *out) {
    memset(out, 0, sizeof(*out));
    if (text_offset) *text_offset = 0;
    if (len > MOBI_SEGMENT_MAX) return ESP_ERR_INVALID_SIZE;
    target_t *targets = heap_caps_malloc((MOBI_NAV_MAX + 1) * sizeof(*targets), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!targets) return ESP_ERR_NO_MEM;
    size_t target_count = 0;
    if (source_offset < len) targets[target_count++].raw = (uint32_t)source_offset;
    mobi_token_t t;
    for (size_t at = 0; at < len;) {
        size_t n = book_mobi_token(raw + at, len - at, true, &t);
        const char *v; size_t vn; uint32_t pos;
        if (t.len > MOBI_TAG_MAX && t.tag) { free(targets); return ESP_ERR_INVALID_SIZE; }
        if (!strcmp(t.name, "a") && book_mobi_attr(&t, "filepos", &v, &vn) &&
            book_mobi_number(v, vn, &pos) && pos >= start && pos - start < len) {
            if (target_count == MOBI_NAV_MAX + 1) { free(targets); return ESP_ERR_INVALID_SIZE; }
            targets[target_count++].raw = pos - start;
        }
        at += n;
    }
    qsort(targets, target_count, sizeof(*targets), compare_target);
    /* 中文：保留原坐标作 ID，插入位置退到完整标签、字符或实体之前。
     * English: Keep raw coordinates as IDs; insert before complete tags, characters or entities. */
    size_t mapped = 0;
    for (size_t at = 0; at < len;) {
        size_t n = book_mobi_token(raw + at, len - at, true, &t);
        while (mapped < target_count && targets[mapped].raw < at + n) {
            size_t pos = targets[mapped].raw;
            if (t.tag) pos = at;
            else {
                if (enc == 65001) while (pos > at && ((uint8_t)raw[pos] & 0xc0) == 0x80) --pos;
                size_t begin = pos > at + 32 ? pos - 32 : at;
                for (size_t j = pos; j > begin;) {
                    char c = raw[--j];
                    if (c == ';' || isspace((unsigned char)c)) break;
                    if (c == '&') { pos = j; break; }
                }
            }
            targets[mapped++].effective = (uint32_t)pos;
        }
        at += n;
    }
    output_t normalized = {0};
    size_t next = 0;
    esp_err_t err = ESP_OK;
    for (size_t at = 0; at < len;) {
        size_t n = book_mobi_token(raw + at, len - at, true, &t);
        while (next < target_count && targets[next].effective <= at) {
            char id[64];
            int count = snprintf(id, sizeof(id), "<a id=\"mobi-%u\"></a>", (unsigned)(start + targets[next++].raw));
            if (!append(&normalized, id, (size_t)count)) { err = ESP_ERR_NO_MEM; goto done; }
        }
        if (!t.tag && next < target_count && targets[next].effective < at + n)
            n = targets[next].effective - at;
        const char *v; size_t vn; uint32_t num;
        char replacement[128]; int count = 0;
        if (!strcmp(t.name, "mbp:pagebreak")) count = snprintf(replacement, sizeof(replacement), "<br/>");
        else if (!strcmp(t.name, "img") && !t.closing && book_mobi_attr(&t, "recindex", &v, &vn)) {
            if (book_mobi_number(v, vn, &num) && num && first_image < records && num <= records - first_image)
                count = snprintf(replacement, sizeof(replacement), "<img src=\"mobi-%u\"/>", (unsigned)(first_image + num - 1));
            else count = snprintf(replacement, sizeof(replacement), " ");
        } else if (!strcmp(t.name, "a") && book_mobi_attr(&t, "filepos", &v, &vn)) {
            if (book_mobi_number(v, vn, &num) && num >= start && num - start < len)
                count = snprintf(replacement, sizeof(replacement), "<a href=\"#mobi-%u\">", (unsigned)num);
            else count = snprintf(replacement, sizeof(replacement), "<a>");
        }
        if (count) {
            if (!append(&normalized, replacement, (size_t)count)) { err = ESP_ERR_NO_MEM; goto done; }
        } else {
            char *utf8 = NULL; size_t bytes = 0;
            err = book_mobi_decode(raw + at, n, enc, &utf8, &bytes);
            if (err != ESP_OK) goto done;
            bool ok = append(&normalized, utf8, bytes);
            free(utf8);
            if (!ok) { err = ESP_ERR_NO_MEM; goto done; }
        }
        at += n;
    }
    char target[48];
    if (!anchor && source_offset < len) { snprintf(target, sizeof(target), "mobi-%u", (unsigned)(start + source_offset)); anchor = target; }
    err = html_to_blocks_with_css_anchor(normalized.s ? normalized.s : "", normalized.len,
                                         NULL, 0, anchor, text_offset, out);
done:
    free(targets); free(normalized.s);
    return err;
}
