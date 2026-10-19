/*
 * utf.h - UTF-8 <-> UTF-16 conversion and iteration.
 *
 * wchar_t is 16-bit here (Win32/Win98), so UTF-16 output uses surrogate
 * pairs for code points above U+FFFF. All functions are C89 and work on
 * narrow strings holding raw UTF-8 bytes.
 */
#ifndef LLMCHAT32_UTF_H
#define LLMCHAT32_UTF_H

#include <stddef.h>     /* wchar_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Convert UTF-8 to UTF-16. Writes at most cap-1 UTF-16 units plus a
 * terminating NUL when cap > 0. A surrogate pair is never split: if the
 * remaining capacity cannot hold both units the pair is dropped.
 * Returns the number of UTF-16 units written (excluding NUL), or -1 if
 * the input is not valid UTF-8. */
int utf8_to_utf16(const char *s, wchar_t *out, int cap);

/* Convert UTF-16 to UTF-8. Truncation-safe: at most cap-1 bytes plus a
 * terminating NUL are written. Returns the number of bytes written
 * (excluding NUL), or -1 if the input contains a lone surrogate. */
int utf16_to_utf8(const wchar_t *s, char *out, int cap);

/* Decode one code point from the len bytes at s. Returns the number of
 * bytes consumed, or 0 if the sequence is invalid (bad lead byte,
 * truncated continuation, overlong, or out of range). */
int utf8_decode_next(const char *s, int len, unsigned *cp);

/* Encode cp as UTF-8 at out (no NUL appended). Returns bytes written,
 * or 0 if cp is not a valid scalar value (surrogates, > U+10FFFF). */
int utf8_encode(char *out, unsigned cp);

/* Number of code points in the valid-at-start UTF-8 string s.
 * Stops at the first invalid sequence or the terminating NUL. */
int utf8_char_count(const char *s);

/* 1 if s is entirely valid UTF-8, 0 otherwise. */
int utf8_valid(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UTF_H */
