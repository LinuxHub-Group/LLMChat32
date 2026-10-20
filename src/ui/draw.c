/*
 * draw.c - bubble rendering, RGBA-over-white blending and the shared
 * UI font (see draw.h).
 *
 * draw_bubble fills the tail polygon and the RoundRect body with one
 * brush; the AI (white) bubble is then framed with a 1px silver pen and
 * the tail is filled a second time so the frame's bottom edge never
 * crosses the tail. Stock objects (NULL_PEN / NULL_BRUSH) are used for
 * the shape passes so no per-call GDI object churn beyond brush+pen.
 *
 * ui_font() caches its HFONT keyed by (font_size, font_name); creation
 * goes through ui_make_font(), which picks CreateFontW on NT and
 * CreateFontA (CP_ACP face name) on Windows 9x, where CreateFontW is a
 * stub (see draw.h).
 */
#include "draw.h"

#include <string.h>
#include <windows.h>

#include "../core/config.h"
#include "../core/utf.h"
#include "ui.h"

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

void draw_blend_white(const unsigned char *rgba, int npixels, int opacity,
                      unsigned char *rgb)
{
    int i, o, base;

    if (rgba == NULL || rgb == NULL || npixels <= 0)
        return;
    if (opacity < 0)
        opacity = 0;
    if (opacity > 100)
        opacity = 100;
    o = opacity;
    base = 255 * (100 - o) + 50;   /* white part + round-half-up bias */
    for (i = 0; i < npixels; i++) {
        const unsigned char *s = rgba + (size_t)i * 4;
        unsigned char *d = rgb + (size_t)i * 3;

        d[0] = (unsigned char)((s[0] * o + base) / 100);
        d[1] = (unsigned char)((s[1] * o + base) / 100);
        d[2] = (unsigned char)((s[2] * o + base) / 100);
    }
}

HFONT ui_make_font(int point_size)
{
    const char *name = g_cfg.g.font_name;
    int h = point_size < 0 ? point_size : -point_size;

    if (name == NULL || name[0] == '\0')
        name = "MS Shell Dlg";
    if (g_win9x) {
        /* CreateFontW is a stub on 9x; the CP_ACP outlet turns the
         * configured UTF-8 face name into the bytes CreateFontA wants
         * (ASCII names pass through unchanged) */
        char face[64];

        ui_acp_a(face, (int)sizeof face, name);
        return CreateFontA(h, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    }
    {
        wchar_t wface[64];

        if (utf8_to_utf16(name, wface, 64) < 0)
            wface[0] = (wchar_t)0;
        return CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, wface);
    }
}

HFONT ui_font(void)
{
    static HFONT s_font = NULL;
    static int s_size = -1;
    static char s_name[64] = "";
    int size;
    const char *name;

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

    s_font = ui_make_font(size);
    if (s_font != NULL) {
        s_size = size;
        strncpy(s_name, name, sizeof s_name - 1);
        s_name[sizeof s_name - 1] = '\0';
    }
    return s_font;
}
