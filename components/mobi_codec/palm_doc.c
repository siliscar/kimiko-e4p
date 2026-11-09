/*
 * Copyright (c) 2014 Bartek Fabiszewski, http://www.fabiszewski.net
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Derived from libmobi src/compression.c and src/buffer.c at
 * 906274205c11944b628da1c553b255acb1af7c55 (0.12).
 * Modified 2026: use caller-owned bounded buffers without heap allocation;
 * omit HUFF/CDIC and reject zero-distance or truncated back references.
 * 中文：保留上游 PalmDOC 算法，仅裁剪缓冲与不支持的编解码路径。
 * English: Retain the upstream PalmDOC algorithm with bounded buffers only.
 */
#include "mobi_codec.h"
#include <string.h>

bool mobi_codec_palmdoc(uint8_t *out, size_t *out_len, const uint8_t *in, size_t in_len) {
    if (!out || !out_len || (!in && in_len)) return false;
    size_t at = 0, used = 0, cap = *out_len;
    *out_len = 0;
    while (at < in_len) {
        uint8_t byte = in[at++];
        if (byte >= 0xc0) {
            if (cap - used < 2) return false;
            out[used++] = ' ';
            out[used++] = byte ^ 0x80;
        } else if (byte >= 0x80) {
            if (at == in_len) return false;
            uint8_t next = in[at++];
            uint16_t distance = ((((uint16_t)byte << 8) | next) >> 3) & 0x7ff;
            uint8_t length = (next & 7) + 3;
            if (!distance || distance > used || length > cap - used) return false;
            while (length--) {
                out[used] = out[used - distance];
                ++used;
            }
        } else if (byte >= 0x09 || !byte) {
            if (used == cap) return false;
            out[used++] = byte;
        } else {
            if (byte > in_len - at || byte > cap - used) return false;
            memcpy(out + used, in + at, byte);
            used += byte;
            at += byte;
        }
    }
    *out_len = used;
    return true;
}
