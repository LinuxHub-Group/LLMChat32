/*
 * tabbar.c - self-drawn tab strip child window (see tabbar.h).
 *
 * Per-tab width = title width (ui_text_width under the tabbar's cached
 * HFONT) + padding + close-glyph area; the "+" tab has a fixed width.
 * Widths are measured once per tabbar_set_sessions() and cached, so both
 * painting and hit-testing are pure arithmetic on the cached values.
 *
 * Hit-testing (WM_LBUTTONDOWN), right to left: "+" rect, close-glyph
 * area of a tab, tab body. Closing asks for confirmation through
 * ui_msg_a and only POSTS WM_APP_TAB_CLOSE - deletion is the owner's
 * job (see header). Drawing is owner-draw with system colors: selected
 * tab white/flush with the content area, unselected button-face.
 */
#include "tabbar.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "mainwin.h"   /* g_hwnd_main */
#include "draw.h"      /* ui_make_font */
#include "strings.h"
#include "ui.h"
#include "../core/diag.h"

#define TABBAR_CLASS   "LlmTabBar"
#define TAB_PAD_X      6     /* title padding, both sides */
#define TAB_CLOSE_W    16    /* close-glyph area width */
#define TAB_MIN_W      48    /* never narrower than this */
#define TAB_EDGE       4     /* left margin */
#define TAB_GAP        1     /* between tabs */
#define TAB_PLUS_W     22    /* "+" tab width */

/* U+00D7 MULTIPLICATION SIGN (x D7), drawn via ui_text_w. */
static const char kCloseGlyph[] = "\xc3\x97";

typedef struct {
    char title[64];      /* copied from Session.title */
    int  w;              /* full tab width incl. close area */
} TabInfo;

/* Single instance - the API in tabbar.h has no per-window state. */
static HWND    s_hwnd = NULL;
static HFONT   s_font = NULL;
static int     s_registered = 0;
static TabInfo *s_tabs = NULL;
static int     s_n = 0;
static int     s_cur = -1;

/* ------------------------------------------------------------------ */
/* geometry (pure arithmetic on cached widths)                        */

static void tab_rect(int i, RECT *rc)
{
    int k;
    int left = TAB_EDGE;

    for (k = 0; k < i; k++)
        left += s_tabs[k].w + TAB_GAP;
    rc->left = left;
    rc->top = (i == s_cur) ? 1 : 3;
    rc->right = left + s_tabs[i].w;
    rc->bottom = TABBAR_HEIGHT - 1;
}

static void plus_rect(RECT *rc)
{
    int k;
    int left = TAB_EDGE;

    for (k = 0; k < s_n; k++)
        left += s_tabs[k].w + TAB_GAP;
    left += 3;
    rc->left = left;
    rc->top = 3;
    rc->right = left + TAB_PLUS_W;
    rc->bottom = TABBAR_HEIGHT - 1;
}

/* Tab index under (x, y), or -1. */
static int tab_at(int x, int y)
{
    int i;
    RECT rc;

    if (y < 0 || y >= TABBAR_HEIGHT)
        return -1;
    for (i = 0; i < s_n; i++) {
        tab_rect(i, &rc);
        if (x >= rc.left && x < rc.right)
            return i;
    }
    return -1;
}

static int point_in_rect(const RECT *rc, int x, int y)
{
    return x >= rc->left && x < rc->right && y >= rc->top && y < rc->bottom;
}

/* ------------------------------------------------------------------ */
/* painting                                                           */

static void frame_tab(HDC hdc, const RECT *rc, int selected)
{
    HPEN pen;
    HGDIOBJ old;

    /* left + top hilight */
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
    old = SelectObject(hdc, pen);
    MoveToEx(hdc, rc->left, rc->bottom - 1, NULL);
    LineTo(hdc, rc->left, rc->top);
    LineTo(hdc, rc->right - 1, rc->top);
    SelectObject(hdc, old);
    DeleteObject(pen);

    /* right + bottom shadow */
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
    old = SelectObject(hdc, pen);
    MoveToEx(hdc, rc->right - 1, rc->top, NULL);
    LineTo(hdc, rc->right - 1, rc->bottom - 1);
    LineTo(hdc, rc->left - 1, rc->bottom - 1);
    SelectObject(hdc, old);
    DeleteObject(pen);

    if (selected) {
        /* open the bottom edge so the tab merges with the content */
        pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOW));
        old = SelectObject(hdc, pen);
        MoveToEx(hdc, rc->left, rc->bottom - 1, NULL);
        LineTo(hdc, rc->right, rc->bottom - 1);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
}

static void draw_tab(HDC hdc, const RECT *rc, const char *title,
                     int selected)
{
    RECT tr = *rc;
    RECT txt;
    HBRUSH brush;

    brush = GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE);
    FillRect(hdc, &tr, brush);
    frame_tab(hdc, &tr, selected);

    /* title (excluding the close area) */
    txt = tr;
    txt.left += TAB_PAD_X;
    txt.right -= TAB_CLOSE_W;
    ui_drawtext_w(hdc, title, &txt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* close glyph, vertically centered in its area */
    txt = tr;
    txt.left = tr.right - TAB_CLOSE_W;
    txt.right = tr.right - 2;
    ui_drawtext_w(hdc, kCloseGlyph, &txt,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_plus(HDC hdc, const RECT *rc)
{
    RECT tr = *rc;
    RECT txt;

    FillRect(hdc, &tr, GetSysColorBrush(COLOR_BTNFACE));
    frame_tab(hdc, &tr, 0);
    txt = tr;
    txt.bottom -= 1;
    ui_drawtext_w(hdc, "+", &txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* ------------------------------------------------------------------ */
/* window proc                                                        */

static LRESULT CALLBACK tabbar_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp)
{
    HDC hdc;
    PAINTSTRUCT ps;
    RECT rc;
    HGDIOBJ old;
    int i;
    int x;
    int y;
    int idx;

    switch (msg) {
    case WM_CREATE:
        /* DEFAULT_CHARSET (inside ui_make_font) so 中文 titles resolve
         * on zh systems; 9x-safe CreateFontA variant */
        s_font = ui_make_font(11);
        diag_font("tab", s_font);
        return 0;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        old = NULL;
        if (s_font != NULL)
            old = SelectObject(hdc, s_font);
        ui_prime_dc(hdc);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        for (i = 0; i < s_n; i++) {
            tab_rect(i, &rc);
            draw_tab(hdc, &rc, s_tabs[i].title, i == s_cur);
        }
        plus_rect(&rc);
        draw_plus(hdc, &rc);
        if (old != NULL)
            SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_LBUTTONDOWN:
        x = (int)(short)LOWORD(lp);
        y = (int)(short)HIWORD(lp);
        plus_rect(&rc);
        if (point_in_rect(&rc, x, y)) {
            PostMessage(g_hwnd_main, WM_APP_TAB_NEW, 0, 0);
            return 0;
        }
        idx = tab_at(x, y);
        if (idx < 0)
            return 0;
        tab_rect(idx, &rc);
        if (x >= rc.right - TAB_CLOSE_W) {
            /* close: confirm, then let the owner delete/refresh */
            if (ui_msg_a(g_hwnd_main, LS(S_CONFIRM_DEL_SESSION),
                         LS(S_MENU_CLOSE_CHAT),
                         MB_YESNO | MB_ICONQUESTION) == IDYES)
                PostMessage(g_hwnd_main, WM_APP_TAB_CLOSE, 0, (LPARAM)idx);
            return 0;
        }
        if (idx != s_cur) {
            s_cur = idx;
            InvalidateRect(hwnd, NULL, FALSE);
            PostMessage(g_hwnd_main, WM_APP_TAB_CHANGED, 0, (LPARAM)idx);
        }
        return 0;

    case WM_DESTROY:
        if (s_font) {
            DeleteObject(s_font);
            s_font = NULL;
        }
        if (s_tabs) {
            free(s_tabs);
            s_tabs = NULL;
        }
        s_n = 0;
        s_cur = -1;
        s_hwnd = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* public API                                                         */

HWND tabbar_create(HWND parent)
{
    HINSTANCE inst;
    WNDCLASSA wc;

    if (parent == NULL)
        return NULL;
    inst = GetModuleHandle(NULL);
    if (!s_registered) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = tabbar_wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = TABBAR_CLASS;
        if (!RegisterClassA(&wc))
            return NULL;
        s_registered = 1;
    }
    s_hwnd = CreateWindowExA(0, TABBAR_CLASS, NULL,
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 0, 0, parent, NULL, inst, NULL);
    return s_hwnd;
}

void tabbar_set_sessions(Session **list, int n)
{
    HDC hdc;
    HGDIOBJ old;
    int i;

    if (s_tabs) {
        free(s_tabs);
        s_tabs = NULL;
    }
    s_n = 0;
    s_cur = -1;
    if (s_hwnd == NULL)
        return;

    if (list != NULL && n > 0) {
        s_tabs = (TabInfo *)malloc(n * sizeof(TabInfo));
        if (s_tabs != NULL) {
            hdc = GetDC(s_hwnd);
            old = NULL;
            if (hdc != NULL && s_font != NULL)
                old = SelectObject(hdc, s_font);
            for (i = 0; i < n; i++) {
                int tw = 0;

                strncpy(s_tabs[i].title,
                        list[i] != NULL ? list[i]->title : "",
                        sizeof s_tabs[i].title - 1);
                diagf("tab: set [%d] title=%02x %02x %02x %02x '%s'",
                      i, (unsigned char)s_tabs[i].title[0],
                      (unsigned char)s_tabs[i].title[1],
                      (unsigned char)s_tabs[i].title[2],
                      (unsigned char)s_tabs[i].title[3],
                      s_tabs[i].title);
                s_tabs[i].title[sizeof s_tabs[i].title - 1] = '\0';
                if (hdc != NULL)
                    tw = ui_text_width(hdc, s_tabs[i].title);
                s_tabs[i].w = tw + 2 * TAB_PAD_X + TAB_CLOSE_W;
                if (s_tabs[i].w < TAB_MIN_W)
                    s_tabs[i].w = TAB_MIN_W;
            }
            if (hdc != NULL) {
                if (old != NULL)
                    SelectObject(hdc, old);
                ReleaseDC(s_hwnd, hdc);
            }
            s_n = n;
            s_cur = 0;
        }
    }
    InvalidateRect(s_hwnd, NULL, TRUE);
}

int tabbar_current(void)
{
    return s_cur;
}

void tabbar_set_current(int idx)
{
    if (s_hwnd == NULL || s_n < 1)
        return;
    if (idx < 0)
        idx = 0;
    if (idx > s_n - 1)
        idx = s_n - 1;
    if (idx != s_cur) {
        s_cur = idx;
        InvalidateRect(s_hwnd, NULL, FALSE);
    }
}
