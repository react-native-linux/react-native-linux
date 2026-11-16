/*
 * From T3 Code, apps/mobile/modules/t3-highlights/cpp/t3_highlights.h (https://github.com/pingdotgg/t3code).
 *
 * MIT License
 *
 * Copyright (c) 2026 T3 Tools Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef T3_HIGHLIGHTS_H
#define T3_HIGHLIGHTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lexes UTF-8 `source` with @pierre/highlights language `language` and points
 * `records` at `(endUtf16, tokenId)` pairs. A token ID of UINT32_MAX ends a
 * line, and its end value is where the next line starts. The records stay
 * valid until the next call. Returns the number of uint32 values, or -1 when
 * the lexer could not run.
 *
 * Not thread-safe: callers serialize access.
 */
int64_t t3_highlights_tokenize(const uint8_t* source, size_t length, uint8_t language, const uint32_t** records);

#ifdef __cplusplus
}
#endif

#endif
