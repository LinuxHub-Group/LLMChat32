/*
 * mainwin.c - main window: class, menu, tab strip, chat view, input bar,
 * status strip, and the wiring that ties the core (app/history/session)
 * to the UI.
 *
 * The client area hosts the tab strip ("LlmTabBar" child) in its top
 * ~28px, the conversation view ("LlmChat") below it, the input strip
 * ("LlmInputBar", see inputbar.h) above the self-drawn status strip at
 * the bottom. The menu is assembled programmatically from LS() strings -
 * there is no .rc menu.
 *
 * Ownership: mainwin owns the Session list (s_sess, capped at
 * MAX_SESSIONS). Sessions are loaded at startup from
 * g_cfg.g.history_dir via history_list + history_load; a fresh empty
 * session is created on New-tab / when the last one closes. Closing a
 * tab history_delete()s its file, frees the Session and selects a
 * neighbor. Everything is freed on WM_DESTROY.
 *
 * The App object (Task 12) is created here too; app events are sunk into
 * the UI (chatview invalidation, status strip, input bar button), and a
 * 50ms SetTimer drives app_pump() - simple and Win98-fine.
 *
 * Busy policy: while a send is in flight (app_busy) tab switching,
 * closing, new-tab, config edits and recall are blocked (the tabbar
 * selection is reverted) - the Session being streamed into must stay
 * the current one. Recall itself: right-clicking a message bubble in
 * the chatview pops a one-item menu (enabled only for the user's own
 * messages); choosing it posts WM_APP_RECALL (chatview.h), which lands
 * in do_recall() below: app_recall() truncates the message and its
 * successors, persists, then the view is fully relaid out.
 */
#include "mainwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "../core/app.h"
#include "../core/history.h"
#include "../core/session.h"
#include "../core/utf.h"
#include "chatview.h"
#include "commands.h"
#include "dlg_config.h"
#include "dlg_view.h"
#include "inputbar.h"
#include "strings.h"
#include "tabbar.h"
#include "ui.h"

#define MAIN_CLASS      "LLMChat32Main"
#define STATUS_STRIP_H  22
#define STATUS_PAD_X    6

#define MAX_SESSIONS    32
#define TIMER_PUMP_ID   1
#define TIMER_PUMP_MS   50

HWND g_hwnd_main = NULL;

/* status strip content: LS() string unless s_status_text is non-empty
 * (e.g. an error message or a session title override). */
static StrID s_status = S_STATUS_READY;
static char s_status_text[128];   /* UTF-8 override */

static HWND s_tabbar = NULL;
static HWND s_chat = NULL;
static HWND s_input = NULL;

static App *s_app = NULL;
static Session *s_sess[MAX_SESSIONS];
static int s_nsess = 0;
static int s_cur = -1;            /* index into s_sess, -1 = none */

/* ------------------------------------------------------------------ */
/* status strip helpers                                                */

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

/* ------------------------------------------------------------------ */
/* session list (owned here)                                           */

static int session_index_of(const char *id)
{
    int i;

    for (i = 0; i < s_nsess; i++)
        if (strcmp(s_sess[i]->id, id) == 0)
            return i;
    return -1;
}

/* Unique time+counter id ("t<hex>-<n>", <= 23 chars). */
static void make_session_id(char *id, int cap)
{
    static unsigned long seq = 0;
    unsigned long base = (unsigned long)time(NULL);

    for (;;) {
        snprintf(id, cap, "t%lx-%lu", base, seq++);
        if (session_index_of(id) < 0)
            return;
    }
}

/* Append a fresh unsaved session; NULL when the list is full or OOM. */
static Session *session_add_blank(void)
{
    char id[24];
    Session *s;

    if (s_nsess >= MAX_SESSIONS)
        return NULL;
    make_session_id(id, sizeof id);
    s = session_new(id);
    if (s == NULL)
        return NULL;
    s_sess[s_nsess] = s;
    s_nsess++;
    return s;
}

/* 1 when the title was filled from the first line of text. */
static int session_autotitle(Session *s, const char *text)
{
    int i = 0;
    int n = 0;

    if (s->title[0] != '\0')
        return 0;
    while (text[i] != '\0' && n < 20 && text[i] != '\n' && text[i] != '\r') {
        unsigned cp;
        int k = utf8_decode_next(text + i, (int)strlen(text + i), &cp);

        if (k <= 0)
            break;
        i += k;
        n++;
    }
    if (i == 0)
        return 0;
    if (i > (int)sizeof s->title - 1)
        i = (int)sizeof s->title - 1;
    memcpy(s->title, text, (size_t)i);
    s->title[i] = '\0';
    return 1;
}

static void tabs_refresh(int select_idx)
{
    tabbar_set_sessions(s_sess, s_nsess);
    if (select_idx >= 0 && select_idx < s_nsess)
        tabbar_set_current(select_idx);
}

/* Show session idx (must be valid): point the chatview at it, mirror its
 * title into the status strip and make it current. */
static void show_session(int idx)
{
    s_cur = idx;
    chatview_set_session(s_sess[idx]);
    if (s_sess[idx]->title[0] != '\0') {
        status_set_text(s_sess[idx]->title);
    } else {
        s_status_text[0] = '\0';
        s_status = S_STATUS_READY;
    }
    status_invalidate(g_hwnd_main);
}

/* Close idx: delete the history file, free the Session, drop it from the
 * list and select a neighbor (a fresh session when the last one goes). */
static void close_session(int idx)
{
    int sel;

    if (idx < 0 || idx >= s_nsess)
        return;
    history_delete(g_cfg.g.history_dir, s_sess[idx]->id);
    session_free(s_sess[idx]);
    memmove(&s_sess[idx], &s_sess[idx + 1],
            (size_t)(s_nsess - idx - 1) * sizeof s_sess[0]);
    s_nsess--;
    if (s_nsess == 0)
        session_add_blank();        /* keep at least one tab */
    sel = idx < s_nsess ? idx : s_nsess - 1;
    tabs_refresh(sel);
    if (s_nsess > 0)
        show_session(sel);
    else {
        s_cur = -1;
        chatview_set_session(NULL);
        status_invalidate(g_hwnd_main);
    }
}

static void sessions_init(void)
{
    char ids[MAX_SESSIONS][24];
    int n = history_list(g_cfg.g.history_dir, ids, MAX_SESSIONS);
    int i;
    char skipped[512];          /* one combined UTF-8 notice body */
    int nskip = 0;

    skipped[0] = '\0';
    for (i = 0; i < n && s_nsess < MAX_SESSIONS; i++) {
        char path[600];
        char err[128];
        Session *s = session_new(ids[i]);

        if (s == NULL)
            break;
        sprintf(path, "%s\\%s.json", g_cfg.g.history_dir, ids[i]);
        if (history_load(s, path, err, (int)sizeof err) != 0) {
            /* corrupt/unreadable file: skip this session, remember the
               name for one combined notice at the end; loading the
               remaining sessions must not be aborted */
            session_free(s);
            nskip++;
            if (skipped[0] == '\0')
                strncpy(skipped, LS(S_CORRUPT_BODY), sizeof skipped - 1);
            skipped[sizeof skipped - 1] = '\0';
            strncat(skipped, ids[i],
                    sizeof skipped - strlen(skipped) - 1);
            strncat(skipped, ".json\n",
                    sizeof skipped - strlen(skipped) - 1);
            continue;
        }
        s_sess[s_nsess] = s;
        s_nsess++;
    }
    if (s_nsess == 0)
        session_add_blank();        /* unsaved until the first send ends */
    if (nskip > 0)
        ui_msg_a(g_hwnd_main, skipped, LS(S_CORRUPT_TITLE),
                 MB_OK | MB_ICONWARNING);
}

static void sessions_free_all(void)
{
    int i;

    for (i = 0; i < s_nsess; i++) {
        session_free(s_sess[i]);
        s_sess[i] = NULL;
    }
    s_nsess = 0;
    s_cur = -1;
}

/* ------------------------------------------------------------------ */
/* app event sink -> UI                                                */

/* The pump already appended the fragment to the Session; the sink only
 * repaints. */
static void ev_text(void *ctx, int msg_idx, const char *frag, int len)
{
    (void)ctx; (void)frag; (void)len;
    chatview_invalidate_msg(msg_idx);
    chatview_scroll_bottom();
}

static void ev_done(void *ctx, int msg_idx, int finish)
{
    (void)ctx; (void)msg_idx;
    s_status_text[0] = '\0';
    s_status = (finish == 2) ? S_STATUS_ABORTED : S_STATUS_READY;
    inputbar_set_sending(0);
    status_invalidate(g_hwnd_main);
}

static void ev_error(void *ctx, int msg_idx, const char *msg)
{
    (void)ctx; (void)msg_idx;
    status_set_text(msg);
    inputbar_set_sending(0);
    status_invalidate(g_hwnd_main);
}

static void ev_busy(void *ctx, int busy)
{
    (void)ctx;
    inputbar_set_sending(busy);
    if (busy) {
        s_status_text[0] = '\0';
        s_status = S_STATUS_SENDING;
        status_invalidate(g_hwnd_main);
    }
}

static const AppEvents APP_EV = { ev_text, ev_text, ev_done, ev_error,
                                  ev_busy };

/* ------------------------------------------------------------------ */
/* send flow                                                           */

static void do_send_or_abort(void)
{
    char buf[INPUTBAR_TEXT_MAX];
    size_t len;
    int rc;

    if (s_app != NULL && app_busy(s_app)) {
        app_abort(s_app);
        return;
    }
    inputbar_get_text(buf, sizeof buf);
    len = strlen(buf);
    while (len > 0 &&
           (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
            buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        buf[--len] = '\0';
    if (len == 0)
        return;
    if (s_cur < 0 || s_cur >= s_nsess) {
        if (session_add_blank() == NULL) {
            status_set_text(LS(S_SEND_FAILED));
            status_invalidate(g_hwnd_main);
            return;
        }
        s_cur = s_nsess - 1;
        tabs_refresh(s_cur);
        show_session(s_cur);
    }
    if (session_autotitle(s_sess[s_cur], buf))
        tabs_refresh(s_cur);
    rc = app_send(s_app, s_sess[s_cur], buf);
    if (rc != 0) {
        status_set_text(LS(S_SEND_FAILED));
        status_invalidate(g_hwnd_main);
        return;
    }
    /* on_busy(1) fired inside app_send: status -> sending, button ->
     * abort. Just clear the input. */
    inputbar_clear();
}

/* ------------------------------------------------------------------ */
/* layout                                                              */

static void layout_tabbar(HWND hwnd)
{
    RECT rc;

    if (s_tabbar == NULL)
        return;
    GetClientRect(hwnd, &rc);
    MoveWindow(s_tabbar, 0, 0, rc.right, TABBAR_HEIGHT, TRUE);
}

/* the chat view takes the client area between the tab strip and the
 * input strip */
static void layout_chat(HWND hwnd)
{
    RECT rc;

    if (s_chat == NULL)
        return;
    GetClientRect(hwnd, &rc);
    MoveWindow(s_chat, 0, TABBAR_HEIGHT, rc.right,
               rc.bottom - TABBAR_HEIGHT - STATUS_STRIP_H - INPUTBAR_HEIGHT,
               TRUE);
}

static void layout_inputbar(HWND hwnd)
{
    RECT rc;

    if (s_input == NULL)
        return;
    GetClientRect(hwnd, &rc);
    MoveWindow(s_input, 0, rc.bottom - STATUS_STRIP_H - INPUTBAR_HEIGHT,
               rc.right, INPUTBAR_HEIGHT, TRUE);
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
/* chatview link notifications (Task 18)                               */

/* The message index travelled through PostMessage; the current session
 * may have changed since the click, so validate everything. */
static void open_think(int msg_idx)
{
    Msg *m;
    char *buf;
    size_t need = 1;
    int j;

    if (s_cur < 0 || s_cur >= s_nsess || msg_idx < 0
        || msg_idx >= s_sess[s_cur]->nm)
        return;
    m = &s_sess[s_cur]->m[msg_idx];
    for (j = 0; j < m->nb; j++)
        if (m->b[j].kind == BLOCK_THINK && m->b[j].text != NULL)
            need += strlen(m->b[j].text);
    buf = (char *)malloc(need);
    if (buf == NULL)
        return;
    buf[0] = '\0';
    for (j = 0; j < m->nb; j++)
        if (m->b[j].kind == BLOCK_THINK && m->b[j].text != NULL)
            strcat(buf, m->b[j].text);
    dlg_view_text(g_hwnd_main, LS(S_THINK_TITLE), buf);
    free(buf);
}

static void open_image(int msg_idx, int blk_idx)
{
    Msg *m;
    const char *url;

    if (s_cur < 0 || s_cur >= s_nsess || msg_idx < 0
        || msg_idx >= s_sess[s_cur]->nm)
        return;
    m = &s_sess[s_cur]->m[msg_idx];
    if (blk_idx < 0 || blk_idx >= m->nb
        || m->b[blk_idx].kind != BLOCK_IMAGE)
        return;
    url = m->b[blk_idx].text;
    if (url == NULL || url[0] == '\0')
        return;
    dlg_view_image(g_hwnd_main, url,
                   &g_cfg.m[inputbar_selected_model()], &g_cfg.p);
}

/* ------------------------------------------------------------------ */
/* recall (Task 20)                                                    */

/* Recall msg_idx of the current session (spec section 8: that message
 * and everything after it are deleted and persisted). Blocked while a
 * send is in flight: app_recall() truncates the very Session the worker
 * is streaming into. */
static void do_recall(int msg_idx)
{
    char err[256];

    if (s_cur < 0 || s_cur >= s_nsess || msg_idx < 0
        || msg_idx >= s_sess[s_cur]->nm)
        return;
    if (app_busy(s_app))
        return;
    if (app_recall(s_app, s_sess[s_cur], msg_idx, err, (int)sizeof err)
        != 0) {
        ui_msg_a(g_hwnd_main, (err[0] != '\0') ? err : LS(S_RECALL_FAILED),
                 LS(S_RECALL_FAILED), MB_OK | MB_ICONERROR);
        return;
    }
    /* same session, truncated content: a full relayout */
    chatview_set_session(s_sess[s_cur]);
    status_set_text(LS(S_RECALL_DONE));
    status_invalidate(g_hwnd_main);
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
        /* switching mid-send is blocked: the streaming Session must stay
         * the current one (tabbar selection is reverted) */
        if (app_busy(s_app)) {
            tabbar_set_current(s_cur);
            return 0;
        }
        if (lp >= 0 && lp < s_nsess)
            show_session((int)lp);
        return 0;

    case WM_APP_TAB_NEW:
        if (app_busy(s_app)) {
            tabbar_set_current(s_cur);
            return 0;
        }
        if (session_add_blank() != NULL) {
            tabs_refresh(s_nsess - 1);
            show_session(s_nsess - 1);
        }
        return 0;

    case WM_APP_TAB_CLOSE:
        if (app_busy(s_app)) {
            tabbar_set_current(s_cur);
            return 0;
        }
        close_session((int)lp);
        return 0;

    case WM_APP_OPEN_THINK:
        open_think((int)wp);
        return 0;

    case WM_APP_OPEN_IMAGE:
        open_image((int)wp, (int)lp);
        return 0;

    case WM_APP_RECALL:
        do_recall((int)wp);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_PUMP_ID && s_app != NULL)
            app_pump(s_app);
        return 0;

    case WM_SIZE:
        layout_tabbar(hwnd);
        layout_chat(hwnd);
        layout_inputbar(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDIB_SEND:
            do_send_or_abort();
            break;
        case IDIB_MODEL:
            if (HIWORD(wp) == CBN_SELCHANGE && s_app != NULL)
                app_set_model(s_app, inputbar_selected_model());
            break;
        case IDM_NEW_CHAT:
            if (!app_busy(s_app) && session_add_blank() != NULL) {
                tabs_refresh(s_nsess - 1);
                show_session(s_nsess - 1);
            }
            break;
        case IDM_CLOSE_CHAT:
            if (!app_busy(s_app))
                close_session(s_cur);
            break;
        case IDM_RECALL:
            /* Edit -> recall the user's most recent message */
            if (!app_busy(s_app) && s_cur >= 0 && s_cur < s_nsess) {
                int k = s_sess[s_cur]->nm;

                while (--k >= 0)
                    if (s_sess[s_cur]->m[k].role == ROLE_USER)
                        break;
                if (k >= 0)
                    do_recall(k);
            }
            break;
        case IDM_CLEAR:
            /* later: clear conversation view */
            break;
        case IDM_MODEL_CFG:
            /* the dialog mutates g_cfg; a running worker reads the
               App's private snapshot of it, so refuse mid-send */
            if (!app_busy(s_app) && dlg_config_run(hwnd, &g_cfg) == 1) {
                inputbar_set_models(&g_cfg);
                if (s_app != NULL)
                    app_update_config(s_app, &g_cfg);
                status_set_text(LS(S_CONFIG_SAVED));
                status_invalidate(hwnd);
            }
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
        KillTimer(hwnd, TIMER_PUMP_ID);
        if (s_app != NULL) {
            app_free(s_app);    /* cancels + joins a running worker */
            s_app = NULL;
        }
        sessions_free_all();
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
    s_chat = chatview_create(hwnd);
    s_input = inputbar_create(hwnd);
    layout_tabbar(hwnd);
    layout_chat(hwnd);
    layout_inputbar(hwnd);
    /* Task 16 background: the config is already loaded (main.c copied
     * it into g_cfg before this call) and the chatview exists now - it
     * did NOT at config-load time, so this post-create init is the
     * earliest correct wiring point. Empty bg_image / load failure
     * silently keeps the plain white background. */
    chatview_set_background(g_cfg.g.bg_image, g_cfg.g.bg_opacity);

    /* the app orchestration layer + its UI event sink */
    s_app = app_new(&g_cfg);
    if (s_app != NULL) {
        app_set_events(s_app, &APP_EV, NULL);
        inputbar_set_models(&g_cfg);
        app_set_model(s_app, inputbar_selected_model());
    }

    /* real sessions from the history dir (falls back to one blank) */
    if (g_cfg.g.history_dir[0] != '\0')
        CreateDirectoryA(g_cfg.g.history_dir, NULL);  /* ok if already there */
    sessions_init();
    if (s_nsess > 0) {
        tabs_refresh(0);
        show_session(0);
    } else {
        tabs_refresh(-1);
        chatview_set_session(NULL);
    }

    SetTimer(hwnd, TIMER_PUMP_ID, TIMER_PUMP_MS, NULL);
    return hwnd;
}
