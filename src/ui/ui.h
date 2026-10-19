/*
 * ui.h - text output wrappers (the ONLY legal outlets for UI strings).
 *
 * Encoding discipline (project red line): all UI text lives as narrow
 * UTF-8 (see strings.h). It may reach the screen only through these
 * wrappers:
 *
 *   - ui_text_w / ui_drawtext_w: utf8_to_utf16 -> TextOutW / DrawTextW
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

/* TextOutW at (x, y). Invalid UTF-8 draws nothing. */
void ui_text_w(HDC hdc, int x, int y, const char *utf8);

/* DrawTextW into rc with fmt (DT_*). Invalid UTF-8 draws nothing. */
void ui_drawtext_w(HDC hdc, const char *utf8, RECT *rc, UINT fmt);

/* Pixel width of utf8 under the DC's current font, 0 on failure. */
int ui_text_width(HDC hdc, const char *utf8);

/* SetWindowTextA after converting utf8 to the ANSI code page. */
void ui_set_a(HWND hwnd, const char *utf8);

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
