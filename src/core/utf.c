/*
 * utf.c - UTF-8 <-> UTF-16 conversion and iteration.
 *
 * Strict UTF-8 validation: rejects overlong encodings, surrogate code
 * points (U+D800..U+DFFF), and values above U+10FFFF. Conversion into a
 * bounded output buffer never writes past cap-1, always NUL-terminates
 * when cap > 0, and never splits a UTF-16 surrogate pair.
 */
#include "utf.h"

#include <string.h>

/* continuation byte check */
#define IS_CONT(b) (((b) & 0xC0) == 0x80)

/* encoded length of a scalar value, 0 if invalid */
static int utf8_size(unsigned cp)
{
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        return 0;
    if (cp < 0x80u)
        return 1;
    if (cp < 0x800u)
        return 2;
    if (cp < 0x10000u)
        return 3;
    return 4;
}

int utf8_decode_next(const char *s, int len, unsigned *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned b0, b1, b2, b3;

    if (s == 0 || len < 1 || cp == 0)
        return 0;
    b0 = p[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (len < 2 || !IS_CONT(p[1]))
            return 0;
        *cp = ((b0 & 0x1Fu) << 6) | (unsigned)(p[1] & 0x3Fu);
        return 2;
    }
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        if (len < 2)
            return 0;
        b1 = p[1];
        /* E0: second byte A0..BF (no overlong); ED: 80..9F (no surrogates) */
        if (b0 == 0xE0) {
            if (b1 < 0xA0 || b1 > 0xBF)
                return 0;
        } else if (b0 == 0xED) {
            if (b1 < 0x80 || b1 > 0x9F)
                return 0;
        } else if (!IS_CONT(b1)) {
            return 0;
        }
        if (len < 3 || !IS_CONT(p[2]))
            return 0;
        b2 = p[2];
        *cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        return 3;
    }
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        if (len < 2)
            return 0;
        b1 = p[1];
        /* F0: second byte 90..BF (no overlong); F4: 80..8F (<= U+10FFFF) */
        if (b0 == 0xF0) {
            if (b1 < 0x90 || b1 > 0xBF)
                return 0;
        } else if (b0 == 0xF4) {
            if (b1 < 0x80 || b1 > 0x8F)
                return 0;
        } else if (!IS_CONT(b1)) {
            return 0;
        }
        if (len < 3 || !IS_CONT(p[2]))
            return 0;
        b2 = p[2];
        if (len < 4 || !IS_CONT(p[3]))
            return 0;
        b3 = p[3];
        *cp = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12)
            | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
        return 4;
    }
    return 0;
}

int utf8_encode(char *out, unsigned cp)
{
    if (out == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        return 0;
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

int utf8_char_count(const char *s)
{
    int count = 0;
    int step;
    unsigned cp;

    if (s == 0)
        return 0;
    while (*s != '\0') {
        step = utf8_decode_next(s, (int)strlen(s), &cp);
        if (step == 0)
            break;
        count++;
        s += step;
    }
    return count;
}

int utf8_valid(const char *s)
{
    int step;
    unsigned cp;

    if (s == 0)
        return 0;
    while (*s != '\0') {
        step = utf8_decode_next(s, (int)strlen(s), &cp);
        if (step == 0)
            return 0;
        s += step;
    }
    return 1;
}

int utf8_to_utf16(const char *s, wchar_t *out, int cap)
{
    int written = 0;
    int step;
    unsigned cp;

    if (s == 0 || (out == 0 && cap > 0))
        return -1;
    while (*s != '\0') {
        step = utf8_decode_next(s, (int)strlen(s), &cp);
        if (step == 0)
            return -1;
        if (cp <= 0xFFFFu) {
            if (cap - written < 2)
                break;          /* unit + room for the NUL terminator */
            out[written++] = (wchar_t)cp;
        } else {
            unsigned v = cp - 0x10000u;
            wchar_t hi = (wchar_t)(0xD800u + (v >> 10));
            wchar_t lo = (wchar_t)(0xDC00u + (v & 0x3FFu));

            if (cap - written < 3)
                break;          /* never split a surrogate pair */
            out[written++] = hi;
            out[written++] = lo;
        }
        s += step;
    }
    if (cap > 0)
        out[written] = (wchar_t)0;
    return written;
}

int utf16_to_utf8(const wchar_t *s, char *out, int cap)
{
    int written = 0;
    unsigned cp;
    int n;

    if (s == 0 || (out == 0 && cap > 0))
        return -1;
    while (*s != (wchar_t)0) {
        cp = (unsigned)*s;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            unsigned lo;

            if (s[1] < (wchar_t)0xDC00 || s[1] > (wchar_t)0xDFFF)
                return -1;      /* lone high surrogate */
            lo = (unsigned)s[1];
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            s += 2;
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return -1;          /* lone low surrogate */
        } else {
            s++;
        }
        n = utf8_size(cp);
        if (n == 0)
            return -1;
        if (cap - written < n + 1)
            break;              /* bytes + room for the NUL terminator */
        written += utf8_encode(out + written, cp);
    }
    if (cap > 0)
        out[written] = '\0';
    return written;
}
