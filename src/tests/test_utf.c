/*
 * test_utf.c - UTF-8 <-> UTF-16 conversion and iteration unit tests.
 *
 * Console app (built with -bc). Verifies the exact assertions from the
 * task spec: "中文" -> UTF-16 units 4E2D 6587, code-point counting,
 * U+1F600 encode/decode including surrogate pairs, rejection of invalid
 * or truncated UTF-8, and round-trip identity for a mixed string.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_utf.exe ^
 *          src\tests\test_utf.c src\core\utf.c
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../core/utf.h"

static int g_fail = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (cond) {                                                  \
            printf("ok %s\n", #cond);                                \
        } else {                                                     \
            printf("FAIL %s (line %d)\n", #cond, __LINE__);          \
            g_fail++;                                                \
        }                                                            \
    } while (0)

int main(void)
{
    wchar_t wbuf[64];
    char    ubuf[64];
    unsigned cp;
    int n;

    /* "中文" -> UTF-16 units 4E2D 6587 */
    memset(wbuf, 0, sizeof wbuf);
    n = utf8_to_utf16("\xe4\xb8\xad\xe6\x96\x87", wbuf, 64);
    CHECK(n == 2);
    CHECK(wbuf[0] == (wchar_t)0x4E2D);
    CHECK(wbuf[1] == (wchar_t)0x6587);
    CHECK(wbuf[2] == (wchar_t)0);

    /* code-point count */
    CHECK(utf8_char_count("\xe4\xb8\xad" "a" "\xe6\x96\x87") == 3);

    /* U+1F600 -> F0 9F 98 80 */
    memset(ubuf, 0, sizeof ubuf);
    n = utf8_encode(ubuf, 0x1F600u);
    CHECK(n == 4);
    CHECK((unsigned char)ubuf[0] == 0xF0u);
    CHECK((unsigned char)ubuf[1] == 0x9Fu);
    CHECK((unsigned char)ubuf[2] == 0x98u);
    CHECK((unsigned char)ubuf[3] == 0x80u);

    /* U+1F600 -> surrogate pair D83D DE00 */
    memset(wbuf, 0, sizeof wbuf);
    n = utf8_to_utf16("\xf0\x9f\x98\x80", wbuf, 64);
    CHECK(n == 2);
    CHECK(wbuf[0] == (wchar_t)0xD83D);
    CHECK(wbuf[1] == (wchar_t)0xDE00);

    /* decode_next on the 4-byte sequence */
    cp = 0;
    n = utf8_decode_next("\xf0\x9f\x98\x80", 4, &cp);
    CHECK(n == 4);
    CHECK(cp == 0x1F600u);

    /* invalid sequences */
    cp = 0;
    CHECK(utf8_decode_next("\xff", 1, &cp) == 0);
    cp = 0;
    CHECK(utf8_decode_next("\xe4\xb8", 2, &cp) == 0);
    CHECK(utf8_valid("\xff") == 0);
    CHECK(utf8_valid("\xe4\xb8") == 0);
    CHECK(utf8_to_utf16("\xff", wbuf, 64) == -1);
    CHECK(utf8_to_utf16("\xe4\xb8", wbuf, 64) == -1);
    CHECK(utf8_valid("\xe4\xb8\xad\xe6\x96\x87") == 1);

    /* buffer too small for a surrogate pair: pair must not be split */
    memset(wbuf, 0x7F, sizeof wbuf);
    n = utf8_to_utf16("\xf0\x9f\x98\x80", wbuf, 1);
    CHECK(n == 0);
    CHECK(wbuf[0] != (wchar_t)0xD83D);

    /* truncation-safe UTF-16 -> UTF-8 */
    memset(ubuf, 0, sizeof ubuf);
    n = utf16_to_utf8(L"abc", ubuf, 2);
    CHECK(n == 1);
    CHECK(ubuf[0] == 'a');
    CHECK(ubuf[1] == '\0');

    /* round trip: mixed ASCII / 中文 / emoji */
    {
        static const char mixed[] =
            "a\xe4\xb8\xadZ\xf0\x9f\x98\x80\xe6\x96\x87!";
        const int mlen = (int)sizeof(mixed) - 1;

        memset(wbuf, 0, sizeof wbuf);
        n = utf8_to_utf16(mixed, wbuf, 64);
        CHECK(n == 7); /* a 中 Z emoji(2 units) 文 ! */

        memset(ubuf, 0, sizeof ubuf);
        n = utf16_to_utf8(wbuf, ubuf, 64);
        CHECK(n == mlen);
        CHECK(memcmp(ubuf, mixed, (size_t)mlen) == 0);
        CHECK(ubuf[mlen] == '\0');
    }

    if (g_fail == 0) {
        printf("PASS test_utf\n");
        return 0;
    }
    printf("FAIL test_utf (%d failures)\n", g_fail);
    return 1;
}
