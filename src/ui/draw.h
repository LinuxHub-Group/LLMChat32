/*
 * draw.h - shared drawing helpers for the UI layer.
 *
 * Bubble palette, fixed for the whole app. Both fills are members of the
 * Windows halftone 6x6x6 color cube (channel steps 0,51,...,255), so a
 * 256-color (8-bit) display renders them without dithering; #DCF8C6-style
 * off-cube pastels are deliberately avoided:
 *
 *   user bubble       LLM_COLOR_USER   RGB(0xCC,0xFF,0x99)  #CCFF99 (cube 4,5,3)
 *   AI bubble         LLM_COLOR_AI     RGB(0xFF,0xFF,0xFF)  white   (cube 5,5,5)
 *   AI bubble frame   LLM_COLOR_AIEDGE RGB(0xC0,0xC0,0xC0)  silver  (static 20)
 *   inline link text  LLM_COLOR_LINK   RGB(0x00,0x00,0xCC)  blue    (cube 0,0,4)
 *
 * draw_bubble() draws the body as a RoundRect plus a small Polygon tail
 * at the bottom edge; rc must reserve LLM_BUBBLE_TAIL_H extra rows below
 * the body for the tail. A white (AI) bubble gets a 1px silver frame and
 * its tail is filled twice so the frame line never crosses it; a colored
 * (user) bubble is borderless.
 */
#ifndef LLMCHAT32_UI_DRAW_H
#define LLMCHAT32_UI_DRAW_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LLM_COLOR_USER    RGB(0xCC, 0xFF, 0x99)
#define LLM_COLOR_AI      RGB(0xFF, 0xFF, 0xFF)
#define LLM_COLOR_AIEDGE  RGB(0xC0, 0xC0, 0xC0)
#define LLM_COLOR_LINK    RGB(0x00, 0x00, 0xCC)

#define LLM_BUBBLE_TAIL_W  24   /* tail base width                    */
#define LLM_BUBBLE_TAIL_H  7    /* tail height; extra rows below body */

/* Fill the bubble outer rect rc (body + tail) on memdc. left_side puts
 * the tail at the bottom-left edge, otherwise bottom-right. */
void draw_bubble(HDC memdc, const RECT *rc, int left_side, COLORREF fill);

/* Cached UI font: CreateFontW(-g_cfg.g.font_size, DEFAULT_CHARSET) with
 * the face name from g_cfg.g.font_name (fallback "MS Shell Dlg"), both
 * converted/validated at runtime - no L"" literals. The returned HFONT
 * is shared and must NOT be deleted by the caller; it is recreated when
 * the config values change. */
HFONT ui_font(void);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_DRAW_H */
