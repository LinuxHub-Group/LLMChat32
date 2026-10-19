/*
 * mainwin.c - main window skeleton: class, menu, tab strip, status strip,
 * command dispatch shell.
 *
 * The client area hosts the tab strip ("LlmTabBar" child, see tabbar.h)
 * in its top ~28px; the chat view lands in the remaining space in a later
 * task. The menu is assembled programmatically from LS() strings - there
 * is no .rc menu. The status bar is a self-drawn strip at the bottom of
 * the client area (FillRect + ui_drawtext_w), not a comctl32 control.
 *
 * DEMO_TABS: with no session-loading layer yet (Tasks 17/20), a first
 * run gets 3 fake sessions so the tab strip is visible and clickable.
 */
#include "mainwin.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../core/config.h"
#include "../core/session.h"
#include "commands.h"
#include "strings.h"
#include "tabbar.h"
#include "ui.h"

#define MAIN_CLASS      "LLMChat32Main"
#define STATUS_STRIP_H  22
#define STATUS_PAD_X    6

/* Demo tabs until real session loading lands (Tasks 17/20). Set to 0 to
 * disable; the fake sessions and this define are removed together. */
#define DEMO_TABS 1
#define DEMO_TABS_N 3

HWND g_hwnd_main = NULL;

/* status strip content; later tasks switch this between
 * S_STATUS_READY / S_STATUS_SENDING / S_STATUS_ABORTED. When
 * s_status_text is non-empty it overrides the LS() string. */
static StrID s_status = S_STATUS_READY;
static char s_status_text[128];   /* UTF-8 override */

static HWND s_tabbar = NULL;

#if DEMO_TABS
static Session *s_demo[DEMO_TABS_N];

static void demo_tabs_init(void)
{
    Session *list[DEMO_TABS_N];
    int i;

    for (i = 0; i < DEMO_TABS_N; i++) {
        char id[24];

        sprintf(id, "demo-%d", i);
        s_demo[i] = session_new(id);
        if (s_demo[i] == NULL)
            return;
        sprintf(s_demo[i]->title, "%s %d", LS(S_TAB_DEMO), i + 1);
        list[i] = s_demo[i];
    }
    tabbar_set_sessions(list, DEMO_TABS_N);
}

static void demo_tabs_free(void)
{
    int i;

    for (i = 0; i < DEMO_TABS_N; i++) {
        if (s_demo[i] != NULL) {
            session_free(s_demo[i]);
            s_demo[i] = NULL;
        }
    }
}
#endif /* DEMO_TABS */

static void status_set_text(const char *utf8)
{
    strncpy(s_status_text, utf8, sizeof s_status_text - 1);
    s_status_text[sizeof s_status_text - 1] = '\0';
}

/* redraw just the status strip */
static void status_invalidate(HWND hwnd)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - STATUS_STRIP_H;
    InvalidateRect(hwnd, &rc, FALSE);
}

static void layout_tabbar(HWND hwnd)
{
    RECT rc;

    if (s_tabbar == NULL)
        return;
    GetClientRect(hwnd, &rc);
    MoveWindow(s_tabbar, 0, 0, rc.right, TABBAR_HEIGHT, TRUE);
}

/* ------------------------------------------------------------------ */
/* menu                                                                */

static HMENU build_menu(void)
{
    HMENU bar;
    HMENU m;

    bar = CreateMenu();

    m = CreatePopupMenu();
    menu_append_str(m, MF_STRING, IDM_NEW_CHAT, S_MENU_NEW_CHAT);
    menu_append_str(m, MF_STRING, IDM_CLOSE_CHAT, S_MENU_CLOSE_CHAT);
    menu_append_sep(m);
    menu_append_str(m, MF_STRING, IDM_EXIT, S_MENU_EXIT);
    menu_append_str(bar, MF_STRING | MF_POPUP, (UINT)m, S_MENU_FILE);

    m = CreatePopupMenu();
    menu_append_str(m, MF_STRING, IDM_RECALL, S_MENU_RECALL);
    menu_append_str(m, MF_STRING, IDM_CLEAR, S_MENU_CLEAR);
    menu_append_str(bar, MF_STRING | MF_POPUP, (UINT)m, S_MENU_EDIT);

    m = CreatePopupMenu();
    menu_append_str(m, MF_STRING, IDM_MODEL_CFG, S_MENU_MODEL_CFG);
    menu_append_str(bar, MF_STRING | MF_POPUP, (UINT)m, S_MENU_OPTIONS);

    m = CreatePopupMenu();
    menu_append_str(m, MF_STRING, IDM_ABOUT, S_MENU_ABOUT);
    menu_append_str(bar, MF_STRING | MF_POPUP, (UINT)m, S_MENU_HELP);

    return bar;
}

/* ------------------------------------------------------------------ */
/* painting                                                            */

static void paint_status_strip(HDC hdc, const RECT *client)
{
    RECT rc;
    HPEN pen;
    HGDIOBJ old;
    COLORREF prev;

    rc = *client;
    rc.top = rc.bottom - STATUS_STRIP_H;

    FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

    /* sunken top edge */
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
    old = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.top, NULL);
    LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, old);
    DeleteObject(pen);

    rc.left += STATUS_PAD_X;
    rc.right -= STATUS_PAD_X;
    prev = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    SetBkMode(hdc, TRANSPARENT);
    ui_drawtext_w(hdc, s_status_text[0] ? s_status_text : LS(s_status),
                  &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, prev);
}

/* ------------------------------------------------------------------ */
/* window proc                                                         */

static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp)
{
    PAINTSTRUCT ps;
    RECT rc;

    switch (msg) {
    case WM_APP_TAB_CHANGED:
        /* log to the status strip; real switching lands in Task 17/20 */
        if (lp >= 0 && lp < DEMO_TABS_N
#if DEMO_TABS
            && s_demo[lp] != NULL
#endif
            ) {
#if DEMO_TABS
            status_set_text(s_demo[lp]->title);
#else
            status_set_text("tab changed");
#endif
        } else {
            status_set_text("tab: ?");
        }
        status_invalidate(hwnd);
        return 0;

    case WM_APP_TAB_CLOSE:
        /* real close/delete lands in Task 17/20 */
        {
            char buf[32];

            sprintf(buf, "close: %d", (int)lp);
            status_set_text(buf);
        }
        status_invalidate(hwnd);
        return 0;

    case WM_APP_TAB_NEW:
        /* real new-session lands in Task 17/20 */
        status_set_text(LS(S_MENU_NEW_CHAT));
        status_invalidate(hwnd);
        return 0;

    case WM_SIZE:
        layout_tabbar(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_NEW_CHAT:
            /* later: start a fresh session */
            break;
        case IDM_CLOSE_CHAT:
            /* later: close current session */
            break;
        case IDM_RECALL:
            /* later: app_recall() */
            break;
        case IDM_CLEAR:
            /* later: clear conversation view */
            break;
        case IDM_MODEL_CFG:
            /* later: model settings dialog */
            break;
        case IDM_ABOUT:
            ui_msg_a(hwnd, LS(S_ABOUT_TEXT), LS(S_MENU_HELP), MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        paint_status_strip(ps.hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_DESTROY:
#if DEMO_TABS
        demo_tabs_free();
#endif
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* init                                                                */

HWND ui_mainwin_init(HINSTANCE inst, int ncmdshow, Config *cfg)
{
    WNDCLASSA wc;
    HWND hwnd;
    int w, h;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = main_wndproc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(inst, "APPICON");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_CLASS;
    if (!RegisterClassA(&wc))
        return NULL;

    w = (cfg != NULL && cfg->g.window_width > 0) ? cfg->g.window_width : 640;
    h = (cfg != NULL && cfg->g.window_height > 0) ? cfg->g.window_height : 480;

    hwnd = CreateWindowExA(0, MAIN_CLASS, "LLMChat32",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                           NULL, build_menu(), inst, NULL);
    if (hwnd == NULL)
        return NULL;

    g_hwnd_main = hwnd;
    ShowWindow(hwnd, ncmdshow);
    UpdateWindow(hwnd);

    s_tabbar = tabbar_create(hwnd);
    layout_tabbar(hwnd);
#if DEMO_TABS
    demo_tabs_init();
#endif
    return hwnd;
}
