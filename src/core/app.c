/*
 * app.c - application orchestration layer (see app.h).
 *
 * Threading model: app_send() runs on the UI/test thread and only touches
 * the Session there. The worker thread (CLIB _beginthread, which returns
 * the Win32 thread handle used for the join) calls the transport
 * (llm_send, or the app_set_transport_hook replacement) and forwards its
 * LlmEvents into a locked queue of heap nodes; it never reads or writes
 * the Session beyond passing the const pointer to the transport while
 * building the request. app_pump() on the UI/test thread drains the
 * queue: fragments are appended to the assistant message's blocks (a new
 * block per kind change, otherwise the fragment is concatenated onto the
 * last same-kind block), terminal nodes save the Session to the history
 * dir (tmp+rename via history_save) and then fire exactly one of
 * on_done/on_error followed by on_busy(0). A failed save fires on_error
 * with the disk message and leaves the in-memory Session untouched. If
 * the worker cannot queue its terminal node at all (allocation failure),
 * it sets term_lost and app_pump() synthesizes the terminal event as
 * on_error("out of memory"), so app_busy() can never wedge.
 *
 * app_free() cancels and joins a still-running worker thread
 * (WaitForSingleObject on the _beginthread handle, then CloseHandle) and
 * releases any queue nodes the pump never claimed.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <process.h>    /* _beginthread */
#include <stdio.h>
#include <stdlib.h>     /* malloc, realloc, free */
#include <string.h>
#include <windows.h>    /* CRITICAL_SECTION, WaitForSingleObject */

#include "app.h"
#include "history.h"

/* queue node types */
enum { QTEXT, QTHINK, QDONE, QERROR };

typedef struct QNode {
    struct QNode *next;
    int type;                   /* QTEXT/QTHINK/QDONE/QERROR */
    int finish;                 /* QDONE: 0=stop 1=length 2=aborted */
    int len;
    char *data;                 /* heap copy of the fragment/message */
} QNode;

struct App {
    Config cfg;                 /* snapshot taken at app_new */
    AppEvents ev;
    void *ev_ctx;
    AppTransportFn transport;   /* NULL -> real llm_send */

    volatile int cancel;        /* set by app_abort, polled by transport */
    volatile int term_lost;     /* worker: terminal node could not be queued */
    long q_push_count;          /* test-only OOM injection counter */
    long q_push_fail_at;        /* test-only: one-shot fail the n-th q_push */
    int busy;                   /* owned by the UI/test thread only */
    int oom;                    /* fragment-append OOM seen by the pump */
    int model_idx;              /* selected Config.m[] entry (see app_set_model) */
    Session *sess;              /* session of the in-flight send */
    int msg_idx;                /* assistant message being filled */

    CRITICAL_SECTION qlock;     /* guards qhead/qtail */
    QNode *qhead;
    QNode *qtail;

    unsigned long thandle;      /* worker thread handle, 0 when none */
};

/* --- queue (worker pushes, pump pops) ------------------------------------- */

static int q_push(App *a, int type, int finish, const char *data, int len)
{
    QNode *n;

    /* test-only OOM injection: one-shot failure of the n-th push */
    if (a->q_push_fail_at > 0 && ++a->q_push_count >= a->q_push_fail_at) {
        a->q_push_fail_at = 0;
        return -1;
    }
    n = (QNode *)malloc(sizeof(QNode));
    if (n == NULL)
        return -1;              /* OOM: caller decides how to recover */
    n->next = NULL;
    n->type = type;
    n->finish = finish;
    n->len = len;
    n->data = (char *)malloc((size_t)len + 1);
    if (n->data == NULL) {
        free(n);
        return -1;
    }
    if (len > 0)
        memcpy(n->data, data, (size_t)len);
    n->data[len] = '\0';

    EnterCriticalSection(&a->qlock);
    if (a->qtail != NULL)
        a->qtail->next = n;
    else
        a->qhead = n;
    a->qtail = n;
    LeaveCriticalSection(&a->qlock);
    return 0;
}

static QNode *q_pop(App *a)
{
    QNode *n;

    EnterCriticalSection(&a->qlock);
    n = a->qhead;
    if (n != NULL) {
        a->qhead = n->next;
        if (a->qhead == NULL)
            a->qtail = NULL;
    }
    LeaveCriticalSection(&a->qlock);
    return n;
}

static void q_free_all(App *a)
{
    QNode *n = a->qhead;

    while (n != NULL) {
        QNode *nx = n->next;
        free(n->data);
        free(n);
        n = nx;
    }
    a->qhead = NULL;
    a->qtail = NULL;
}

/* --- worker thread --------------------------------------------------------- */

typedef struct {
    App *a;
    int finish;                 /* captured on_done finish reason */
    int has_err;
    char err[256];
} WCtx;

static void w_on_text(void *ctx, const char *frag, int len)
{
    WCtx *w = (WCtx *)ctx;

    if (!w->has_err)
        q_push(w->a, QTEXT, 0, frag, len);
}

static void w_on_think(void *ctx, const char *frag, int len)
{
    WCtx *w = (WCtx *)ctx;

    if (!w->has_err)
        q_push(w->a, QTHINK, 0, frag, len);
}

static void w_on_done(void *ctx, int finish)
{
    WCtx *w = (WCtx *)ctx;

    w->finish = finish;
}

static void w_on_error(void *ctx, const char *msg)
{
    WCtx *w = (WCtx *)ctx;

    if (!w->has_err) {
        w->has_err = 1;
        strncpy(w->err, msg, sizeof(w->err) - 1);
        w->err[sizeof(w->err) - 1] = '\0';
    }
}

static const LlmEvents WORKER_EV = { w_on_text, w_on_think, w_on_done,
                                     w_on_error };

static void worker_main(void *arg)
{
    App *a = (App *)arg;
    WCtx w;
    int rc;

    w.a = a;
    w.finish = 0;
    w.has_err = 0;
    w.err[0] = '\0';

    if (a->transport != NULL)
        rc = a->transport(&a->cfg.m[a->model_idx], &a->cfg.p, a->sess,
                          &WORKER_EV, &w, &a->cancel, w.err,
                          (int)sizeof(w.err));
    else
        rc = llm_send(&a->cfg.m[a->model_idx], &a->cfg.p, a->sess, &WORKER_EV,
                      &w, &a->cancel, w.err, (int)sizeof(w.err));

    /* a lost terminal node would leave app_busy() stuck forever, so flag
       it; the pump synthesizes the terminal event (on_error) instead */
    if (w.has_err) {
        if (q_push(a, QERROR, 0, w.err, (int)strlen(w.err)) != 0)
            a->term_lost = 1;
    } else if (rc != 0) {
        const char *msg = w.err[0] != '\0' ? w.err : "request failed";
        if (q_push(a, QERROR, 0, msg, (int)strlen(msg)) != 0)
            a->term_lost = 1;
    } else {
        if (q_push(a, QDONE, w.finish, NULL, 0) != 0)
            a->term_lost = 1;
    }
}

/* --- session helpers (UI/test thread only) --------------------------------- */

/* Free messages [nm, s->nm) and shrink the array. */
static void session_truncate(Session *s, int nm)
{
    int i;
    int j;

    if (nm < 0)
        nm = 0;
    for (i = nm; i < s->nm; i++)
        for (j = 0; j < s->m[i].nb; j++)
            free(s->m[i].b[j].text);
    if (nm == 0) {
        free(s->m);
        s->m = NULL;
    } else {
        Msg *p = (Msg *)realloc(s->m, (size_t)nm * sizeof(Msg));
        if (p != NULL)
            s->m = p;
    }
    s->nm = nm;
}

static int block_append(Block *b, const char *frag, int len)
{
    size_t old = strlen(b->text);
    char *p = (char *)realloc(b->text, old + (size_t)len + 1);

    if (p == NULL)
        return -1;
    memcpy(p + old, frag, (size_t)len);
    p[old + (size_t)len] = '\0';
    b->text = p;
    return 0;
}

/* --- pump ------------------------------------------------------------------ */

static void pump_frag(App *a, QNode *n)
{
    Msg *m;
    BlockKind k = (n->type == QTHINK) ? BLOCK_THINK : BLOCK_TEXT;

    if (a->sess == NULL || a->msg_idx < 0 || a->msg_idx >= a->sess->nm)
        return;
    m = &a->sess->m[a->msg_idx];
    if (m->nb > 0 && m->b[m->nb - 1].kind == k) {
        if (block_append(&m->b[m->nb - 1], n->data, n->len) != 0)
            a->oom = 1;
    } else {
        if (msg_add_block(m, k, n->data) == NULL)
            a->oom = 1;
    }
    /* the fragment is forwarded even if the append ran out of memory:
       the UI can still render the stream, and the terminal event reports
       the failure */
    if (k == BLOCK_THINK) {
        if (a->ev.on_think)
            a->ev.on_think(a->ev_ctx, a->msg_idx, n->data, n->len);
    } else {
        if (a->ev.on_text)
            a->ev.on_text(a->ev_ctx, a->msg_idx, n->data, n->len);
    }
}

static void pump_term(App *a, QNode *n)
{
    char serr[160];
    int save_ok;

    /* join the worker before touching the Session on this thread */
    if (a->thandle != 0) {
        WaitForSingleObject((HANDLE)a->thandle, INFINITE);
        CloseHandle((HANDLE)a->thandle);
        a->thandle = 0;
    }

    serr[0] = '\0';
    save_ok = (a->sess != NULL &&
               history_save(a->sess, a->cfg.g.history_dir,
                            serr, (int)sizeof(serr)) == 0);

    if (!save_ok) {
        /* disk failure: report it, keep the in-memory Session as is */
        if (a->ev.on_error)
            a->ev.on_error(a->ev_ctx, a->msg_idx,
                           serr[0] != '\0' ? serr : "history save failed");
    } else if (a->oom || n == NULL) {
        /* n == NULL: the worker's terminal node was lost to OOM and the
           pump synthesized this terminal event */
        if (a->ev.on_error)
            a->ev.on_error(a->ev_ctx, a->msg_idx, "out of memory");
    } else if (n->type == QERROR) {
        if (a->ev.on_error)
            a->ev.on_error(a->ev_ctx, a->msg_idx, n->data);
    } else {
        if (a->ev.on_done)
            a->ev.on_done(a->ev_ctx, a->msg_idx, n->finish);
    }

    a->busy = 0;
    a->sess = NULL;
    a->msg_idx = -1;
    a->oom = 0;
    if (a->ev.on_busy)
        a->ev.on_busy(a->ev_ctx, 0);
}

void app_pump(App *a)
{
    QNode *n;

    if (a == NULL)
        return;
    for (;;) {
        n = q_pop(a);
        if (n == NULL)
            break;
        if (n->type == QTEXT || n->type == QTHINK)
            pump_frag(a, n);
        else
            pump_term(a, n);
        free(n->data);
        free(n);
    }
    /* a terminal node lost to worker-side OOM would wedge busy forever:
       synthesize the terminal event here, exactly once */
    if (a->term_lost && a->busy) {
        a->term_lost = 0;
        pump_term(a, NULL);
    }
}

/* --- public API ------------------------------------------------------------ */

App *app_new(const Config *cfg)
{
    App *a = (App *)calloc(1, sizeof(App));

    if (a == NULL)
        return NULL;
    if (cfg != NULL)
        a->cfg = *cfg;
    else
        config_defaults(&a->cfg);
    a->msg_idx = -1;
    a->model_idx = 0;
    InitializeCriticalSection(&a->qlock);
    return a;
}

void app_free(App *a)
{
    if (a == NULL)
        return;
    if (a->thandle != 0) {
        a->cancel = 1;
        WaitForSingleObject((HANDLE)a->thandle, INFINITE);
        CloseHandle((HANDLE)a->thandle);
        a->thandle = 0;
    }
    q_free_all(a);
    DeleteCriticalSection(&a->qlock);
    free(a);
}

void app_set_events(App *a, const AppEvents *ev, void *ctx)
{
    if (a == NULL)
        return;
    if (ev != NULL) {
        a->ev = *ev;
        a->ev_ctx = ctx;
    } else {
        memset(&a->ev, 0, sizeof(a->ev));
        a->ev_ctx = NULL;
    }
}

void app_set_transport_hook(App *a, AppTransportFn fn)
{
    if (a != NULL)
        a->transport = fn;
}

int app_set_model(App *a, int model_idx)
{
    if (a == NULL || model_idx < 0 || model_idx >= a->cfg.n_models)
        return -1;
    a->model_idx = model_idx;
    return 0;
}

void app_test_q_push_fail_at(App *a, long n)
{
    if (a != NULL) {
        a->q_push_count = 0;
        a->q_push_fail_at = n;
    }
}

int app_busy(App *a)
{
    return a != NULL && a->busy;
}

void app_abort(App *a)
{
    if (a != NULL && a->busy)
        a->cancel = 1;
}

int app_send(App *a, Session *s, const char *user_text)
{
    Msg *m;
    unsigned long h;
    int old_nm;

    if (a == NULL || s == NULL || user_text == NULL)
        return -1;
    if (a->busy)
        return 1;

    old_nm = s->nm;
    m = session_add_msg(s, ROLE_USER);
    if (m == NULL)
        return -1;
    if (msg_add_block(m, BLOCK_TEXT, user_text) == NULL) {
        session_truncate(s, old_nm);
        return -1;
    }
    if (session_add_msg(s, ROLE_ASSIST) == NULL) {
        session_truncate(s, old_nm);
        return -1;
    }

    a->sess = s;
    a->msg_idx = s->nm - 1;
    a->cancel = 0;
    a->busy = 1;
    if (a->ev.on_busy)
        a->ev.on_busy(a->ev_ctx, 1);

    /* _beginthread (NT CLIB) returns the Win32 thread handle, -1 on error */
    h = _beginthread(worker_main, (unsigned)(64 * 1024), a);
    if (h == (unsigned long)-1) {
        a->busy = 0;
        a->sess = NULL;
        a->msg_idx = -1;
        if (a->ev.on_busy)
            a->ev.on_busy(a->ev_ctx, 0);
        return -1;
    }
    a->thandle = h;
    return 0;
}

int app_recall(App *a, Session *s, int msg_idx, char *err, int errcap)
{
    Session view;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (a == NULL || s == NULL) {
        if (err != NULL && errcap > 0) {
            strncpy(err, "bad argument", (size_t)errcap - 1);
            err[errcap - 1] = '\0';
        }
        return -1;
    }
    if (a->busy) {
        if (err != NULL && errcap > 0) {
            strncpy(err, "send in progress", (size_t)errcap - 1);
            err[errcap - 1] = '\0';
        }
        return -1;
    }
    if (msg_idx < 0 || msg_idx > s->nm) {
        if (err != NULL && errcap > 0) {
            strncpy(err, "msg index out of range", (size_t)errcap - 1);
            err[errcap - 1] = '\0';
        }
        return -1;
    }

    /* Persist the truncated view first; only then touch memory, so a
       disk failure leaves the Session completely unchanged. */
    view = *s;
    view.nm = msg_idx;
    if (history_save(&view, a->cfg.g.history_dir, err, errcap) != 0) {
        if (a->ev.on_error)
            a->ev.on_error(a->ev_ctx, msg_idx,
                           (err != NULL && err[0] != '\0')
                               ? err : "history save failed");
        return -1;
    }
    session_truncate(s, msg_idx);
    return 0;
}
