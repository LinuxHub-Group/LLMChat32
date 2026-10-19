/*
 * mainwin.c - main window skeleton: class, menu, status strip, command
 * dispatch shell.
 *
 * The client area is empty for now (chat view lands in a later task).
 * The menu is assembled programmatically from LS() strings - there is no
 * .rc menu. The status bar is a self-drawn strip at the bottom of the
 * client area (FillRect + ui_drawtext_w), not a comctl32 control.
 */
#include "mainwin.h"

#include <string.h>
#include <windows.h>

#include "../core/config.h"
#include "commands.h"
#include "strings.h"
#include "ui.h"

#define MAIN_CLASS      "LLMChat32Main"
#define STATUS_STRIP_H  22
#define STATUS_PAD_X    6

HWND g_hwnd_main = NULL;

/* status strip content; later tasks switch this between
 * S_STATUS_READY / S_STATUS_SENDING / S_STATUS_ABORTED */
static StrID s_status = S_STATUS_READY;

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
    ui_drawtext_w(hdc, LS(s_status), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
    return hwnd;
}
