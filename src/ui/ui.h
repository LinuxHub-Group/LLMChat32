/*
 * ui.h - text output wrappers (the ONLY legal outlets for UI strings).
 *
 * Encoding discipline (project red line): all UI text lives as narrow
 * UTF-8 (see strings.h). It may reach the screen only through these
 * wrappers:
 *
 *   - ui_text_w / ui_drawtext_w: utf8_to_utf16 -> TextOutW / the internal
 *                               word-wrap engine (GetTextExtentPointW +
 *                               ExtTextOutW). DrawTextW is NOT used: 9x
 *                               only stubs it, so every platform draws
 *                               through the engine - NT looks the same.
 *   - ui_drawtext_ww:           the engine on a caller-converted wide
 *                               string (long chat texts, see ui.h)
 *   - ui_set_a / ui_msg_a:      WideCharToMultiByte(CP_ACP) -> SetWindowTextA
 *                               / MessageBoxA
 *   - menu_append_str / menu_append_sep: AppendMenuA with CP_ACP-converted
 *                               menu text (menu_append_sep adds a separator,
 *                               which carries no text)
 *
 * Never pass a raw UTF-8 pointer to any other ANSI API.
 */
#ifndef LLMCHAT32_UI_H
#define LLMCHAT32_UI_H

#include <windows.h>

#include "strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform probe, set by ui_lang_init() (called once at startup):
 * 1 on Windows 95/98/ME (VER_PLATFORM_WIN32_WINDOWS), else 0.
 * UI code branches on it ONLY where a W API is a stub on 9x (font
 * creation, Unicode EDIT controls, SetWindowTextW); text drawing never
 * branches - the engine below uses 9x-real primitives on every OS. */
extern int g_win9x;

/* TextOutW at (x, y). Invalid UTF-8 draws nothing. */
void ui_text_w(HDC hdc, int x, int y, const char *utf8);

/* Engine-based DrawText replacement: draws utf8 into rc with fmt (DT_*).
 * Supported combos (the only ones the codebase uses):
 *   DT_SINGLELINE with DT_LEFT/DT_CENTER/DT_RIGHT, DT_VCENTER/DT_BOTTOM
 *     and DT_END_ELLIPSIS (width-trimmed with U+2026)
 *   DT_WORDBREAK with DT_LEFT, with or without DT_CALCRECT (greedy word
 *     wrap; words run to the next space/tab, a word wider than the rect
 *     breaks at code-point boundaries so CJK wraps per character; \r \n
 *     are hard breaks, "\r\n" counts as one)
 * Invalid UTF-8 draws nothing. DT_CALCRECT returns the extent in rc. */
void ui_drawtext_w(HDC hdc, const char *utf8, RECT *rc, UINT fmt);

/* Same as ui_drawtext_w for a caller-converted wide string. This is the
 * outlet for long texts that would overflow ui_drawtext_w's fixed
 * conversion buffer (chat message bodies); the caller produces w with
 * utf8_to_utf16, so the encoding discipline (no UTF-8 handed to an API)
 * is preserved. */
void ui_drawtext_ww(HDC hdc, const wchar_t *w, RECT *rc, UINT fmt);

/* Pixel width of utf8 under the DC's current font, 0 on failure. */
int ui_text_width(HDC hdc, const char *utf8);

/* SetWindowTextA after converting utf8 to the ANSI code page. */
void ui_set_a(HWND hwnd, const char *utf8);

/* SetWindowTextW on a caller-converted wide string. This is the outlet
 * for long texts that would overflow ui_set_a's fixed conversion buffer
 * (view-dialog editors); the caller produces w with utf8_to_utf16, so
 * the encoding discipline (no UTF-8 handed to an API) is preserved.
 * On 9x (SetWindowTextW is a stub there) the text goes to SetWindowTextA
 * through the CP_ACP outlet instead; an ANSI EDIT round-trips those
 * bytes unchanged. */
void ui_set_ww(HWND hwnd, const wchar_t *w);

/* Convert utf8 into out (cap bytes) as ANSI code page bytes, for A-APIs
 * that take a string argument rather than a window handle (e.g. combo box
 * CB_ADDSTRING/CB_INSERTSTRING). Same truncation rules as ui_set_a. */
void ui_acp_a(char *out, int cap, const char *utf8);

/* MessageBoxA after converting text/title to the ANSI code page.
 * Returns the MessageBoxA result (IDOK/IDYES/...). */
int ui_msg_a(HWND owner, const char *utf8_text, const char *utf8_title,
             UINT flags);

/* AppendMenuA with the LS(sid) text converted to the ANSI code page.
 * flags is the usual MF_STRING/MF_GRAYED/... set. */
void menu_append_str(HMENU menu, UINT flags, UINT id, StrID sid);

/* AppendMenuA(MF_SEPARATOR) - no text is involved. */
void menu_append_sep(HMENU menu);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_H */
