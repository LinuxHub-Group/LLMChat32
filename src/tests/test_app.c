/*
 * test_app.c - application orchestration layer tests (task 12).
 *
 * Console app (built with -bc). The transport is replaced with a canned
 * SSE-style playback via app_set_transport_hook, so no network is needed.
 * Verifies the task 12 assertions:
 *   - send: on_busy(1) -> text/think fragments in order -> on_done(0) ->
 *     on_busy(0); the Session gains a user message plus an assistant
 *     message whose text blocks reassemble the stream and whose think
 *     block is stored separately; the history file is written on disk.
 *   - busy: a second app_send while one is running is refused (non-0).
 *   - abort: app_abort sets cancel -> on_done(2) -> on_busy(0), saved.
 *   - recall: app_recall(s, 1) drops message 1 and later from memory
 *     and from disk (verified by reloading); out-of-range index fails.
 *   - disk full: with a read-only history dir, app_send surfaces the
 *     save failure via on_error (no on_done) and the in-memory Session
 *     is untouched; app_recall fails and leaves memory untouched.
 *   - failed send: a transport that reports on_error immediately leaves
 *     the Session (and the saved history) without the empty assistant
 *     shell appended at send start - only the user message remains.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_app.exe ^
 *          src\tests\test_app.c src\core\app.c src\core\llm.c ^
 *          src\core\net.c src\core\tls.c src\core\http.c ^
 *          src\core\json.c src\core\config.c src\core\toml.c ^
 *          src\core\session.c src\core\utf.c build\wolfssl.lib ^
 *          ws2_32.lib advapi32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../core/app.h"
#include "../core/history.h"
#include "../core/session.h"

static int g_fail = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (cond) {                                                  \
            printf("ok %s\n", #cond);                                \
        } else {                                                     \
            printf("FAIL %s (line %d)\n", #cond, __LINE__);          \
            g_fail++;                                                \
        }                                                            \
    } while (0)

#define DIR_RW "tests\\tmp_app12"
#define DIR_RO "tests\\tmp_app12_ro"

/* --- fake event sink ------------------------------------------------------ */

enum { EV_BUSY, EV_TEXT, EV_THINK, EV_DONE, EV_ERROR };

typedef struct {
    int type;
    int msg_idx;
    int num;                    /* finish for EV_DONE, busy for EV_BUSY */
    char text[256];
} EvRec;

static EvRec g_ev[256];
static int g_nev;

static void sink_reset(void) { g_nev = 0; }

static void rec(int type, int idx, int num, const char *frag, int len)
{
    if (g_nev >= 256)
        return;
    g_ev[g_nev].type = type;
    g_ev[g_nev].msg_idx = idx;
    g_ev[g_nev].num = num;
    g_ev[g_nev].text[0] = '\0';
    if (frag != NULL && len > 0) {
        if (len > 255)
            len = 255;
        memcpy(g_ev[g_nev].text, frag, (size_t)len);
        g_ev[g_nev].text[len] = '\0';
    }
    g_nev++;
}

static void s_text(void *ctx, int idx, const char *frag, int len)
{
    (void)ctx;
    rec(EV_TEXT, idx, 0, frag, len);
}

static void s_think(void *ctx, int idx, const char *frag, int len)
{
    (void)ctx;
    rec(EV_THINK, idx, 0, frag, len);
}

static void s_done(void *ctx, int idx, int finish)
{
    (void)ctx;
    rec(EV_DONE, idx, finish, NULL, 0);
}

static void s_error(void *ctx, int idx, const char *msg)
{
    (void)ctx;
    rec(EV_ERROR, idx, 0, msg, msg ? (int)strlen(msg) : 0);
}

static void s_busy(void *ctx, int busy)
{
    (void)ctx;
    rec(EV_BUSY, -1, busy, NULL, 0);
}

static const AppEvents SINK = { s_text, s_think, s_done, s_error, s_busy };

static int count_type(int t)
{
    int i;
    int n = 0;

    for (i = 0; i < g_nev; i++)
        if (g_ev[i].type == t)
            n++;
    return n;
}

/* Concatenate every text (kind==0) or think (kind==1) block of message i. */
static void blocks_join(char *out, int cap, Session *s, int mi, int kind)
{
    Msg *m = &s->m[mi];
    int j;

    out[0] = '\0';
    for (j = 0; j < m->nb; j++) {
        if ((kind == 0 && m->b[j].kind == BLOCK_TEXT) ||
            (kind == 1 && m->b[j].kind == BLOCK_THINK)) {
            strncat(out, m->b[j].text, (size_t)cap - strlen(out) - 1);
        }
    }
}

/* --- canned transport (stands in for llm_send) ---------------------------- */

typedef struct {
    int kind;                   /* 0=text 1=think */
    const char *text;
    int pause_ms;
} Step;

typedef struct {
    const Step *steps;
    int n;
    int check_cancel;           /* when set, stop with on_done(2) on cancel */
} Script;

/* The transport's ctx param is the caller's LlmEvents ctx (owned by app.c),
   so the canned script reaches the playback through this global. */
static Script *g_script;

static int canned_send(const ModelCfg *m, const ProxyCfg *px, const Session *s,
                       const LlmEvents *ev, void *ctx,
                       const volatile int *cancel, char *err, int errcap)
{
    Script *sc = g_script;
    int i;

    (void)m;
    (void)px;
    (void)s;
    (void)err;
    (void)errcap;
    for (i = 0; i < sc->n; i++) {
        if (sc->check_cancel && cancel != NULL && *cancel) {
            if (ev->on_done)
                ev->on_done(ctx, 2);
            return 0;
        }
        if (sc->steps[i].kind == 0) {
            if (ev->on_text)
                ev->on_text(ctx, sc->steps[i].text,
                            (int)strlen(sc->steps[i].text));
        } else {
            if (ev->on_think)
                ev->on_think(ctx, sc->steps[i].text,
                             (int)strlen(sc->steps[i].text));
        }
        if (sc->steps[i].pause_ms > 0)
            Sleep((DWORD)sc->steps[i].pause_ms);
    }
    if (ev->on_done)
        ev->on_done(ctx, 0);
    return 0;
}

/* Transport that fails immediately (network unreachable, bad key, ...):
   no fragments, on_error right away, nonzero rc. */
static int failing_send(const ModelCfg *m, const ProxyCfg *px,
                        const Session *s, const LlmEvents *ev, void *ctx,
                        const volatile int *cancel, char *err, int errcap)
{
    (void)m;
    (void)px;
    (void)s;
    (void)cancel;
    if (err != NULL && errcap > 0) {
        strncpy(err, "request failed: boom", (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
    if (ev->on_error)
        ev->on_error(ctx, "boom");
    return -1;
}

/* Pump until the current send is fully drained. */
static void pump_until_idle(App *a)
{
    while (app_busy(a)) {
        app_pump(a);
        Sleep(1);
    }
    app_pump(a);
}

int main(void)
{
    Config cfg;
    Config cfg_ro;
    App *a;
    App *a2;
    Session *s;
    Session *s2;
    Msg *m;
    char err[160];
    char buf[256];
    char buf2[256];
    FILE *fp;

    static const Step script_ok[] = {
        { 0, "Hello ", 0 },
        { 1, "hmm", 0 },
        { 0, "world", 0 }
    };
    static const Step script_slow[] = {
        { 0, "one ", 40 },
        { 0, "two ", 40 },
        { 0, "three", 40 }
    };
    Script sc;

    g_script = &sc;

    CreateDirectoryA(DIR_RW, NULL);
    CreateDirectoryA(DIR_RO, NULL);
    SetFileAttributesA(DIR_RO, FILE_ATTRIBUTE_READONLY);

    config_defaults(&cfg);
    strcpy(cfg.g.history_dir, DIR_RW);
    cfg_ro = cfg;
    strcpy(cfg_ro.g.history_dir, DIR_RO);

    a = app_new(&cfg);
    CHECK(a != NULL);
    app_set_events(a, &SINK, NULL);
    app_set_transport_hook(a, canned_send);

    /* === scenario 1: normal send (busy sequence + persistence) === */
    s = session_new("app1");
    CHECK(s != NULL);
    sink_reset();
    sc.steps = script_ok;
    sc.n = 3;
    sc.check_cancel = 0;

    CHECK(app_send(a, s, "hi there") == 0);
    CHECK(app_busy(a) == 1);
    CHECK(count_type(EV_BUSY) == 1 && g_ev[0].type == EV_BUSY &&
          g_ev[0].num == 1);
    CHECK(app_send(a, s, "re-entry") != 0);   /* refused while busy */
    pump_until_idle(a);
    CHECK(app_busy(a) == 0);

    /* exactly one balanced busy pair, one done, no error */
    CHECK(count_type(EV_BUSY) == 2);
    CHECK(g_ev[g_nev - 1].type == EV_BUSY && g_ev[g_nev - 1].num == 0);
    CHECK(count_type(EV_DONE) == 1);
    CHECK(count_type(EV_ERROR) == 0);

    /* event order: busy(1) text think text done busy(0) */
    CHECK(g_nev == 6);
    CHECK(g_ev[1].type == EV_TEXT && strcmp(g_ev[1].text, "Hello ") == 0);
    CHECK(g_ev[2].type == EV_THINK && strcmp(g_ev[2].text, "hmm") == 0);
    CHECK(g_ev[3].type == EV_TEXT && strcmp(g_ev[3].text, "world") == 0);
    CHECK(g_ev[4].type == EV_DONE && g_ev[4].num == 0 &&
          g_ev[4].msg_idx == 1);

    /* session content: user added, assistant text reassembled, think apart */
    CHECK(s->nm == 2);
    CHECK(s->m[0].role == ROLE_USER);
    CHECK(s->m[0].nb == 1 && strcmp(s->m[0].b[0].text, "hi there") == 0);
    CHECK(s->m[1].role == ROLE_ASSIST);
    blocks_join(buf, sizeof buf, s, 1, 0);
    CHECK(strcmp(buf, "Hello world") == 0);
    blocks_join(buf, sizeof buf, s, 1, 1);
    CHECK(strcmp(buf, "hmm") == 0);

    /* saved on disk: reload and compare */
    s2 = session_new("app1");
    err[0] = '\0';
    CHECK(history_load(s2, DIR_RW "\\app1.json", err, sizeof err) == 0);
    CHECK(s2->nm == 2);
    blocks_join(buf, sizeof buf, s2, 1, 0);
    blocks_join(buf2, sizeof buf2, s2, 1, 1);
    CHECK(strcmp(buf, "Hello world") == 0 && strcmp(buf2, "hmm") == 0);
    session_free(s2);
    session_free(s);
    remove(DIR_RW "\\app1.json");

    /* === scenario 2: abort mid-stream -> on_done(2) === */
    s = session_new("app2");
    sink_reset();
    sc.steps = script_slow;
    sc.n = 3;
    sc.check_cancel = 1;

    CHECK(app_send(a, s, "go") == 0);
    Sleep(20);                                /* let the worker start */
    CHECK(app_busy(a) == 1);
    app_abort(a);
    pump_until_idle(a);
    CHECK(app_busy(a) == 0);
    CHECK(count_type(EV_BUSY) == 2);
    CHECK(g_ev[0].type == EV_BUSY && g_ev[0].num == 1);
    CHECK(g_ev[g_nev - 1].type == EV_BUSY && g_ev[g_nev - 1].num == 0);
    CHECK(count_type(EV_ERROR) == 0);
    CHECK(count_type(EV_DONE) == 1);
    CHECK(g_ev[g_nev - 2].type == EV_DONE && g_ev[g_nev - 2].num == 2);
    CHECK(s->nm == 2);                        /* partial text kept in memory */
    /* aborted state saved too */
    s2 = session_new("app2");
    err[0] = '\0';
    CHECK(history_load(s2, DIR_RW "\\app2.json", err, sizeof err) == 0);
    CHECK(s2->nm == 2);
    session_free(s2);
    session_free(s);
    remove(DIR_RW "\\app2.json");

    /* === scenario 3: recall === */
    s = session_new("app3");
    strcpy(s->title, "recall");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "u1");
    m = session_add_msg(s, ROLE_ASSIST);
    msg_add_block(m, BLOCK_TEXT, "a1");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "u2");
    err[0] = '\0';
    CHECK(history_save(s, DIR_RW, err, sizeof err) == 0);

    sink_reset();
    err[0] = '\0';
    CHECK(app_recall(a, s, 1, err, sizeof err) == 0);
    CHECK(s->nm == 1);
    CHECK(s->m[0].nb == 1 && strcmp(s->m[0].b[0].text, "u1") == 0);
    /* disk synced: reload shows the truncation */
    s2 = session_new("app3");
    err[0] = '\0';
    CHECK(history_load(s2, DIR_RW "\\app3.json", err, sizeof err) == 0);
    CHECK(s2->nm == 1 && strcmp(s2->m[0].b[0].text, "u1") == 0);
    session_free(s2);
    /* out-of-range recall fails, memory untouched */
    err[0] = '\0';
    CHECK(app_recall(a, s, 99, err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    CHECK(s->nm == 1);
    /* recall to zero empties the session and persists that */
    err[0] = '\0';
    CHECK(app_recall(a, s, 0, err, sizeof err) == 0);
    CHECK(s->nm == 0 && s->m == NULL);
    fp = fopen(DIR_RW "\\app3.json", "rb");
    CHECK(fp != NULL);
    if (fp)
        fclose(fp);
    session_free(s);
    remove(DIR_RW "\\app3.json");

    /* === scenario 4: disk full (read-only history dir) === */
    a2 = app_new(&cfg_ro);
    CHECK(a2 != NULL);
    app_set_events(a2, &SINK, NULL);
    app_set_transport_hook(a2, canned_send);

    s = session_new("app4");
    sink_reset();
    sc.steps = script_ok;
    sc.n = 3;
    sc.check_cancel = 0;
    CHECK(app_send(a2, s, "hi") == 0);
    pump_until_idle(a2);
    /* save failure surfaced via on_error, no on_done after on_error */
    CHECK(count_type(EV_ERROR) == 1);
    CHECK(count_type(EV_DONE) == 0);
    CHECK(g_ev[g_nev - 2].type == EV_ERROR && g_ev[g_nev - 2].text[0] != '\0');
    /* busy pair still balanced */
    CHECK(count_type(EV_BUSY) == 2);
    CHECK(g_ev[g_nev - 1].type == EV_BUSY && g_ev[g_nev - 1].num == 0);
    /* Session memory unchanged (full assistant text still present) */
    CHECK(s->nm == 2);
    blocks_join(buf, sizeof buf, s, 1, 0);
    CHECK(strcmp(buf, "Hello world") == 0);
    /* nothing was written into the read-only dir */
    fp = fopen(DIR_RO "\\app4.json", "rb");
    CHECK(fp == NULL);
    if (fp)
        fclose(fp);
    session_free(s);

    /* recall against a read-only dir: fails, memory untouched */
    s = session_new("app5");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "keep");
    m = session_add_msg(s, ROLE_ASSIST);
    msg_add_block(m, BLOCK_TEXT, "drop");
    sink_reset();
    err[0] = '\0';
    CHECK(app_recall(a2, s, 1, err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    CHECK(s->nm == 2);
    CHECK(count_type(EV_ERROR) == 1);
    session_free(s);

    /* === scenario 5: terminal queue push OOM -> busy recovers, on_error === */
    /* script_ok pushes 3 fragments + 1 terminal node; fail the 4th push so
       the worker's QDONE is lost and the pump must synthesize on_error */
    s = session_new("app7");
    sink_reset();
    sc.steps = script_ok;
    sc.n = 3;
    sc.check_cancel = 0;
    app_test_q_push_fail_at(a, 4);
    CHECK(app_send(a, s, "hi") == 0);
    pump_until_idle(a);
    CHECK(app_busy(a) == 0);                  /* busy cannot wedge */
    CHECK(count_type(EV_ERROR) == 1);
    CHECK(g_ev[g_nev - 2].type == EV_ERROR &&
          strcmp(g_ev[g_nev - 2].text, "out of memory") == 0);
    CHECK(count_type(EV_DONE) == 0);
    CHECK(count_type(EV_BUSY) == 2 &&
          g_ev[g_nev - 1].type == EV_BUSY && g_ev[g_nev - 1].num == 0);
    /* fragments arrived before the loss and stay in memory, and the
       terminal event still saved them */
    CHECK(s->nm == 2);
    blocks_join(buf, sizeof buf, s, 1, 0);
    CHECK(strcmp(buf, "Hello world") == 0);
    s2 = session_new("app7");
    err[0] = '\0';
    CHECK(history_load(s2, DIR_RW "\\app7.json", err, sizeof err) == 0);
    CHECK(s2->nm == 2);
    blocks_join(buf, sizeof buf, s2, 1, 0);
    CHECK(strcmp(buf, "Hello world") == 0);
    session_free(s2);
    session_free(s);
    remove(DIR_RW "\\app7.json");

    /* === scenario 6: failed send drops the empty assistant shell === */
    app_set_transport_hook(a, failing_send);
    s = session_new("app8");
    sink_reset();
    CHECK(app_send(a, s, "hello") == 0);
    pump_until_idle(a);
    CHECK(app_busy(a) == 0);
    CHECK(count_type(EV_ERROR) == 1);
    CHECK(count_type(EV_DONE) == 0);
    CHECK(g_ev[g_nev - 2].type == EV_ERROR);
    /* the shell appended at send start is gone: only the user message */
    CHECK(s->nm == 1);
    CHECK(s->m[0].role == ROLE_USER);
    CHECK(s->m[0].nb == 1 && strcmp(s->m[0].b[0].text, "hello") == 0);
    /* the saved history must not contain the empty assistant either */
    s2 = session_new("app8");
    err[0] = '\0';
    CHECK(history_load(s2, DIR_RW "\\app8.json", err, sizeof err) == 0);
    CHECK(s2->nm == 1);
    CHECK(s2->m[0].role == ROLE_USER);
    session_free(s2);
    session_free(s);
    remove(DIR_RW "\\app8.json");
    app_set_transport_hook(a, canned_send);

    /* app_free with a still-pending stream must not crash or leak */
    s = session_new("app6");
    sink_reset();
    sc.steps = script_ok;
    sc.n = 3;
    sc.check_cancel = 0;
    CHECK(app_send(a, s, "x") == 0);
    app_free(a);                              /* joins the worker thread */
    CHECK(1);
    session_free(s);
    remove(DIR_RW "\\app6.json");

    app_free(a2);

    /* --- teardown --- */
    SetFileAttributesA(DIR_RO, FILE_ATTRIBUTE_DIRECTORY);
    RemoveDirectoryA(DIR_RW);
    RemoveDirectoryA(DIR_RO);

    if (g_fail == 0) {
        printf("PASS test_app\n");
        return 0;
    }
    printf("FAIL test_app (%d failures)\n", g_fail);
    return 1;
}
