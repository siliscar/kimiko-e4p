/* SPDX-License-Identifier: LGPL-3.0-or-later
 * PalmDOC decoder adapted from libmobi, Copyright (c) 2014 Bartek Fabiszewski.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mobi_codec_palmdoc(uint8_t *out, size_t *out_len, const uint8_t *in, size_t in_len);
