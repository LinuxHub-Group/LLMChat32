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

/* Pre-mix RGBA pixels over white at a global opacity, producing RGB24.
 * rgba points at 4*npixels bytes in R,G,B,A order; the per-pixel alpha
 * is IGNORED - opacity replaces it as the single mix factor. rgb
 * receives 3*npixels bytes in R,G,B order (swap to B,G,R when filling
 * a bottom-up 24-bit DIB). Per byte, with o = opacity clamped 0..100:
 *     out = (c*o + 255*(100-o) + 50) / 100        (round half up)
 * opacity 0 -> pure white, 100 -> the original colors. Pure integer
 * arithmetic (peak ~25.6k, far from INT_MAX); no GDI handles involved. */
void draw_blend_white(const unsigned char *rgba, int npixels, int opacity,
                      unsigned char *rgb);

/* Cached UI font (see ui_font below). */
HFONT ui_font(void);

/* Uncached font creation at point_size (a positive size is negated into
 * a pixel cell height, like the historic CreateFontW call sites). NT:
 * CreateFontW with the configured face name (DEFAULT_CHARSET keeps 中文
 * resolving on zh systems). 9x: CreateFontA on the CP_ACP conversion of
 * the face name (CreateFontW is a stub there) - configured names are
 * ASCII ("SimSun" default), so both paths pick the same face. The caller
 * owns the returned HFONT (may be NULL on GDI failure - SelectObject
 * sites must guard). */
HFONT ui_make_font(int point_size);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_DRAW_H */
