/*
 * dlg_config.c - modal model-configuration dialog (see dlg_config.h).
 *
 * Layout (fixed client size, all constants below): a model LISTBOX on
 * the left with 添加/删除/上移/下移 beneath it; on the right a column
 * of labelled field rows - name edit, api dropdown (CBS_DROPDOWNLIST),
 * base_url / api_key / model_id edits, two checkbox rows
 * (use_proxy/stream, reasoning/verify_tls), the reasoning_effort
 * dropdown and four number edits (thinking_budget / max_tokens /
 * max_context_chars with ES_NUMBER, temperature as a plain edit so the
 * -1 sentinel and decimals parse through strtod).
 *
 * Editing model: every control change applies straight into the local
 * Config copy (d->cfg.m[d->sel]); the listbox item text follows the
 * name edit live. Numeric edits re-parse on every change and flag
 * errors instead of committing garbage. 确定 re-checks everything
 * (per-model name/api/max_tokens plus the numeric error flags), then
 * config_save()s the copy; only a successful save is copied back into
 * the caller's Config. A failed save pops ui_msg_a(S_SAVE_FAILED) and
 * keeps the dialog open. Validation problems go to the status static
 * in the dialog, also keeping it open.
 *
 * The dlg_register / dlg_modal_run / dlg_frame_* / dlg_center helpers
 * below duplicate dlg_view.c on purpose (Task 19 ruling: duplicating
 * the ~40 lines beats growing a shared helper for two call sites).
 *
 * Encoding discipline: every string handed to an A-API goes through
 * the ui_* outlets (ui_set_a / ui_acp_a). Text read back from a
 * control is CP_ACP bytes (the OS owns the content), converted to
 * UTF-8 exactly like inputbar_get_text does. Combo boxes take their
 * item strings via ui_acp_a (they accept a string argument, exactly
 * the case ui.h documents).
 */
#include "dlg_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <windows.h>

#include "../core/utf.h"
#include "draw.h"
#include "strings.h"
#include "ui.h"

#define DLG_CLASS   "LlmDlgConfig"
#define DLG_INST    GetModuleHandle(NULL)

/* ------------------------------------------------------------------ */
/* layout constants (client coordinates; the window is fixed-size)     */
/* ------------------------------------------------------------------ */

#define DLG_CW     460              /* client width                     */
#define DLG_CH     372              /* client height                    */

#define LST_X      8                /* model list, left column          */
#define LST_Y      8
#define LST_W      140
#define LST_H      252

#define LB_X       8                /* 2x2 button grid under the list   */
#define LB_W       67
#define LB_H       24
#define LB_GAP     6
#define LB_Y1      (LST_Y + LST_H + 8)
#define LB_Y2      (LB_Y1 + LB_H + 4)

#define R_X        (LST_X + LST_W + 8)      /* right column             */
#define R_RIGHT    (DLG_CW - 8)
#define LBL_W      116
#define CTL_X      (R_X + LBL_W)
#define CTL_W      (R_RIGHT - CTL_X)

#define ROW_H      24
#define ROW_Y(n)   (8 + (n) * ROW_H)
#define CTL_H      22

#define CHK_W      112              /* one checkbox                     */
#define ST_Y       306              /* status line above the buttons    */
#define BTN_Y      330
#define BTN_H      26
#define BTN_W      64

/* child ids (dialog-private; kept clear of IDOK=1/IDCANCEL=2) */
#define IDC_LIST    10
#define IDC_ADD     11
#define IDC_DEL     12
#define IDC_UP      13
#define IDC_DOWN    14
#define IDC_NAME    15
#define IDC_API     16
#define IDC_BASE    17
#define IDC_KEY     18
#define IDC_MID     19
#define IDC_UPROXY  20
#define IDC_STREAM  21
#define IDC_REASON  22
#define IDC_VTLS    23
#define IDC_EFF     24
#define IDC_BUDGET  25
#define IDC_MAXTOK  26
#define IDC_MAXCTX  27
#define IDC_TEMP    28
#define IDC_STATUS  29

#define MAX_MODELS  32              /* Config.m[] capacity              */

/* stored-value <-> dropdown-index maps */
static const char *const API_VAL[3] = { "anthropic", "chat", "responses" };
static const StrID API_STR[3] = { S_API_ANTHROPIC, S_API_CHAT,
                                  S_API_RESPONSES };
static const char *const EFF_VAL[3] = { "low", "medium", "high" };
static const StrID EFF_STR[3] = { S_EFF_LOW, S_EFF_MEDIUM, S_EFF_HIGH };

/* ------------------------------------------------------------------ */
/* shared window helpers (duplicated from dlg_view.c, see header)      */
/* ------------------------------------------------------------------ */

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
 * close paths; the loop exits right after. WM_QUIT is re-posted for
 * the outer loop. */
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

/* ------------------------------------------------------------------ */
/* dialog state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    Config *cfg_out;             /* caller's config (copy-back target)  */
    Config cfg;                  /* local working copy                  */
    int ended;                   /* 0 running, else IDOK/IDCANCEL       */
    int sel;                     /* listbox selection, -1 = none        */
    int loading;                 /* guard vs LBN_SELCHANGE re-entry     */
    int err_budget, err_maxtok, err_maxctx, err_temp;  /* parse flags  */
    HWND list;
    HWND btn_add, btn_del, btn_up, btn_down;
    HWND ed_name, cb_api, ed_base, ed_key, ed_mid;
    HWND ck_uproxy, ck_stream, ck_reason, ck_vtls;
    HWND cb_eff, ed_budget, ed_maxtok, ed_maxctx, ed_temp;
    HWND st_status, btn_ok, btn_cancel;
} CfgDlg;

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

/* Create one child control. */
static HWND mkctl(const char *cls, DWORD style, int x, int y, int w, int h,
                  int id, HWND parent)
{
    return CreateWindowExA(0, cls, NULL, style, x, y, w, h, parent,
                           (HMENU)id, DLG_INST, NULL);
}

/* Hand every child the app font (WM_SETFONT), exactly like
 * inputbar_create / dlg_view do - without it CJK text renders as
 * codepage garbage in the stock System font. */
static void dlg_set_fonts(CfgDlg *d)
{
    static const int members[] = {
        offsetof(CfgDlg, list),
        offsetof(CfgDlg, ed_name), offsetof(CfgDlg, cb_api),
        offsetof(CfgDlg, ed_base), offsetof(CfgDlg, ed_key),
        offsetof(CfgDlg, ed_mid),
        offsetof(CfgDlg, ck_uproxy), offsetof(CfgDlg, ck_stream),
        offsetof(CfgDlg, ck_reason), offsetof(CfgDlg, ck_vtls),
        offsetof(CfgDlg, cb_eff), offsetof(CfgDlg, ed_budget),
        offsetof(CfgDlg, ed_maxtok), offsetof(CfgDlg, ed_maxctx),
        offsetof(CfgDlg, ed_temp),
        offsetof(CfgDlg, st_status), offsetof(CfgDlg, btn_ok),
        offsetof(CfgDlg, btn_cancel)
    };
    HFONT f = ui_font();
    size_t i;

    if (f == NULL)
        return;
    for (i = 0; i < sizeof members / sizeof members[0]; i++) {
        HWND h = *(HWND *)((char *)d + members[i]);

        if (h != NULL)
            SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE);
    }
}

/* GetWindowTextA yields CP_ACP bytes (the OS owns the control content);
 * convert back to narrow UTF-8, same recipe as inputbar_get_text. */
static void get_utf8(HWND ctl, char *buf, int cap)
{
    char acp[600];
    wchar_t w[300];
    int alen, wlen;

    if (buf == NULL || cap < 1)
        return;
    buf[0] = '\0';
    if (ctl == NULL)
        return;
    alen = GetWindowTextA(ctl, acp, (int)sizeof acp);
    if (alen < 1)
        return;
    wlen = MultiByteToWideChar(CP_ACP, 0, acp, alen, w,
                               (int)(sizeof w / sizeof w[0]) - 1);
    if (wlen < 1)
        return;
    w[wlen] = L'\0';
    if (utf16_to_utf8(w, buf, cap) < 0)
        buf[0] = '\0';
}

/* Strict long parse: rejects empty, whitespace-padded, trailing-garbage
 * and (via the first-char check) anything strtol would loosely accept.
 * Returns 0 on success. */
static int parse_long(const char *s, long *out)
{
    char *end;
    long v;

    if (s == NULL || s[0] == '\0')
        return -1;
    if ((s[0] < '0' || s[0] > '9') && s[0] != '-')
        return -1;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return -1;
    *out = v;
    return 0;
}

/* Strict double parse (temperature): full-string, first char must look
 * numeric so strtod never swallows "inf"/"nan"/hex floats. Negative
 * values are legal (-1 is the "do not send" sentinel). */
static int parse_double(const char *s, double *out)
{
    char *end;
    double v;

    if (s == NULL || s[0] == '\0')
        return -1;
    if ((s[0] < '0' || s[0] > '9') && s[0] != '-' && s[0] != '+'
        && s[0] != '.')
        return -1;
    v = strtod(s, &end);
    if (end == s || *end != '\0')
        return -1;
    *out = v;
    return 0;
}

/* Show a status line when a numeric edit currently holds garbage, else
 * clear it. */
static void status_refresh(CfgDlg *d)
{
    if (d->err_budget || d->err_maxtok || d->err_maxctx || d->err_temp)
        ui_set_a(d->st_status, LS(S_CFG_ERR_NUM));
    else
        ui_set_a(d->st_status, "");
}

/* ------------------------------------------------------------------ */
/* listbox <-> model mapping                                           */
/* ------------------------------------------------------------------ */

/* (Re)fill the listbox from the local copy and select `sel`. */
static void lb_fill(CfgDlg *d, int sel)
{
    int i;

    d->loading = 1;
    SendMessage(d->list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < d->cfg.n_models; i++) {
        char name[128];

        ui_acp_a(name, (int)sizeof name, d->cfg.m[i].name);
        SendMessage(d->list, LB_ADDSTRING, 0, (LPARAM)name);
    }
    if (sel >= 0 && sel < d->cfg.n_models)
        SendMessage(d->list, LB_SETCURSEL, sel, 0);
    else
        SendMessage(d->list, LB_SETCURSEL, (WPARAM)-1, 0);
    d->loading = 0;
    d->sel = (sel >= 0 && sel < d->cfg.n_models) ? sel : -1;
}

/* Refresh one item's text in place (LB has no LB_SETTEXT: delete +
 * insert at the slot, keeping the selection). loading suppresses the
 * LBN_SELCHANGE notifications the listbox fires mid-way. */
static void lb_refresh_item(CfgDlg *d, int idx)
{
    char name[128];

    if (idx < 0 || idx >= d->cfg.n_models)
        return;
    ui_acp_a(name, (int)sizeof name, d->cfg.m[idx].name);
    d->loading = 1;
    SendMessage(d->list, LB_DELETESTRING, idx, 0);
    SendMessage(d->list, LB_INSERTSTRING, idx, (LPARAM)name);
    SendMessage(d->list, LB_SETCURSEL, idx, 0);
    d->loading = 0;
}

/* ------------------------------------------------------------------ */
/* field <-> control transfer                                          */
/* ------------------------------------------------------------------ */

static int map_to_index(const char *stored, const char *const *vals, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(stored, vals[i]) == 0)
            return i;
    return -1;                   /* unrecognized/empty: no selection    */
}

/* Load model idx into the field controls; idx < 0 clears them. */
static void load_fields(CfgDlg *d, int idx)
{
    ModelCfg *m;
    char num[32];

    d->sel = (idx >= 0 && idx < d->cfg.n_models) ? idx : -1;
    if (d->sel < 0) {
        ui_set_a(d->ed_name, "");
        SendMessage(d->cb_api, CB_SETCURSEL, (WPARAM)-1, 0);
        ui_set_a(d->ed_base, "");
        ui_set_a(d->ed_key, "");
        ui_set_a(d->ed_mid, "");
        SendMessage(d->ck_uproxy, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(d->ck_stream, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(d->ck_reason, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(d->ck_vtls, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(d->cb_eff, CB_SETCURSEL, (WPARAM)-1, 0);
        ui_set_a(d->ed_budget, "");
        ui_set_a(d->ed_maxtok, "");
        ui_set_a(d->ed_maxctx, "");
        ui_set_a(d->ed_temp, "");
        d->err_budget = d->err_maxtok = d->err_maxctx = d->err_temp = 0;
        status_refresh(d);
        return;
    }

    d->loading = 1;
    m = &d->cfg.m[d->sel];
    ui_set_a(d->ed_name, m->name);
    SendMessage(d->cb_api, CB_SETCURSEL,
                (WPARAM)map_to_index(m->api, API_VAL, 3), 0);
    ui_set_a(d->ed_base, m->base_url);
    ui_set_a(d->ed_key, m->api_key);
    ui_set_a(d->ed_mid, m->model_id);
    SendMessage(d->ck_uproxy, BM_SETCHECK,
                m->use_proxy ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(d->ck_stream, BM_SETCHECK,
                m->stream ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(d->ck_reason, BM_SETCHECK,
                m->reasoning ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(d->ck_vtls, BM_SETCHECK,
                m->verify_tls ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(d->cb_eff, CB_SETCURSEL,
                (WPARAM)map_to_index(m->reasoning_effort, EFF_VAL, 3), 0);
    sprintf(num, "%ld", m->thinking_budget);
    ui_set_a(d->ed_budget, num);
    sprintf(num, "%ld", m->max_tokens);
    ui_set_a(d->ed_maxtok, num);
    sprintf(num, "%ld", m->max_context_chars);
    ui_set_a(d->ed_maxctx, num);
    sprintf(num, "%g", m->temperature);
    ui_set_a(d->ed_temp, num);
    d->loading = 0;
    /* the shown numbers are struct values: always parse clean */
    d->err_budget = d->err_maxtok = d->err_maxctx = d->err_temp = 0;
    status_refresh(d);
}

/* ------------------------------------------------------------------ */
/* control change handlers (immediate-apply into the local copy)       */
/* ------------------------------------------------------------------ */

static void on_name_changed(CfgDlg *d)
{
    if (d->loading || d->sel < 0)
        return;
    get_utf8(d->ed_name, d->cfg.m[d->sel].name,
             (int)sizeof d->cfg.m[d->sel].name);
    lb_refresh_item(d, d->sel);
}

/* Immediate-apply for the plain text fields (no per-keystroke side
 * effects beyond the struct write). */
static void on_text_changed(CfgDlg *d, int id)
{
    ModelCfg *m;

    if (d->loading || d->sel < 0)
        return;
    m = &d->cfg.m[d->sel];
    if (id == IDC_BASE)
        get_utf8(d->ed_base, m->base_url, (int)sizeof m->base_url);
    else if (id == IDC_KEY)
        get_utf8(d->ed_key, m->api_key, (int)sizeof m->api_key);
    else
        get_utf8(d->ed_mid, m->model_id, (int)sizeof m->model_id);
}

/* Re-parse one numeric edit; commit into the model on success, raise
 * the dialog error flag on failure. */
static void on_number_changed(CfgDlg *d, HWND ctl, int which)
{
    char buf[64];
    long lv;
    double dv;

    if (d->loading || d->sel < 0)
        return;
    get_utf8(ctl, buf, (int)sizeof buf);
    switch (which) {
    case IDC_BUDGET:
        if (parse_long(buf, &lv) == 0) {
            d->cfg.m[d->sel].thinking_budget = lv;
            d->err_budget = 0;
        } else {
            d->err_budget = 1;
        }
        break;
    case IDC_MAXTOK:
        if (parse_long(buf, &lv) == 0) {
            d->cfg.m[d->sel].max_tokens = lv;
            d->err_maxtok = 0;
        } else {
            d->err_maxtok = 1;
        }
        break;
    case IDC_MAXCTX:
        if (parse_long(buf, &lv) == 0) {
            d->cfg.m[d->sel].max_context_chars = lv;
            d->err_maxctx = 0;
        } else {
            d->err_maxctx = 1;
        }
        break;
    default:                    /* IDC_TEMP */
        if (parse_double(buf, &dv) == 0) {
            d->cfg.m[d->sel].temperature = dv;
            d->err_temp = 0;
        } else {
            d->err_temp = 1;
        }
        break;
    }
    status_refresh(d);
}

static void on_check_changed(CfgDlg *d, int id)
{
    HWND ctl;
    int on;

    if (d->loading || d->sel < 0)
        return;
    switch (id) {
    case IDC_UPROXY: ctl = d->ck_uproxy; break;
    case IDC_STREAM: ctl = d->ck_stream; break;
    case IDC_REASON: ctl = d->ck_reason; break;
    default:         ctl = d->ck_vtls;  break;
    }
    on = SendMessage(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    switch (id) {
    case IDC_UPROXY: d->cfg.m[d->sel].use_proxy = on;  break;
    case IDC_STREAM: d->cfg.m[d->sel].stream = on;     break;
    case IDC_REASON: d->cfg.m[d->sel].reasoning = on;  break;
    default:         d->cfg.m[d->sel].verify_tls = on; break;
    }
}

static void on_combo_changed(CfgDlg *d, int id)
{
    LRESULT idx;
    HWND ctl;
    const char *val;

    if (d->loading || d->sel < 0)
        return;
    if (id == IDC_API) {
        ctl = d->cb_api;
        idx = SendMessage(ctl, CB_GETCURSEL, 0, 0);
        if (idx == CB_ERR || idx < 0 || idx > 2)
            return;
        val = API_VAL[idx];
        strcpy(d->cfg.m[d->sel].api, val);
    } else {
        ctl = d->cb_eff;
        idx = SendMessage(ctl, CB_GETCURSEL, 0, 0);
        if (idx == CB_ERR || idx < 0 || idx > 2)
            return;
        val = EFF_VAL[idx];
        strcpy(d->cfg.m[d->sel].reasoning_effort, val);
    }
}

/* ------------------------------------------------------------------ */
/* list operations                                                     */
/* ------------------------------------------------------------------ */

static void model_add(CfgDlg *d)
{
    ModelCfg *m;

    if (d->cfg.n_models >= MAX_MODELS) {
        ui_set_a(d->st_status, LS(S_CFG_ERR_FULL));
        return;
    }
    m = &d->cfg.m[d->cfg.n_models];
    memset(m, 0, sizeof *m);
    strcpy(m->name, LS(S_CFG_NEWMODEL));
    strcpy(m->api, "chat");
    m->stream = 1;
    m->max_tokens = 4096;
    m->temperature = -1.0;      /* sentinel: do not send */
    d->cfg.n_models++;
    lb_fill(d, d->cfg.n_models - 1);
    load_fields(d, d->sel);
}

static void model_delete(CfgDlg *d)
{
    int sel = d->sel;

    if (sel < 0 || sel >= d->cfg.n_models)
        return;
    memmove(&d->cfg.m[sel], &d->cfg.m[sel + 1],
            (size_t)(d->cfg.n_models - sel - 1) * sizeof(ModelCfg));
    d->cfg.n_models--;
    if (d->cfg.n_models == 0) {
        lb_fill(d, -1);
        load_fields(d, -1);
        return;
    }
    lb_fill(d, sel < d->cfg.n_models ? sel : d->cfg.n_models - 1);
    load_fields(d, d->sel);
}

static void model_move(CfgDlg *d, int delta)
{
    int sel = d->sel;
    int to = sel + delta;
    ModelCfg tmp;

    if (sel < 0 || to < 0 || sel >= d->cfg.n_models
        || to >= d->cfg.n_models)
        return;
    tmp = d->cfg.m[sel];
    d->cfg.m[sel] = d->cfg.m[to];
    d->cfg.m[to] = tmp;
    lb_refresh_item(d, sel);
    lb_refresh_item(d, to);
    d->loading = 1;
    SendMessage(d->list, LB_SETCURSEL, to, 0);
    d->loading = 0;
    d->sel = to;
    /* the moved model keeps its values: nothing to reload */
}

/* ------------------------------------------------------------------ */
/* OK / validation                                                     */
/* ------------------------------------------------------------------ */

/* NULL when everything validates, else the status text to show. */
static const char *validate_all(CfgDlg *d)
{
    int i;

    if (d->err_budget || d->err_maxtok || d->err_maxctx || d->err_temp)
        return LS(S_CFG_ERR_NUM);
    for (i = 0; i < d->cfg.n_models; i++) {
        const ModelCfg *m = &d->cfg.m[i];

        if (m->name[0] == '\0')
            return LS(S_CFG_ERR_NAME);
        if (map_to_index(m->api, API_VAL, 3) < 0)
            return LS(S_CFG_ERR_API);
        if (m->max_tokens <= 0)
            return LS(S_CFG_ERR_MAXTOK);
    }
    return NULL;
}

static void do_ok(CfgDlg *d, HWND hwnd)
{
    const char *msg = validate_all(d);
    char err[160];
    char full[300];

    if (msg != NULL) {
        ui_set_a(d->st_status, msg);
        return;
    }
    if (config_save(&d->cfg, "config.toml", err, (int)sizeof err) != 0) {
        strcpy(full, LS(S_SAVE_FAILED));
        strncat(full, err, (int)sizeof full - (int)strlen(full) - 1);
        full[(int)sizeof full - 1] = '\0';
        ui_msg_a(hwnd, full, LS(S_CFG_TITLE), MB_OK | MB_ICONERROR);
        return;                 /* stay open: the user may fix or cancel */
    }
    *d->cfg_out = d->cfg;
    d->ended = IDOK;
    DestroyWindow(hwnd);
}

/* ------------------------------------------------------------------ */
/* window proc                                                         */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK cfg_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp)
{
    CfgDlg *d = (CfgDlg *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        DWORD ed = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
        DWORD num = ed | ES_NUMBER;
        int i;

        d = (CfgDlg *)cs->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)d);
        ui_set_a(hwnd, LS(S_CFG_TITLE));

        d->list = mkctl("LISTBOX",
                        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                        LST_X, LST_Y, LST_W, LST_H, IDC_LIST, hwnd);
        d->btn_add = mkctl("BUTTON",
                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           LB_X, LB_Y1, LB_W, LB_H, IDC_ADD, hwnd);
        ui_set_a(d->btn_add, LS(S_CFG_ADD));
        d->btn_del = mkctl("BUTTON",
                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           LB_X + LB_W + LB_GAP, LB_Y1, LB_W, LB_H,
                           IDC_DEL, hwnd);
        ui_set_a(d->btn_del, LS(S_CFG_DEL));
        d->btn_up = mkctl("BUTTON",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          LB_X, LB_Y2, LB_W, LB_H, IDC_UP, hwnd);
        ui_set_a(d->btn_up, LS(S_CFG_UP));
        d->btn_down = mkctl("BUTTON",
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            LB_X + LB_W + LB_GAP, LB_Y2, LB_W, LB_H,
                            IDC_DOWN, hwnd);
        ui_set_a(d->btn_down, LS(S_CFG_DOWN));

        /* labelled field rows; labels are right-aligned statics */
#define LABEL(sid, n) \
        ui_set_a(mkctl("STATIC", \
                       WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE \
                       | SS_NOPREFIX, \
                       R_X, ROW_Y(n), LBL_W - 6, CTL_H, 100 + (n), hwnd), \
                 LS(sid))
        LABEL(S_CFG_NAME, 0);
        LABEL(S_CFG_API, 1);
        LABEL(S_CFG_BASEURL, 2);
        LABEL(S_CFG_APIKEY, 3);
        LABEL(S_CFG_MODELID, 4);
        LABEL(S_CFG_EFFORT, 7);
        LABEL(S_CFG_BUDGET, 8);
        LABEL(S_CFG_MAXTOKENS, 9);
        LABEL(S_CFG_MAXCTX, 10);
        LABEL(S_CFG_TEMP, 11);
#undef LABEL

        d->ed_name = mkctl("EDIT", ed, CTL_X, ROW_Y(0), CTL_W, CTL_H,
                           IDC_NAME, hwnd);
        d->cb_api = mkctl("COMBOBOX",
                          WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST
                          | WS_VSCROLL,
                          CTL_X, ROW_Y(1), CTL_W, 200, IDC_API, hwnd);
        for (i = 0; i < 3; i++) {
            char item[64];

            ui_acp_a(item, (int)sizeof item, LS(API_STR[i]));
            SendMessage(d->cb_api, CB_ADDSTRING, 0, (LPARAM)item);
        }
        d->ed_base = mkctl("EDIT", ed, CTL_X, ROW_Y(2), CTL_W, CTL_H,
                           IDC_BASE, hwnd);
        d->ed_key = mkctl("EDIT", ed, CTL_X, ROW_Y(3), CTL_W, CTL_H,
                          IDC_KEY, hwnd);
        d->ed_mid = mkctl("EDIT", ed, CTL_X, ROW_Y(4), CTL_W, CTL_H,
                          IDC_MID, hwnd);

        d->ck_uproxy = mkctl("BUTTON",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             R_X, ROW_Y(5), CHK_W, CTL_H,
                             IDC_UPROXY, hwnd);
        ui_set_a(d->ck_uproxy, LS(S_CFG_USEPROXY));
        d->ck_stream = mkctl("BUTTON",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             R_X + CHK_W + 12, ROW_Y(5), CHK_W, CTL_H,
                             IDC_STREAM, hwnd);
        ui_set_a(d->ck_stream, LS(S_CFG_STREAM));
        d->ck_reason = mkctl("BUTTON",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             R_X, ROW_Y(6), CHK_W, CTL_H,
                             IDC_REASON, hwnd);
        ui_set_a(d->ck_reason, LS(S_CFG_REASONING));
        d->ck_vtls = mkctl("BUTTON",
                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           R_X + CHK_W + 12, ROW_Y(6), CHK_W, CTL_H,
                           IDC_VTLS, hwnd);
        ui_set_a(d->ck_vtls, LS(S_CFG_VERIFYTLS));

        d->cb_eff = mkctl("COMBOBOX",
                          WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST
                          | WS_VSCROLL,
                          CTL_X, ROW_Y(7), CTL_W, 200, IDC_EFF, hwnd);
        for (i = 0; i < 3; i++) {
            char item[32];

            ui_acp_a(item, (int)sizeof item, LS(EFF_STR[i]));
            SendMessage(d->cb_eff, CB_ADDSTRING, 0, (LPARAM)item);
        }
        d->ed_budget = mkctl("EDIT", num, CTL_X, ROW_Y(8), CTL_W, CTL_H,
                             IDC_BUDGET, hwnd);
        d->ed_maxtok = mkctl("EDIT", num, CTL_X, ROW_Y(9), CTL_W, CTL_H,
                             IDC_MAXTOK, hwnd);
        d->ed_maxctx = mkctl("EDIT", num, CTL_X, ROW_Y(10), CTL_W, CTL_H,
                             IDC_MAXCTX, hwnd);
        d->ed_temp = mkctl("EDIT", ed, CTL_X, ROW_Y(11), CTL_W, CTL_H,
                           IDC_TEMP, hwnd);

        d->st_status = mkctl("STATIC",
                             WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                             8, ST_Y, DLG_CW - 16, 18, IDC_STATUS, hwnd);
        d->btn_ok = mkctl("BUTTON",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON
                          | BS_DEFPUSHBUTTON,
                          R_RIGHT - BTN_W, BTN_Y, BTN_W, BTN_H, IDOK, hwnd);
        ui_set_a(d->btn_ok, LS(S_CFG_OK));
        d->btn_cancel = mkctl("BUTTON", WS_CHILD | WS_VISIBLE
                              | BS_PUSHBUTTON,
                              R_RIGHT - 2 * BTN_W - 8, BTN_Y, BTN_W, BTN_H,
                              IDCANCEL, hwnd);
        ui_set_a(d->btn_cancel, LS(S_CFG_CANCEL));

        dlg_set_fonts(d);
        lb_fill(d, d->cfg.n_models > 0 ? 0 : -1);
        load_fields(d, d->sel);
        if (d->list != NULL)
            SetFocus(d->list);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        /* the status line sits on the btnface dialog background */
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE && !d->loading) {
                LRESULT sel = SendMessage(d->list, LB_GETCURSEL, 0, 0);

                load_fields(d, sel == LB_ERR ? -1 : (int)sel);
            }
            return 0;
        case IDC_ADD:
            model_add(d);
            return 0;
        case IDC_DEL:
            model_delete(d);
            return 0;
        case IDC_UP:
            model_move(d, -1);
            return 0;
        case IDC_DOWN:
            model_move(d, 1);
            return 0;
        case IDC_NAME:
            if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_KILLFOCUS)
                on_name_changed(d);
            return 0;
        case IDC_BASE:
        case IDC_KEY:
        case IDC_MID:
            if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_KILLFOCUS)
                on_text_changed(d, (int)LOWORD(wp));
            return 0;
        case IDC_BUDGET:
        case IDC_MAXTOK:
        case IDC_MAXCTX:
        case IDC_TEMP:
            if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_KILLFOCUS)
                on_number_changed(d, (HWND)lp, (int)LOWORD(wp));
            return 0;
        case IDC_UPROXY:
        case IDC_STREAM:
        case IDC_REASON:
        case IDC_VTLS:
            if (HIWORD(wp) == BN_CLICKED)
                on_check_changed(d, (int)LOWORD(wp));
            return 0;
        case IDC_API:
        case IDC_EFF:
            if (HIWORD(wp) == CBN_SELCHANGE)
                on_combo_changed(d, (int)LOWORD(wp));
            return 0;
        case IDOK:
            do_ok(d, hwnd);
            return 0;
        case IDCANCEL:
            d->ended = IDCANCEL;
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

/* ------------------------------------------------------------------ */
/* public entry                                                        */
/* ------------------------------------------------------------------ */

int dlg_config_run(HWND owner, Config *cfg)
{
    static int registered = 0;
    CfgDlg *d;
    HWND hwnd;
    int x, y, rc;

    if (cfg == NULL)
        return 0;
    if (!registered) {
        if (!dlg_register(DLG_CLASS, cfg_wndproc))
            return 0;
        registered = 1;
    }

    d = (CfgDlg *)malloc(sizeof *d);
    if (d == NULL)
        return 0;
    memset(d, 0, sizeof *d);
    d->cfg_out = cfg;
    d->cfg = *cfg;
    d->sel = -1;

    dlg_center(owner, DLG_CW + dlg_frame_w(), DLG_CH + dlg_frame_h(),
               &x, &y);
    hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, DLG_CLASS, NULL,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           x, y, DLG_CW + dlg_frame_w(),
                           DLG_CH + dlg_frame_h(), owner, NULL, DLG_INST,
                           d);
    if (hwnd == NULL) {
        free(d);
        return 0;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (owner != NULL)
        EnableWindow(owner, FALSE);
    dlg_modal_run(hwnd, &d->ended);
    if (owner != NULL) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    rc = d->ended == IDOK;
    free(d);
    return rc;
}
