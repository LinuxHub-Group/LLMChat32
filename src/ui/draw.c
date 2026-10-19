/*
 * draw.c - bubble rendering and the shared UI font (see draw.h).
 *
 * draw_bubble fills the tail polygon and the RoundRect body with one
 * brush; the AI (white) bubble is then framed with a 1px silver pen and
 * the tail is filled a second time so the frame's bottom edge never
 * crosses the tail. Stock objects (NULL_PEN / NULL_BRUSH) are used for
 * the shape passes so no per-call GDI object churn beyond brush+pen.
 *
 * ui_font() caches its HFONT keyed by (font_size, font_name); the face
 * name arrives through utf8_to_utf16 like every other UI string.
 */
#include "draw.h"

#include <string.h>
#include <windows.h>

#include "../core/config.h"
#include "../core/utf.h"

#define BUBBLE_RADIUS 8
#define TAIL_OFF      10   /* tail base distance from the side edge */

void draw_bubble(HDC memdc, const RECT *rc, int left_side, COLORREF fill)
{
    HBRUSH brush;
    HGDIOBJ old_brush;
    HGDIOBJ old_pen;
    RECT body;
    POINT tail[3];
    int framed;

    if (memdc == NULL || rc == NULL)
        return;
    if (rc->right - rc->left < 2 * BUBBLE_RADIUS + LLM_BUBBLE_TAIL_W / 2
        || rc->bottom - rc->top < BUBBLE_RADIUS + LLM_BUBBLE_TAIL_H)
        return;

    body = *rc;
    body.bottom -= LLM_BUBBLE_TAIL_H;
    framed = (fill == LLM_COLOR_AI);

    if (left_side) {
        tail[0].x = body.left + TAIL_OFF;
        tail[0].y = body.bottom - 1;
        tail[1].x = tail[0].x + LLM_BUBBLE_TAIL_W;
        tail[1].y = tail[0].y;
        tail[2].x = body.left + TAIL_OFF - 5;
        tail[2].y = body.bottom + LLM_BUBBLE_TAIL_H - 1;
    } else {
        tail[0].x = body.right - TAIL_OFF - LLM_BUBBLE_TAIL_W;
        tail[0].y = body.bottom - 1;
        tail[1].x = tail[0].x + LLM_BUBBLE_TAIL_W;
        tail[1].y = tail[0].y;
        tail[2].x = body.right - TAIL_OFF + 5;
        tail[2].y = body.bottom + LLM_BUBBLE_TAIL_H - 1;
    }

    brush = CreateSolidBrush(fill);
    old_brush = SelectObject(memdc, brush);
    old_pen = SelectObject(memdc, GetStockObject(NULL_PEN));

    Polygon(memdc, tail, 3);
    RoundRect(memdc, body.left, body.top, body.right, body.bottom,
              BUBBLE_RADIUS, BUBBLE_RADIUS);

    if (framed) {
        HPEN pen;
        HGDIOBJ old_pen2;

        pen = CreatePen(PS_SOLID, 1, LLM_COLOR_AIEDGE);
        old_pen2 = SelectObject(memdc, pen);
        SelectObject(memdc, GetStockObject(NULL_BRUSH));
        RoundRect(memdc, body.left, body.top, body.right, body.bottom,
                  BUBBLE_RADIUS, BUBBLE_RADIUS);
        Polygon(memdc, tail, 3);   /* cover the frame's bottom edge */
        SelectObject(memdc, old_pen2);
        DeleteObject(pen);
    }

    SelectObject(memdc, old_pen);
    SelectObject(memdc, old_brush);
    DeleteObject(brush);
}

HFONT ui_font(void)
{
    static HFONT s_font = NULL;
    static int s_size = -1;
    static char s_name[64] = "";
    int size;
    const char *name;
    wchar_t wface[64];

    size = g_cfg.g.font_size;
    if (size <= 0)
        size = 9;
    name = g_cfg.g.font_name;
    if (name == NULL || name[0] == '\0')
        name = "MS Shell Dlg";

    if (s_font != NULL && size == s_size && strcmp(s_name, name) == 0)
        return s_font;

    if (s_font != NULL) {
        DeleteObject(s_font);
        s_font = NULL;
    }

    if (utf8_to_utf16(name, wface, 64) < 0)
        wface[0] = (wchar_t)0;

    s_font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, wface);
    if (s_font != NULL) {
        s_size = size;
        strncpy(s_name, name, sizeof s_name - 1);
        s_name[sizeof s_name - 1] = '\0';
    }
    return s_font;
}
