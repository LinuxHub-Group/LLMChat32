/*
 * inputbar.c - the bottom input strip (see inputbar.h).
 *
 * Children: a multiline EDIT (ES_MULTILINE|ES_WANTRETURN|WS_VSCROLL|
 * ES_AUTOVSCROLL) subclassed so Ctrl+Enter forwards an IDIB_SEND command
 * to the parent while a plain Enter keeps its ES_WANTRETURN newline
 * behavior; a standard CBS_DROPDOWNLIST COMBOBOX for the model; a
 * BS_PUSHBUTTON for send/abort. The bar itself is a bare container that
 * only lays the children out on WM_SIZE and forwards their WM_COMMAND
 * notifications upward.
 *
 * The edit is created with CreateWindowExW as a UNICODE control: under
 * ACP 65001 ("Beta: UTF-8") an ANSI edit mangles IME input into '?'
 * before the app ever reads it, and on other ACPs CJK text round-trips
 * lossy through the ANSI code page. A Unicode edit stores UTF-16
 * directly, so IME composition lands intact. The subclass therefore
 * uses the W family too (SetWindowLongW/CallWindowProcW) - an ANSI
 * subclass on a Unicode window would make the system convert every
 * message back through the ACP. The combo/button stay ANSI: they only
 * ever display text the app sets itself through the ui_* outlets.
 *
 * Ctrl+Enter eats BOTH WM_KEYDOWN and WM_CHAR: TranslateMessage in the
 * main loop turns the VK_RETURN keydown into a WM_CHAR '\r' even when
 * the subclass swallows the keydown, so eating only one would still let
 * a newline through. In the W subclass WM_CHAR wParam is UTF-16; the
 * 0x0D test is unchanged.
 *
 * All functions are C89. Strings handed to controls are narrow UTF-8
 * converted through the ui_* outlets; text read back is converted to
 * UTF-8 in inputbar_get_text (see header).
 */
#include "inputbar.h"

#include <string.h>

#include "../core/utf.h"
#include "draw.h"
#include "strings.h"
#include "ui.h"

#define BAR_CLASS   "LlmInputBar"
#define BTN_W       64
#define COMBO_W     150
#define PAD         6

static HWND s_bar = NULL;
static HWND s_edit = NULL;
static HWND s_combo = NULL;
static HWND s_btn = NULL;
static WNDPROC s_edit_old = NULL;

/* ------------------------------------------------------------------ */
/* edit subclass: Ctrl+Enter -> send                                   */

static LRESULT CALLBACK edit_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp)
{
    if ((msg == WM_KEYDOWN || msg == WM_CHAR) && wp == '\r' &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        if (msg == WM_KEYDOWN)
            SendMessage(GetParent(GetParent(hwnd)), WM_COMMAND,
                        MAKEWPARAM(IDIB_SEND, BN_CLICKED),
                        (LPARAM)GetDlgItem(GetParent(hwnd), IDIB_SEND));
        return 0;               /* swallow; no newline, no beep */
    }
    return CallWindowProcW(s_edit_old, hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* bar window proc                                                     */

static void layout_children(HWND hwnd)
{
    RECT rc;
    int w, h, eh;

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    eh = h - 2 * PAD;

    if (s_edit != NULL)
        MoveWindow(s_edit, PAD + COMBO_W + PAD, PAD,
                   w - COMBO_W - BTN_W - 4 * PAD, eh, TRUE);
    if (s_combo != NULL)
        MoveWindow(s_combo, PAD, (h - 24) / 2, COMBO_W, 200, TRUE);
    if (s_btn != NULL)
        MoveWindow(s_btn, w - BTN_W - PAD, PAD, BTN_W, eh, TRUE);
}

static LRESULT CALLBACK bar_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        layout_children(hwnd);
        return 0;
    case WM_COMMAND:
        /* forward child notifications (send click, model change) */
        SendMessage(GetParent(hwnd), msg, wp, lp);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* public API                                                          */

HWND inputbar_create(HWND parent)
{
    WNDCLASSA wc;
    HINSTANCE inst;
    HFONT font;

    if (s_bar != NULL)
        return s_bar;

    inst = (HINSTANCE)GetWindowLongA(parent, GWL_HINSTANCE);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = bar_wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = BAR_CLASS;
    if (!RegisterClassA(&wc))
        return NULL;

    s_bar = CreateWindowExA(0, BAR_CLASS, NULL,
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent, NULL, inst, NULL);
    if (s_bar == NULL)
        return NULL;

    s_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                             WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                             ES_WANTRETURN | WS_VSCROLL | ES_AUTOVSCROLL,
                             0, 0, 0, 0, s_bar, (HMENU)IDIB_EDIT, inst,
                             NULL);
    s_combo = CreateWindowExA(0, "COMBOBOX", NULL,
                              WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                              WS_VSCROLL,
                              0, 0, 0, 0, s_bar, (HMENU)IDIB_MODEL, inst,
                              NULL);
    s_btn = CreateWindowExA(0, "BUTTON", NULL,
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON |
                            BS_CENTER,
                            0, 0, 0, 0, s_bar, (HMENU)IDIB_SEND, inst,
                            NULL);
    if (s_edit == NULL || s_combo == NULL || s_btn == NULL)
        return NULL;

    font = ui_font();
    if (font != NULL) {
        SendMessageW(s_edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(s_combo, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(s_btn, WM_SETFONT, (WPARAM)font, TRUE);
    }

    s_edit_old = (WNDPROC)GetWindowLongW(s_edit, GWL_WNDPROC);
    SetWindowLongW(s_edit, GWL_WNDPROC, (LONG)edit_wndproc);

    ui_set_a(s_btn, LS(S_BTN_SEND));
    layout_children(s_bar);
    return s_bar;
}

void inputbar_set_models(const Config *cfg)
{
    int i;
    char name[128];

    if (s_combo == NULL)
        return;
    SendMessage(s_combo, CB_RESETCONTENT, 0, 0);
    for (i = 0; cfg != NULL && i < cfg->n_models && i < 32; i++) {
        ui_acp_a(name, sizeof name, cfg->m[i].name);
        SendMessage(s_combo, CB_ADDSTRING, 0, (LPARAM)name);
    }
    SendMessage(s_combo, CB_SETCURSEL, 0, 0);
}

int inputbar_selected_model(void)
{
    LRESULT sel;

    if (s_combo == NULL)
        return 0;
    sel = SendMessage(s_combo, CB_GETCURSEL, 0, 0);
    return sel == CB_ERR ? 0 : (int)sel;
}

void inputbar_get_text(char *buf, int cap)
{
    wchar_t w[INPUTBAR_TEXT_MAX];
    int wlen;

    if (buf == NULL || cap < 1)
        return;
    buf[0] = '\0';
    if (s_edit == NULL)
        return;
    /* the edit is a Unicode control: it hands back UTF-16 directly */
    wlen = GetWindowTextW(s_edit, w, (int)(sizeof w / sizeof w[0]));
    if (wlen < 1)
        return;
    if (utf16_to_utf8(w, buf, cap) < 0)
        buf[0] = '\0';
}

void inputbar_clear(void)
{
    if (s_edit != NULL)
        SetWindowTextA(s_edit, "");
}

void inputbar_set_sending(int busy)
{
    if (s_btn != NULL)
        ui_set_a(s_btn, LS(busy ? S_BTN_ABORT : S_BTN_SEND));
}
