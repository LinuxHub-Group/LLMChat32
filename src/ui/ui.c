/*
 * ui.c - text output wrappers and language selection.
 *
 * Implements ui.h. Every conversion outlet funnels through two static
 * helpers: to_wide() (utf8_to_utf16) for the W-APIs and to_acp()
 * (utf8_to_utf16 + WideCharToMultiByte(CP_ACP)) for the A-APIs. Both
 * truncate to a fixed stack buffer - long strings are cut, never split
 * mid-surrogate (utf8_to_utf16 guarantees that), and always NUL-terminate.
 */
#define STRINGS_DEFINE_TABLES
#include "strings.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "../core/utf.h"

#define UI_WIDE_CAP 1024   /* UTF-16 units */
#define UI_ACP_CAP  2048   /* ANSI bytes  */

/* ------------------------------------------------------------------ */
/* language + platform probe                                           */

static int g_lang_is_zh = 1;

/* 1 on Windows 95/98/ME. Set once by ui_lang_init() (the earliest UI
 * init point, called from main before any window exists). See ui.h. */
int g_win9x = 0;

void ui_lang_init(void)
{
    OSVERSIONINFOA osvi;

    memset(&osvi, 0, sizeof osvi);
    osvi.dwOSVersionInfoSize = sizeof osvi;
    if (GetVersionExA(&osvi))
        g_win9x = (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS);
    g_lang_is_zh = ((GetSystemDefaultLangID() & 0x3FF) == 0x04);
}

const char *LS(StrID id)
{
    if (id < 0 || id >= S__COUNT)
        return "";
    return g_lang_is_zh ? g_strs_zh[id] : g_strs_en[id];
}

/* ------------------------------------------------------------------ */
/* conversions                                                         */

/* UTF-8 -> UTF-16. Returns 1 on success (w NUL-terminated), 0 on
 * invalid UTF-8 or an empty conversion buffer. */
static int to_wide(const char *utf8, wchar_t *w, int cap)
{
    if (cap < 1)
        return 0;
    if (utf8_to_utf16(utf8, w, cap) < 0) {
        w[0] = L'\0';
        return 0;
    }
    return 1;
}

/* UTF-8 -> ANSI code page (via UTF-16; CP_ACP has no direct entry).
 * Falls back to a plain ASCII copy on conversion failure. out is always
 * NUL-terminated when cap > 0. */
static void to_acp(const char *utf8, char *out, int cap)
{
    wchar_t w[UI_WIDE_CAP];
    int wlen;

    if (cap < 1)
        return;
    out[0] = '\0';
    if (!to_wide(utf8, w, UI_WIDE_CAP))
        return;
    wlen = lstrlenW(w);
    if (wlen < 1)
        return;
    WideCharToMultiByte(CP_ACP, 0, w, wlen + 1, out, cap, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* text engine                                                         */
/*                                                                     */
/* DrawTextW is a silently-failing stub on Windows 9x, so ALL drawing  */
/* goes through this engine, built on the two W primitives 9x really   */
/* implements (GetTextExtentPointW, ExtTextOutW). It covers exactly    */
/* the DT_* combos the call sites use - see ui.h for the list. Line    */
/* metrics follow DrawText: line height = tm.tmHeight +                */
/* tm.tmExternalLeading; CALCRECT returns left/top plus width/height.  */

/* Width of the n-unit prefix of w under hdc's font, 0 on failure. */
static int ui_seg_w(HDC hdc, const wchar_t *w, int n)
{
    SIZE sz;

    if (n <= 0)
        return 0;
    if (!GetTextExtentPointW(hdc, w, n, &sz))
        return 0;
    return (int)sz.cx;
}

/* DrawText-style line height, 16 on metrics failure. */
static int ui_line_h(HDC hdc)
{
    TEXTMETRIC tm;

    if (!GetTextMetrics(hdc, &tm))
        return 16;
    return tm.tmHeight + tm.tmExternalLeading;
}

/* DT_SINGLELINE path: one line, horizontal DT_LEFT/CENTER/RIGHT,
 * vertical DT_VCENTER/BOTTOM, optional DT_END_ELLIPSIS trimming. */
static void ui_draw_single(HDC hdc, const wchar_t *w, RECT *rc, UINT fmt)
{
    wchar_t buf[UI_WIDE_CAP + 2];
    int n = lstrlenW(w);
    int availw = rc->right - rc->left;
    int lh = ui_line_h(hdc);
    int dw;

    if ((fmt & DT_END_ELLIPSIS) && availw >= 0
        && ui_seg_w(hdc, w, n) > availw) {
        /* largest prefix that still leaves room for U+2026 (prefix
         * widths grow monotonically, so binary search is safe) */
        int lo = 0, hi = n;
        int ew = ui_seg_w(hdc, L"\x2026", 1);

        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;

            if (ui_seg_w(hdc, w, mid) + ew <= availw)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo > UI_WIDE_CAP)
            lo = UI_WIDE_CAP;
        memcpy(buf, w, (size_t)lo * sizeof(wchar_t));
        buf[lo] = (wchar_t)0x2026;
        buf[lo + 1] = L'\0';
        w = buf;
        n = lo + 1;
    }
    dw = ui_seg_w(hdc, w, n);
    if (fmt & DT_CALCRECT) {
        rc->right = rc->left + dw;
        rc->bottom = rc->top + lh;
        return;
    }
    {
        int x = rc->left;
        int y = rc->top;

        if (fmt & DT_CENTER)
            x = rc->left + (availw - dw) / 2;
        else if (fmt & DT_RIGHT)
            x = rc->right - dw;
        if (fmt & DT_VCENTER)
            y = rc->top + (rc->bottom - rc->top - lh) / 2;
        else if (fmt & DT_BOTTOM)
            y = rc->bottom - lh;
        ExtTextOutW(hdc, x, y, ETO_CLIPPED, rc, w, n, NULL);
    }
}

/* One wrapped line: widen the running max, count the line, and draw it
 * (unless calc_only) at its stacked y. */
static void ui_emit_line(HDC hdc, const wchar_t *w, int start, int len,
                         RECT *rc, int lh, int calc_only, int *maxw,
                         int *nlines)
{
    int lw = ui_seg_w(hdc, w + start, len);

    if (lw > *maxw)
        *maxw = lw;
    if (!calc_only)
        ExtTextOutW(hdc, rc->left, rc->top + *nlines * lh, ETO_CLIPPED,
                    rc, w + start, len, NULL);
    (*nlines)++;
}

/* Greedy word wrap over w[0..n) at width avail: a word runs to the next
 * space/tab; a word that alone exceeds the width is broken at code-point
 * boundaries (this is what makes CJK text wrap per character, matching
 * DrawText on both 9x and NT). '\r' and '\n' are hard breaks, "\r\n"
 * counts as one. calc_only measures (*maxw/*nlines) without drawing. */
static void ui_wrap_walk(HDC hdc, const wchar_t *w, int n, int avail,
                         RECT *rc, int lh, int calc_only, int *maxw,
                         int *nlines)
{
    int i = 0;
    int line_start = 0;

    while (i < n) {
        wchar_t c = w[i];

        if (c == L'\r' || c == L'\n') {
            ui_emit_line(hdc, w, line_start, i - line_start, rc, lh,
                         calc_only, maxw, nlines);
            if (c == L'\r' && i + 1 < n && w[i + 1] == L'\n')
                i++;
            line_start = i + 1;
            i = line_start;
            continue;
        }
        {
            /* candidate = current line through this word plus its
             * trailing breakables */
            int word_end = i;
            int k;

            while (word_end < n && w[word_end] != L' '
                   && w[word_end] != L'\t' && w[word_end] != L'\r'
                   && w[word_end] != L'\n')
                word_end++;
            k = word_end;
            while (k < n && (w[k] == L' ' || w[k] == L'\t'))
                k++;
            if (ui_seg_w(hdc, w + line_start, k - line_start) <= avail) {
                i = k;   /* word fits: extend the line */
                continue;
            }
            if (line_start == i) {
                /* the word alone is wider than the whole line: break it
                 * at code-point boundaries */
                int lo = i, hi = word_end;

                while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;

                    if (ui_seg_w(hdc, w + line_start,
                                 mid - line_start) <= avail)
                        lo = mid;
                    else
                        hi = mid - 1;
                }
                if (lo == line_start)
                    lo = line_start + 1;   /* place one char regardless */
                ui_emit_line(hdc, w, line_start, lo - line_start, rc, lh,
                             calc_only, maxw, nlines);
                line_start = lo;
                i = lo;
                continue;
            }
            /* word does not fit in the remaining space: wrap before it
             * (the breakables before it end this line) */
            ui_emit_line(hdc, w, line_start, i - line_start, rc, lh,
                         calc_only, maxw, nlines);
            line_start = i;
        }
    }
    ui_emit_line(hdc, w, line_start, n - line_start, rc, lh, calc_only,
                 maxw, nlines);
}

/* DT_WORDBREAK path. DT_CALCRECT only measures: rc keeps left/top, gets
 * right = left + widest line, bottom = top + nlines * line height. */
static void ui_draw_wrap(HDC hdc, const wchar_t *w, RECT *rc, UINT fmt)
{
    int n = lstrlenW(w);
    int lh = ui_line_h(hdc);
    int avail = rc->right - rc->left;
    int maxw = 0, nlines = 0;

    if (avail < 0)
        avail = 0;
    ui_wrap_walk(hdc, w, n, avail, rc, lh, (fmt & DT_CALCRECT) != 0,
                 &maxw, &nlines);
    if (fmt & DT_CALCRECT) {
        rc->right = rc->left + maxw;
        rc->bottom = rc->top + nlines * lh;
    }
}

static void ui_textout_wrap(HDC hdc, const wchar_t *w, RECT *rc, UINT fmt)
{
    if (hdc == NULL || w == NULL || rc == NULL)
        return;
    if (fmt & DT_WORDBREAK)
        ui_draw_wrap(hdc, w, rc, fmt);
    else
        ui_draw_single(hdc, w, rc, fmt);
}

/* ------------------------------------------------------------------ */
/* W-API outlets                                                       */

void ui_text_w(HDC hdc, int x, int y, const char *utf8)
{
    wchar_t w[UI_WIDE_CAP];

    if (!to_wide(utf8, w, UI_WIDE_CAP))
        return;
    TextOutW(hdc, x, y, w, lstrlenW(w));
}

void ui_drawtext_w(HDC hdc, const char *utf8, RECT *rc, UINT fmt)
{
    wchar_t w[UI_WIDE_CAP];

    if (!to_wide(utf8, w, UI_WIDE_CAP))
        return;
    ui_textout_wrap(hdc, w, rc, fmt);
}

void ui_drawtext_ww(HDC hdc, const wchar_t *w, RECT *rc, UINT fmt)
{
    if (w == NULL || rc == NULL)
        return;
    ui_textout_wrap(hdc, w, rc, fmt);
}

int ui_text_width(HDC hdc, const char *utf8)
{
    wchar_t w[UI_WIDE_CAP];
    SIZE sz;

    if (!to_wide(utf8, w, UI_WIDE_CAP))
        return 0;
    if (!GetTextExtentPointW(hdc, w, lstrlenW(w), &sz))
        return 0;
    return (int)sz.cx;
}

/* ------------------------------------------------------------------ */
/* A-API outlets (CP_ACP conversion - the only legal ones)             */

void ui_set_a(HWND hwnd, const char *utf8)
{
    char buf[UI_ACP_CAP];

    if (hwnd == NULL)
        return;
    to_acp(utf8, buf, UI_ACP_CAP);
    SetWindowTextA(hwnd, buf);
}

void ui_set_ww(HWND hwnd, const wchar_t *w)
{
    int wlen, n;
    char *buf;

    if (hwnd == NULL || w == NULL)
        return;
    if (!g_win9x) {
        SetWindowTextW(hwnd, w);
        return;
    }
    /* 9x: SetWindowTextW is a stub there. The text is already UTF-16;
     * push it through the CP_ACP outlet for SetWindowTextA - an ANSI
     * EDIT round-trips those bytes unchanged. */
    wlen = lstrlenW(w);
    n = WideCharToMultiByte(CP_ACP, 0, w, wlen + 1, NULL, 0, NULL, NULL);
    if (n < 1)
        return;
    buf = (char *)malloc((size_t)n);
    if (buf == NULL)
        return;
    WideCharToMultiByte(CP_ACP, 0, w, wlen + 1, buf, n, NULL, NULL);
    SetWindowTextA(hwnd, buf);
    free(buf);
}

void ui_acp_a(char *out, int cap, const char *utf8)
{
    to_acp(utf8, out, cap);
}

int ui_msg_a(HWND owner, const char *utf8_text, const char *utf8_title,
             UINT flags)
{
    char text[UI_ACP_CAP];
    char title[256];

    to_acp(utf8_text, text, UI_ACP_CAP);
    to_acp(utf8_title, title, sizeof title);
    return MessageBoxA(owner, text, title, flags);
}

void menu_append_str(HMENU menu, UINT flags, UINT id, StrID sid)
{
    char buf[UI_ACP_CAP];

    to_acp(LS(sid), buf, UI_ACP_CAP);
    AppendMenuA(menu, flags, id, buf);
}

void menu_append_sep(HMENU menu)
{
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
}
