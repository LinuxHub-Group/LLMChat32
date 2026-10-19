/*
 * dlg_view.c - modal view dialogs (see dlg_view.h).
 *
 * Both dialogs are plain popup windows (no DLGTEMPLATE): a registered
 * class per dialog, standard child controls (edit / static / buttons),
 * the owner disabled while a private modal loop runs. IsDialogMessage
 * in the loop gives ESC -> IDCANCEL and proper key routing into the
 * children; TAB navigation is not implemented (DefWindowProc ignores
 * WM_NEXTDLGCTL) - three controls, mouse + ESC + Enter suffice.
 *
 * Encoding discipline: every string handed to an A-API goes through the
 * ui_* outlets (ui_set_a/ui_acp_a). The one exception in shape - not in
 * rule - is the long think text: it is converted whole with
 * utf8_to_utf16 and set via SetWindowTextW through the ui_set_ww
 * outlet, exactly the outlet pattern ui.h documents for long texts.
 *
 * The image dialog paints the fetched/decoded image itself: the RGBA
 * buffer is alpha-premultiplied over white into a 32-bit bottom-up DIB
 * (AlphaBlend/MSIMG32 is not Win98-safe, so no real alpha blit), and
 * WM_PAINT StretchBlt's it aspect-fitted into the area above the bottom
 * bar. The raw fetched bytes are kept alive until the dialog closes so
 * 另存为… writes byte-identical content.
 */
#include "dlg_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../core/image.h"
#include "../core/utf.h"
#include "draw.h"
#include "strings.h"
#include "ui.h"

/* ------------------------------------------------------------------ */
/* shared helpers                                                      */

#define DLG_INST GetModuleHandle(NULL)

/* Register a dialog window class once per process. */
static int dlg_register(const char *cls, WNDPROC proc)
{
    WNDCLASSA wc;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = DLG_INST;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    return RegisterClassA(&wc) != 0;
}

/* Private modal loop. *ended flips to IDOK/IDCANCEL in the WndProc's
 * close paths (button, ESC via IsDialogMessage, WM_CLOSE); the loop
 * exits right after. WM_QUIT is re-posted for the outer loop. */
static void dlg_modal_run(HWND dlg, int *ended)
{
    MSG msg;

    while (!*ended && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessage(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

/* Outer-size estimate for a desired client size (fixed frame + caption). */
static int dlg_frame_w(void)
{
    return 2 * GetSystemMetrics(SM_CXFIXEDFRAME);
}

static int dlg_frame_h(void)
{
    return 2 * GetSystemMetrics(SM_CYFIXEDFRAME)
        + GetSystemMetrics(SM_CYCAPTION);
}

/* Centered top-left for an outer w x h window, on the owner when given. */
static void dlg_center(HWND owner, int ow, int oh, int *x, int *y)
{
    RECT orc;
    int bx, by, bw, bh, sw, sh;

    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    bx = 0;
    by = 0;
    bw = sw;
    bh = sh;
    if (owner != NULL && GetWindowRect(owner, &orc)) {
        bx = orc.left;
        by = orc.top;
        bw = orc.right - orc.left;
        bh = orc.bottom - orc.top;
    }
    *x = bx + (bw - ow) / 2;
    *y = by + (bh - oh) / 2;
    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
    if (*x + ow > sw)
        *x = sw - ow > 0 ? sw - ow : 0;
    if (*y + oh > sh)
        *y = sh - oh > 0 ? sh - oh : 0;
}

/* Copy the basename of url (after the last '/', before '?'/'#') into
 * out (cap bytes). Empty/missing name falls back to "image". */
static void url_basename(const char *url, char *out, int cap)
{
    const char *end = url;
    const char *p;
    int n;

    while (*end != '\0' && *end != '?' && *end != '#')
        end++;
    p = end;
    while (p > url && p[-1] != '/' && p[-1] != '\\')
        p--;
    n = (int)(end - p);
    if (n < 1) {
        if (cap >= 6)
            strcpy(out, "image");
        else if (cap > 0)
            out[0] = '\0';
        return;
    }
    if (n > cap - 1)
        n = cap - 1;
    memcpy(out, p, (size_t)n);
    out[n] = '\0';
}

/* Append utf8 converted through the CP_ACP outlet plus a NUL; returns
 * bytes written (>= 1, the NUL). dst is always NUL-terminated. */
static int acp_cat0(char *dst, int cap, const char *utf8)
{
    char tmp[256];
    int n;

    if (cap < 1)
        return 0;
    ui_acp_a(tmp, (int)sizeof tmp, utf8);
    n = (int)strlen(tmp) + 1;
    if (n > cap)
        n = cap;
    memcpy(dst, tmp, (size_t)n);
    dst[n - 1] = '\0';
    return n;
}

/* Hand the children the app font, exactly like inputbar_create does;
 * without WM_SETFONT they keep the stock System font, which renders
 * CJK edit text as codepage garbage. */
static void dlg_set_fonts(HWND edit, HWND btn1, HWND btn2, HWND status)
{
    HFONT f = ui_font();

    if (f == NULL)
        return;
    if (edit != NULL)
        SendMessage(edit, WM_SETFONT, (WPARAM)f, TRUE);
    if (btn1 != NULL)
        SendMessage(btn1, WM_SETFONT, (WPARAM)f, TRUE);
    if (btn2 != NULL)
        SendMessage(btn2, WM_SETFONT, (WPARAM)f, TRUE);
    if (status != NULL)
        SendMessage(status, WM_SETFONT, (WPARAM)f, TRUE);
}

/* ------------------------------------------------------------------ */
/* text dialog                                                         */

#define TEXT_CLASS  "LlmDlgViewText"
#define TEXT_CAP    600          /* max pixel width, 70% screen cap    */
#define TEXT_MAXH   420
#define TEXT_BOTTOM 44           /* OK button row                      */

typedef struct {
    const char *title;           /* borrowed (caller frame)            */
    const char *text;            /* borrowed (caller frame)            */
    int ended;                   /* 0 running, else IDOK/IDCANCEL      */
    HWND edit;
    HWND btn_ok;
} TextDlg;

static void text_layout(TextDlg *d, int w, int h)
{
    if (d->edit == NULL)
        return;
    MoveWindow(d->edit, 8, 8, w - 16, h - 8 - TEXT_BOTTOM, TRUE);
    MoveWindow(d->btn_ok, w - 8 - 64, h - 8 - 24, 64, 24, TRUE);
}

static LRESULT CALLBACK text_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp)
{
    TextDlg *d = (TextDlg *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;

        d = (TextDlg *)cs->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)d);
        ui_set_a(hwnd, d->title);
        d->edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", NULL,
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE
                                  | ES_READONLY | WS_VSCROLL
                                  | ES_AUTOVSCROLL,
                                  0, 0, 0, 0, hwnd, NULL, DLG_INST, NULL);
        d->btn_ok = CreateWindowExA(0, "BUTTON", NULL,
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON
                                    | BS_DEFPUSHBUTTON,
                                    0, 0, 0, 0, hwnd, (HMENU)IDOK,
                                    DLG_INST, NULL);
        dlg_set_fonts(d->edit, d->btn_ok, NULL, NULL);
        ui_set_a(d->btn_ok, LS(S_DLG_OK));
        /* the think text can be far larger than ui_set_a's fixed
         * conversion buffer: convert whole and use the ui_set_ww
         * outlet (same rule as ui_drawtext_ww) */
        if (d->edit != NULL && d->text != NULL) {
            int cap = (int)strlen(d->text) + 1;
            wchar_t *w = (wchar_t *)malloc((size_t)cap
                                           * sizeof(wchar_t));

            if (w != NULL) {
                if (utf8_to_utf16(d->text, w, cap) >= 0)
                    ui_set_ww(d->edit, w);
                free(w);
            }
        }
        return 0;
    }

    case WM_SIZE:
        text_layout(d, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp));
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            d->ended = (int)LOWORD(wp);
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        d->ended = IDCANCEL;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void dlg_view_text(HWND owner, const char *title_utf8, const char *text_utf8)
{
    static int registered = 0;
    TextDlg d;
    HWND hwnd;
    int sw, sh, w, h, x, y;

    if (!registered) {
        if (!dlg_register(TEXT_CLASS, text_wndproc))
            return;
        registered = 1;
    }

    memset(&d, 0, sizeof d);
    d.title = title_utf8 != NULL ? title_utf8 : "";
    d.text = text_utf8 != NULL ? text_utf8 : "";

    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    w = sw * 70 / 100;
    if (w > TEXT_CAP)
        w = TEXT_CAP;
    h = sh * 70 / 100;
    if (h > TEXT_MAXH)
        h = TEXT_MAXH;
    dlg_center(owner, w + dlg_frame_w(), h + dlg_frame_h(), &x, &y);

    hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, TEXT_CLASS, NULL,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           x, y, w + dlg_frame_w(), h + dlg_frame_h(),
                           owner, NULL, DLG_INST, &d);
    if (hwnd == NULL)
        return;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (d.edit != NULL)
        SetFocus(d.edit);
    if (owner != NULL)
        EnableWindow(owner, FALSE);
    dlg_modal_run(hwnd, &d.ended);
    if (owner != NULL) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

/* ------------------------------------------------------------------ */
/* image dialog                                                        */

#define IMG_CLASS   "LlmDlgViewImage"
#define IMG_BOTTOM  48              /* status + button bar              */
#define IMG_W0      480             /* initial size (pre-decode)        */
#define IMG_H0      420

#define IDI_SAVE    100
#define IDI_STATUS  101

typedef struct {
    const char *url;             /* borrowed (caller frame)            */
    const ModelCfg *m;           /* borrowed                           */
    const ProxyCfg *p;           /* borrowed                           */
    int ended;
    HWND status;
    HWND btn_save;
    HWND btn_ok;
    unsigned char *raw;          /* fetched bytes (另存为 writes these) */
    int rawlen;
    unsigned char *rgba;         /* decoded (image_free)               */
    int iw, ih;
    HDC memdc;                   /* DIB section for painting           */
    HBITMAP bmp;
    HGDIOBJ oldbmp;
} ImgDlg;

static void img_layout(ImgDlg *d, int w, int h)
{
    int savew;

    if (d->btn_ok == NULL)
        return;
    savew = 90;
    MoveWindow(d->btn_ok, w - 8 - 64, h - 8 - 26, 64, 26, TRUE);
    MoveWindow(d->btn_save, w - 8 - 64 - 8 - savew, h - 8 - 26, savew,
               26, TRUE);
    {
        int left = 8;
        int right = w - 8 - 64 - 8 - savew - 8;

        if (right - left < 60)
            right = left + 60;
        MoveWindow(d->status, left, h - 8 - 22, right - left, 22, TRUE);
    }
}

/* Alpha-premultiply the RGBA buffer over white into a 32-bit bottom-up
 * DIB (no MSIMG32/AlphaBlend - not Win98-safe). Returns 0 and fills
 * d->bmp/d->memdc on success. */
static int img_build_dib(ImgDlg *d)
{
    BITMAPINFO bi;
    unsigned char *bits;
    int stride, x, y;

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = d->iw;
    bi.bmiHeader.biHeight = d->ih;   /* positive: bottom-up */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    d->bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits,
                              NULL, 0);
    if (d->bmp == NULL || bits == NULL) {
        if (d->bmp != NULL) {
            DeleteObject(d->bmp);
            d->bmp = NULL;
        }
        return -1;
    }
    stride = d->iw * 4;
    for (y = 0; y < d->ih; y++) {
        unsigned char *dst = bits + (size_t)(d->ih - 1 - y)
            * (size_t)stride;
        const unsigned char *src = d->rgba + (size_t)y
            * (size_t)d->iw * 4u;

        for (x = 0; x < d->iw; x++) {
            int a = src[x * 4 + 3];

            dst[x * 4 + 0] = (unsigned char)((src[x * 4 + 2] * a
                + 255 * (255 - a) + 127) / 255);     /* B */
            dst[x * 4 + 1] = (unsigned char)((src[x * 4 + 1] * a
                + 255 * (255 - a) + 127) / 255);     /* G */
            dst[x * 4 + 2] = (unsigned char)((src[x * 4 + 0] * a
                + 255 * (255 - a) + 127) / 255);     /* R */
            dst[x * 4 + 3] = 0;
        }
    }
    d->memdc = CreateCompatibleDC(NULL);
    if (d->memdc == NULL) {
        DeleteObject(d->bmp);
        d->bmp = NULL;
        return -1;
    }
    d->oldbmp = SelectObject(d->memdc, d->bmp);
    return 0;
}

static void img_free_bitmap(ImgDlg *d)
{
    if (d->memdc != NULL) {
        if (d->oldbmp != NULL)
            SelectObject(d->memdc, d->oldbmp);
        DeleteDC(d->memdc);
        d->memdc = NULL;
        d->oldbmp = NULL;
    }
    if (d->bmp != NULL) {
        DeleteObject(d->bmp);
        d->bmp = NULL;
    }
}

/* WM_PAINT: white image area above the bottom bar, bitmap aspect-fitted. */
static void img_paint(HWND hwnd, ImgDlg *d, HDC hdc)
{
    RECT rc;
    int availw, availh, dw, dh;

    GetClientRect(hwnd, &rc);
    rc.bottom -= IMG_BOTTOM;
    if (rc.bottom <= rc.top)
        return;
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
    if (d->bmp == NULL || d->memdc == NULL || d->iw <= 0 || d->ih <= 0)
        return;
    availw = rc.right - rc.left - 16;
    availh = rc.bottom - rc.top - 16;
    if (availw < 8)
        availw = 8;
    if (availh < 8)
        availh = 8;
    dw = availw;
    dh = d->ih * dw / d->iw;
    if (dh > availh) {
        dh = availh;
        dw = d->iw * dh / d->ih;
    }
    if (dw < 1)
        dw = 1;
    if (dh < 1)
        dh = 1;
    {
        int dx = rc.left + (rc.right - rc.left - dw) / 2;
        int dy = rc.top + (rc.bottom - rc.top - dh) / 2;

        StretchBlt(hdc, dx, dy, dw, dh, d->memdc, 0, 0, d->iw, d->ih,
                   SRCCOPY);
    }
}

static int write_bytes(const char *path, const void *data, int len)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return -1;
    if (len > 0
        && fwrite(data, 1, (size_t)len, fp) != (size_t)len) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

static void img_save_as(ImgDlg *d, HWND hwnd)
{
    OPENFILENAMEA ofn;
    char file[600];
    char base[300];
    char filter[256];
    char title[256];
    int pos;

    memset(&ofn, 0, sizeof ofn);
    url_basename(d->url, base, (int)sizeof base);
    /* the ANSI common dialog wants ACP bytes: percent-encoded URLs
     * pass through unchanged, raw UTF-8 names get converted */
    ui_acp_a(file, (int)sizeof file, base);
    /* GetSaveFileNameA wants a double-NUL-terminated CP_ACP filter
     * string; each piece goes through the ui_acp_a outlet */
    pos = 0;
    pos += acp_cat0(filter + pos, (int)sizeof filter - pos,
                    LS(S_IMG_FILTER));
    pos += acp_cat0(filter + pos, (int)sizeof filter - pos,
                    "*.png;*.jpg;*.jpeg;*.bmp;*.gif");
    pos += acp_cat0(filter + pos, (int)sizeof filter - pos,
                    LS(S_ALL_FILES));
    pos += acp_cat0(filter + pos, (int)sizeof filter - pos, "*.*");
    if (pos < (int)sizeof filter)
        filter[pos] = '\0';      /* second terminating NUL */
    else
        filter[(int)sizeof filter - 1] = '\0';
    ui_acp_a(title, (int)sizeof title, LS(S_SAVE_AS));

    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)sizeof file;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT
        | OFN_PATHMUSTEXIST;
    ofn.nFilterIndex = 1;
    if (!GetSaveFileNameA(&ofn))
        return;                  /* cancelled */
    if (write_bytes(file, d->raw, d->rawlen) != 0)
        ui_msg_a(hwnd, LS(S_SAVE_FAILED), LS(S_IMAGE_TITLE),
                 MB_OK | MB_ICONERROR);
}

static void img_fail(ImgDlg *d, const char *what)
{
    char msg[600];

    strcpy(msg, LS(S_DOWNLOAD_FAILED));
    strncat(msg, what, (int)sizeof msg - (int)strlen(msg) - 1);
    msg[(int)sizeof msg - 1] = '\0';
    ui_set_a(d->status, msg);
    free(d->raw);
    d->raw = NULL;
    d->rawlen = 0;
}

static LRESULT CALLBACK img_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp)
{
    ImgDlg *d = (ImgDlg *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;

        d = (ImgDlg *)cs->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)d);
        ui_set_a(hwnd, LS(S_IMAGE_TITLE));
        d->status = CreateWindowExA(0, "STATIC", NULL,
                                    WS_CHILD | WS_VISIBLE | SS_LEFT
                                    | SS_NOPREFIX,
                                    0, 0, 0, 0, hwnd, (HMENU)IDI_STATUS,
                                    DLG_INST, NULL);
        d->btn_save = CreateWindowExA(0, "BUTTON", NULL,
                                      WS_CHILD | WS_VISIBLE
                                      | WS_DISABLED | BS_PUSHBUTTON,
                                      0, 0, 0, 0, hwnd, (HMENU)IDI_SAVE,
                                      DLG_INST, NULL);
        ui_set_a(d->btn_save, LS(S_SAVE_AS));
        d->btn_ok = CreateWindowExA(0, "BUTTON", NULL,
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON
                                    | BS_DEFPUSHBUTTON,
                                    0, 0, 0, 0, hwnd, (HMENU)IDOK,
                                    DLG_INST, NULL);
        dlg_set_fonts(NULL, d->btn_ok, d->btn_save, d->status);
        ui_set_a(d->btn_ok, LS(S_DLG_OK));
        return 0;
    }

    case WM_SIZE:
        img_layout(d, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp));
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;

        BeginPaint(hwnd, &ps);
        img_paint(hwnd, d, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        /* the status line sits on the btnface bottom bar */
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_COMMAND:
        if (LOWORD(wp) == IDI_SAVE && d->raw != NULL) {
            img_save_as(d, hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            d->ended = (int)LOWORD(wp);
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        d->ended = IDCANCEL;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        img_free_bitmap(d);
        if (d->rgba != NULL) {
            image_free(d->rgba);
            d->rgba = NULL;
        }
        free(d->raw);
        d->raw = NULL;
        d->rawlen = 0;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* After a successful decode, grow/shrink the window so the image area
 * matches the picture aspect, capped at 70% of the screen. */
static void img_fit_window(HWND hwnd, int iw, int ih)
{
    RECT wr, cr;
    int fw, fh, sw, sh, maxw, maxh, areah, w, h, x, y;

    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    fw = (wr.right - wr.left) - cr.right;
    fh = (wr.bottom - wr.top) - cr.bottom;
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    maxw = sw * 70 / 100;
    maxh = sh * 70 / 100;
    areah = maxh - IMG_BOTTOM;
    w = iw;
    h = ih;
    if (w > maxw) {
        h = h * maxw / w;
        w = maxw;
    }
    if (h > areah) {
        w = w * areah / h;
        h = areah;
    }
    if (w < 240)
        w = 240;
    if (h < 60)
        h = 60;
    dlg_center(NULL, w + fw, h + IMG_BOTTOM + fh, &x, &y);
    MoveWindow(hwnd, x, y, w + fw, h + IMG_BOTTOM + fh, TRUE);
}

void dlg_view_image(HWND owner, const char *url_utf8, const ModelCfg *m,
                    const ProxyCfg *p)
{
    static int registered = 0;
    ImgDlg d;
    HWND hwnd;
    char err[512];
    int x, y;

    if (!registered) {
        if (!dlg_register(IMG_CLASS, img_wndproc))
            return;
        registered = 1;
    }

    memset(&d, 0, sizeof d);
    d.url = url_utf8 != NULL ? url_utf8 : "";
    d.m = m;
    d.p = p;

    dlg_center(owner, IMG_W0 + dlg_frame_w(), IMG_H0 + dlg_frame_h(),
               &x, &y);
    hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, IMG_CLASS, NULL,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           x, y, IMG_W0 + dlg_frame_w(),
                           IMG_H0 + dlg_frame_h(), owner, NULL, DLG_INST,
                           &d);
    if (hwnd == NULL)
        return;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (owner != NULL)
        EnableWindow(owner, FALSE);

    /* "Downloading..." must be on screen before the (blocking) fetch */
    ui_set_a(d.status, LS(S_DOWNLOADING));
    RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);

    err[0] = '\0';
    if (image_fetch(d.url, d.m, d.p, (void **)&d.raw, &d.rawlen,
                    err, (int)sizeof err) != 0) {
        img_fail(&d, err[0] ? err : "network error");
    } else if (image_decode_mem(d.raw, d.rawlen, &d.iw, &d.ih,
                                &d.rgba) != 0) {
        img_fail(&d, LS(S_DECODE_FAILED));
    } else if (img_build_dib(&d) != 0) {
        img_fail(&d, LS(S_DECODE_FAILED));
    } else {
        EnableWindow(d.btn_save, TRUE);
        ui_set_a(d.status, "");   /* downloaded + decoded: clear the line */
        img_fit_window(hwnd, d.iw, d.ih);
        SetFocus(d.btn_ok);
    }
    InvalidateRect(hwnd, NULL, TRUE);
    dlg_modal_run(hwnd, &d.ended);
    if (owner != NULL) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}
