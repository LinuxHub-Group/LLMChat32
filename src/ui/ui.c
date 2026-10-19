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

#include "../core/utf.h"

#define UI_WIDE_CAP 1024   /* UTF-16 units */
#define UI_ACP_CAP  2048   /* ANSI bytes  */

/* ------------------------------------------------------------------ */
/* language                                                            */

static int g_lang_is_zh = 1;

void ui_lang_init(void)
{
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
    DrawTextW(hdc, w, -1, rc, fmt);
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

    to_acp(utf8, buf, UI_ACP_CAP);
    SetWindowTextA(hwnd, buf);
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
