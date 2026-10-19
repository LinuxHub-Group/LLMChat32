/*
 * test_http.c - HTTP/1.1 layer unit tests (request serialization,
 * Content-Length bodies, chunked decoding, byte-at-a-time SSE streams,
 * interim 1xx, LF-only heads, read-till-close, cancel).
 *
 * Console app (built with -bc). Starts a loopback mock server on
 * 127.0.0.1:0 in a helper thread (_beginthread, -bm CLIB); the mock
 * learns its port via getsockname and signals readiness with an event.
 * Each scenario starts a fresh mock with a scripted response:
 *
 *   1: POST /v1/messages - the mock asserts the exact serialized
 *      request (request line, caller headers in order, auto
 *      Content-Length, body), answers 200 with Content-Length: 11 and
 *      sends the body in 3 pieces; the client must reassemble it and
 *      deliver exactly one final callback with last=1.
 *   2: chunked response - hex sizes (uppercase) with a chunk-extension,
 *      a trailer after the 0-chunk, body split over several sends;
 *      trailing CRLF stripped, bytes reassembled across chunk bounds.
 *   3: SSE-shaped chunked body sent byte-at-a-time; the http layer is
 *      byte-transparent (no SSE semantics here - that is Task 11).
 *   4: 404 - a status code is not a transport error: rc==0, err empty,
 *      out->status==404, error body still readable.
 *   5: interim "100 Continue" head followed by a head with no
 *      Content-Length and no chunked -> body read until connection
 *      close.
 *   6: LF-only (bare \n) response head - tolerant header parsing.
 *   7: cancel - the body callback arms *cancel; http_read_body must
 *      abort promptly with -2 and no final last=1 callback.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_http.exe ^
 *          src\tests\test_http.c src\core\http.c src\core\net.c ws2_32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#include "../core/http.h"

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

#define MOCK_BUFCAP 131072

enum { M_SEND, M_CANCEL };

typedef struct {
    HANDLE ready;
    HANDLE hthread;
    int mode;
    const char *script;       /* response bytes to send after the request */
    int script_len;
    int pieces;               /* 1 = single send, >1 = post-head split into
                                 that many sends, 0 = byte-at-a-time */
    const char *expect;       /* when expect_len > 0: exact request bytes;
                                 otherwise: required request-line prefix */
    int expect_len;
    int port;
    int stop;                 /* set by mock_join to unblock accept */
    int result;
    int received_len;
    char received[MOCK_BUFCAP];
    char note[128];
} Mock;

static Mock g_mock;

static void mock_fail(Mock *m, const char *msg)
{
    if (!m->result) {
        m->result = 1;
        strncpy(m->note, msg, sizeof(m->note) - 1);
        m->note[sizeof(m->note) - 1] = '\0';
    }
}

static int mock_fill(Mock *m, const char *data, int n)
{
    if (n < 0 || m->received_len + n > MOCK_BUFCAP - 1)
        return -1;
    memcpy(m->received + m->received_len, data, (size_t)n);
    m->received_len += n;
    m->received[m->received_len] = '\0';
    return 0;
}

/* recv exactly total bytes into m->received */
static int mock_recv_total(SOCKET s, Mock *m, int total)
{
    char buf[1000];
    int got = 0;

    while (got < total) {
        int want = total - got;
        int n;
        if (want > (int)sizeof(buf))
            want = (int)sizeof(buf);
        n = recv(s, buf, want, 0);
        if (n <= 0)
            return -1;
        if (mock_fill(m, buf, n) != 0)
            return -1;
        got += n;
    }
    return 0;
}

/* recv until "\r\n\r\n" has been seen (HTTP head complete) */
static int mock_recv_head(SOCKET s, Mock *m)
{
    char buf[256];

    while (strstr(m->received, "\r\n\r\n") == NULL) {
        int n = recv(s, buf, (int)sizeof(buf), 0);
        if (n <= 0)
            return -1;
        if (mock_fill(m, buf, n) != 0)
            return -1;
    }
    return 0;
}

/* send the scripted response; pieces==1 sends it whole, pieces==0 sends
 * it byte-at-a-time, pieces>1 sends the head first then splits the rest
 * into evenly sized sends */
static int mock_send_script(SOCKET s, const Mock *m)
{
    const char *pp;
    int head_len = m->script_len;
    int i;

    if (m->pieces == 1)
        return send(s, m->script, m->script_len, 0) == m->script_len ? 0 : -1;
    if (m->pieces == 0) {
        for (i = 0; i < m->script_len; i++) {
            if (send(s, m->script + i, 1, 0) != 1)
                return -1;
        }
        return 0;
    }
    pp = strstr(m->script, "\r\n\r\n");
    if (pp != NULL)
        head_len = (int)(pp - m->script) + 4;
    if (head_len > 0 &&
        send(s, m->script, head_len, 0) != head_len)
        return -1;
    for (i = 0; i < m->pieces; i++) {
        int from = head_len
            + (int)((long)(m->script_len - head_len) * i / m->pieces);
        int to = head_len
            + (int)((long)(m->script_len - head_len) * (i + 1) / m->pieces);
        if (to > from && send(s, m->script + from, to - from, 0) != to - from)
            return -1;
    }
    return 0;
}

static void mock_thread(void *p)
{
    Mock *m = (Mock *)p;
    SOCKET ls, as;
    struct sockaddr_in sa;
    int salen = (int)sizeof(sa);

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET) {
        mock_fail(m, "socket");
        SetEvent(m->ready);
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
        listen(ls, 4) != 0 ||
        getsockname(ls, (struct sockaddr *)&sa, &salen) != 0) {
        mock_fail(m, "bind/listen/getsockname");
        closesocket(ls);
        SetEvent(m->ready);
        return;
    }
    m->port = ntohs(sa.sin_port);
    SetEvent(m->ready);

    /* accept with periodic stop checks so mock_join cannot deadlock */
    as = INVALID_SOCKET;
    while (!m->stop) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ls, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        if (select(0, &fds, NULL, NULL, &tv) > 0) {
            as = accept(ls, NULL, NULL);
            break;
        }
    }
    if (as == INVALID_SOCKET) {
        closesocket(ls);
        return;
    }

    /* verify the request the client sent */
    if (m->expect != NULL && m->expect_len > 0) {
        if (mock_recv_total(as, m, m->expect_len) != 0)
            mock_fail(m, "recv request");
        else if (m->received_len != m->expect_len
                 || memcmp(m->received, m->expect, (size_t)m->expect_len) != 0)
            mock_fail(m, "request bytes mismatch");
    } else if (m->expect != NULL) {
        if (mock_recv_head(as, m) != 0)
            mock_fail(m, "recv request head");
        else if (strncmp(m->received, m->expect, strlen(m->expect)) != 0)
            mock_fail(m, "request line mismatch");
    }

    if (m->result == 0 && mock_send_script(as, m) != 0)
        mock_fail(m, "send script");

    if (m->mode == M_CANCEL) {
        /* keep the connection open until the client gives up and closes,
         * so the cancel path (not a server close) ends the read */
        char buf[512];
        for (;;) {
            int n = recv(as, buf, (int)sizeof(buf), 0);
            if (n <= 0)
                break;
        }
    }

    closesocket(as);
    closesocket(ls);
}

static int mock_start(Mock *m)
{
    m->ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (m->ready == NULL)
        return -1;
    m->received_len = 0;
    m->received[0] = '\0';
    m->result = 0;
    m->note[0] = '\0';
    m->port = 0;
    m->stop = 0;
    m->hthread = (HANDLE)_beginthread(mock_thread, 0, (void *)m);
    if (m->hthread == (HANDLE)-1L) {
        CloseHandle(m->ready);
        m->ready = NULL;
        return -1;
    }
    return 0;
}

static int mock_port(Mock *m)
{
    if (WaitForSingleObject(m->ready, 10000) != WAIT_OBJECT_0)
        return -1;
    return m->port;
}

static void mock_join(Mock *m)
{
    m->stop = 1;   /* unblock the accept loop if no client ever came */
    if (m->hthread != NULL) {
        WaitForSingleObject(m->hthread, INFINITE);
        CloseHandle(m->hthread);
        m->hthread = NULL;
    }
    if (m->ready != NULL) {
        CloseHandle(m->ready);
        m->ready = NULL;
    }
}

/* --- body accumulator -------------------------------------------------- */

typedef struct {
    char buf[65536];
    int len;
    int last_count;
    volatile int *arm_cancel;   /* set *arm_cancel on first data chunk */
} BodyAcc;

static void body_fn(void *ctx, const char *data, int len, int last)
{
    BodyAcc *a = (BodyAcc *)ctx;

    if (len > 0) {
        memcpy(a->buf + a->len, data, (size_t)len);
        a->len += len;
    }
    if (last)
        a->last_count++;
    if (a->arm_cancel != NULL && len > 0)
        *a->arm_cancel = 1;
}

/* --- scenarios ---------------------------------------------------------- */

static int connect_loopback(NetConn **out, int port, char *err, int errcap)
{
    return net_connect(out, "127.0.0.1", (unsigned short)port, 3000,
                       NULL, 0, err, errcap);
}

int main(void)
{
    WSADATA wd;
    int port;

    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        printf("FAIL WSAStartup\n");
        return 1;
    }

    /* --- 1: POST request serialization + Content-Length body in 3 sends -- */
    {
        static const char expect[] =
            "POST /v1/messages HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Authorization: Bearer key123\r\n"
            "Content-Length: 7\r\n"
            "\r\n"
            "{\"x\":1}";
        static const char script[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello world";
        const char *hdrs[3];
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        hdrs[0] = "Host: example.com";
        hdrs[1] = "Authorization: Bearer key123";
        hdrs[2] = NULL;

        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 3;      /* head, then body in 3 sends */
        g_mock.expect = expect;
        g_mock.expect_len = (int)strlen(expect);
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "POST", "/v1/messages", hdrs,
                                    "{\"x\":1}", 7, &res, err,
                                    (int)sizeof(err)) == 0);
            CHECK(res.status == 200);
            CHECK(res.chunked == 0);
            CHECK(res.content_length == 11);
            CHECK(strcmp(res.content_type, "application/json") == 0);
            CHECK(err[0] == '\0');
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == 11
                  && memcmp(acc.buf, "hello world", 11) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 2: chunked response: hex sizes, extension, trailer, split sends - */
    {
        static const char script[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "1A;foo=bar\r\n"
            "abcdefghijklmnopqrstuvwxyz\r\n"
            "b\r\n"
            "hello world\r\n"
            "0\r\n"
            "X-Trailer: yes\r\n"
            "\r\n";
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 4;
        g_mock.expect = "GET /stream HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/stream", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(res.status == 200);
            CHECK(res.chunked == 1);
            CHECK(res.content_length == -1);
            CHECK(strcmp(res.content_type, "text/plain") == 0);
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == 37
                  && memcmp(acc.buf,
                            "abcdefghijklmnopqrstuvwxyzhello world", 37) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 3: SSE-shaped body, byte-at-a-time (chunk boundaries anywhere) -- */
    {
        static const char sse_body[] =
            "data: {\"a\":1}\r\n\r\ndata: [DONE]\r\n\r\n";
        char script[512];
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        snprintf(script, sizeof(script),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/event-stream\r\n"
                 "Transfer-Encoding: chunked\r\n"
                 "\r\n"
                 "%x\r\n"
                 "%s"
                 "\r\n"
                 "0\r\n"
                 "\r\n",
                 (unsigned)strlen(sse_body), sse_body);
        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 0;      /* every byte its own send */
        g_mock.expect = "GET /events HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/events", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(res.chunked == 1);
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == (int)strlen(sse_body)
                  && memcmp(acc.buf, sse_body, (size_t)acc.len) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 4: 404 is not a transport error; error body still readable ------ */
    {
        static const char script[] =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "not found";
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 1;
        g_mock.expect = "GET /missing HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/missing", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(res.status == 404);
            CHECK(err[0] == '\0');
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == 9
                  && memcmp(acc.buf, "not found", 9) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 5: interim 100 head, then no Content-Length -> read till close -- */
    {
        static const char script[] =
            "HTTP/1.1 100 Continue\r\n"
            "\r\n"
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "\r\n"
            "streamed body without length";
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 1;
        g_mock.expect = "GET /raw HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/raw", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(res.status == 200);
            CHECK(res.content_length == -1);
            CHECK(res.chunked == 0);
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == 28
                  && memcmp(acc.buf, "streamed body without length", 28) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 6: LF-only response head ---------------------------------------- */
    {
        static const char script[] =
            "HTTP/1.1 200 OK\n"
            "Content-Type: text/plain\n"
            "Content-Length: 5\n"
            "\n"
            "hi yo";
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        g_mock.mode = M_SEND;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 1;
        g_mock.expect = "GET /lf HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/lf", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(res.status == 200);
            CHECK(strcmp(res.content_type, "text/plain") == 0);
            CHECK(res.content_length == 5);
            CHECK(http_read_body(c, &res, NULL, body_fn, &acc, err,
                                 (int)sizeof(err)) == 0);
            CHECK(acc.len == 5 && memcmp(acc.buf, "hi yo", 5) == 0);
            CHECK(acc.last_count == 1);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 7: cancel -> http_read_body returns -2, no final callback ------- */
    {
        static const char script[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 100\r\n"
            "\r\n"
            "0123456789";       /* only 10 of 100 bytes, then silence */
        volatile int cancel = 0;
        NetConn *c;
        HttpResult res;
        static BodyAcc acc;
        char err[256];

        memset(&acc, 0, sizeof(acc));
        acc.arm_cancel = &cancel;
        g_mock.mode = M_CANCEL;
        g_mock.script = script;
        g_mock.script_len = (int)strlen(script);
        g_mock.pieces = 1;
        g_mock.expect = "GET /slow HTTP/1.1\r\n";
        g_mock.expect_len = 0;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(connect_loopback(&c, port, err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            CHECK(http_send_request(c, "GET", "/slow", NULL, NULL, 0,
                                    &res, err, (int)sizeof(err)) == 0);
            CHECK(http_read_body(c, &res, &cancel, body_fn, &acc, err,
                                 (int)sizeof(err)) == -2);
            CHECK(cancel != 0);
            CHECK(acc.len == 10 && memcmp(acc.buf, "0123456789", 10) == 0);
            CHECK(acc.last_count == 0);
            net_close(c);
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    WSACleanup();
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d checks failed)\n", g_fail);
    return 1;
}
