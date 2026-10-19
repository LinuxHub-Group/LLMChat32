/*
 * test_net.c - net connection layer unit tests (DNS/connect/timeouts/
 * proxy CONNECT/URL parsing).
 *
 * Console app (built with -bc). Starts a loopback mock server on
 * 127.0.0.1:0 in a helper thread (_beginthread, -bm CLIB); the mock
 * learns its port via getsockname and signals readiness with an event.
 * Each scenario starts a fresh mock with a scripted role:
 *
 *   M_RECV_SEND_CLOSE - recv N bytes, reply "pong", close (covers
 *       net_connect via "localhost" DNS, net_send, net_recv, and that
 *       a peer close makes net_recv return 0)
 *   M_ECHO_BIG - recv 100000 bytes in small chunks, reply "K", drain
 *       until close (covers net_send looping over partial writes and
 *       the peer reassembling the full stream)
 *   M_SILENT - accept then sleep 3 s (covers net_recv timeout -> -1)
 *   M_PROXY / M_PROXY_AUTH - play an HTTP proxy: assert the request
 *       line is "CONNECT example.com:443 HTTP/1.1" (auth variant also
 *       asserts "Proxy-Authorization: Basic dXNlcjpwYXNz"), answer
 *       "HTTP/1.1 200 OK\r\n\r\n", then echo the tunnel until close
 *   M_PROXY_407 - answer 407 so net_connect must fail with a readable
 *       err (CONNECT success is 2xx only)
 *
 * Also: connecting to an unlistened port fails with a readable err;
 * use_proxy with an empty proxy host connects directly; net_parse_url
 * checks per the task brief (https -> 443/tls, http -> 80, unescaped
 * space in the path is an error).
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_net.exe ^
 *          src\tests\test_net.c src\core\net.c ws2_32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#include "../core/net.h"

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

enum {
    M_RECV_SEND_CLOSE, M_ECHO_BIG, M_SILENT,
    M_PROXY, M_PROXY_AUTH, M_PROXY_407
};

typedef struct {
    HANDLE ready;
    HANDLE hthread;
    int mode;
    int arg;
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

/* recv exactly total bytes, in small chunks, into m->received */
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

    switch (m->mode) {
    case M_RECV_SEND_CLOSE:
        if (mock_recv_total(as, m, m->arg) != 0)
            mock_fail(m, "recv payload");
        else if (send(as, "pong", 4, 0) != 4)
            mock_fail(m, "send pong");
        break;
    case M_ECHO_BIG: {
        char k = 'K';
        char buf[512];
        if (mock_recv_total(as, m, m->arg) != 0) {
            mock_fail(m, "recv big");
            break;
        }
        if (send(as, &k, 1, 0) != 1) {
            mock_fail(m, "send K");
            break;
        }
        for (;;) {
            int n = recv(as, buf, (int)sizeof(buf), 0);
            if (n <= 0)
                break;
            if (mock_fill(m, buf, n) != 0) {
                mock_fail(m, "overflow");
                break;
            }
        }
        break;
    }
    case M_SILENT:
        Sleep((DWORD)m->arg);
        break;
    case M_PROXY:
    case M_PROXY_AUTH: {
        static const char ok_reply[] = "HTTP/1.1 200 OK\r\n\r\n";
        const char *connect_line = "CONNECT example.com:443 HTTP/1.1\r\n";
        char buf[64];
        if (mock_recv_head(as, m) != 0) {
            mock_fail(m, "read CONNECT");
            break;
        }
        if (strncmp(m->received, connect_line, strlen(connect_line)) != 0)
            mock_fail(m, "CONNECT request line");
        if (m->mode == M_PROXY_AUTH) {
            if (strstr(m->received,
                       "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n") == NULL)
                mock_fail(m, "missing Proxy-Authorization");
        } else if (strstr(m->received, "Proxy-Authorization:") != NULL) {
            mock_fail(m, "unexpected Proxy-Authorization");
        }
        if (send(as, ok_reply, (int)strlen(ok_reply), 0)
            != (int)strlen(ok_reply)) {
            mock_fail(m, "send 200");
            break;
        }
        for (;;) {
            int n = recv(as, buf, (int)sizeof(buf), 0);
            if (n <= 0)
                break;
            if (send(as, buf, n, 0) != n)
                break;
        }
        break;
    }
    case M_PROXY_407: {
        static const char no_reply[] =
            "HTTP/1.1 407 Proxy Authentication Required\r\n\r\n";
        if (mock_recv_head(as, m) != 0) {
            mock_fail(m, "read CONNECT");
            break;
        }
        if (send(as, no_reply, (int)strlen(no_reply), 0)
            != (int)strlen(no_reply))
            mock_fail(m, "send 407");
        break;
    }
    default:
        mock_fail(m, "bad mode");
        break;
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

/* Bind port 0, learn the port, release it: a port nothing listens on. */
static int reserve_dead_port(void)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    int salen = (int)sizeof(sa);
    int port = -1;

    if (s == INVALID_SOCKET)
        return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) == 0 &&
        getsockname(s, (struct sockaddr *)&sa, &salen) == 0)
        port = ntohs(sa.sin_port);
    closesocket(s);
    return port;
}

static void test_parse_url(void)
{
    char host[128];
    char path[256];
    unsigned short port = 0;
    int tls = -1;
    int rc;

    rc = net_parse_url("https://api.anthropic.com/v1/messages",
                       host, (int)sizeof(host), &port,
                       path, (int)sizeof(path), &tls);
    CHECK(rc == 0);
    CHECK(strcmp(host, "api.anthropic.com") == 0);
    CHECK(port == 443);
    CHECK(tls == 1);
    CHECK(strcmp(path, "/v1/messages") == 0);

    tls = -1;
    port = 0;
    rc = net_parse_url("http://example.com", host, (int)sizeof(host), &port,
                       path, (int)sizeof(path), &tls);
    CHECK(rc == 0);
    CHECK(port == 80);
    CHECK(tls == 0);
    CHECK(strcmp(path, "/") == 0);

    tls = -1;
    port = 0;
    rc = net_parse_url("http://example.com:8080/x?y=1",
                       host, (int)sizeof(host), &port,
                       path, (int)sizeof(path), &tls);
    CHECK(rc == 0);
    CHECK(port == 8080);
    CHECK(tls == 0);
    CHECK(strcmp(path, "/x?y=1") == 0);

    /* unescaped space in the path must be rejected */
    CHECK(net_parse_url("http://h:8080/a b", host, (int)sizeof(host), &port,
                        path, (int)sizeof(path), &tls) != 0);
    /* empty host */
    CHECK(net_parse_url("http://:80/x", host, (int)sizeof(host), &port,
                        path, (int)sizeof(path), &tls) != 0);
    /* unknown scheme */
    CHECK(net_parse_url("ftp://h/x", host, (int)sizeof(host), &port,
                        path, (int)sizeof(path), &tls) != 0);
    /* port out of range */
    CHECK(net_parse_url("http://h:99999/x", host, (int)sizeof(host), &port,
                        path, (int)sizeof(path), &tls) != 0);
    /* buffer too small for host / path */
    CHECK(net_parse_url("https://longhostname/x", host, 4, &port,
                        path, (int)sizeof(path), &tls) != 0);
    CHECK(net_parse_url("https://h/lengthypath", host, (int)sizeof(host),
                        &port, path, 4, &tls) != 0);
}

int main(void)
{
    WSADATA wd;
    NetConn *c;
    ProxyCfg px;
    int port;

    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        printf("FAIL WSAStartup\n");
        return 1;
    }

    /* --- 1: DNS ("localhost"), connect, send, recv, peer close -> 0 --- */
    g_mock.mode = M_RECV_SEND_CLOSE;
    g_mock.arg = 4;
    CHECK(mock_start(&g_mock) == 0);
    port = mock_port(&g_mock);
    CHECK(port > 0);
    if (port > 0) {
        char err[256];
        c = NULL;
        err[0] = '\0';
        CHECK(net_connect(&c, "localhost", (unsigned short)port, 3000,
                          NULL, 0, err, (int)sizeof(err)) == 0);
        CHECK(c != NULL);
        if (c != NULL) {
            char buf[16];
            int n;
            CHECK(net_send(c, "ping", 4, 3000) == 0);
            n = net_recv(c, buf, (int)sizeof(buf), 3000);
            CHECK(n == 4 && memcmp(buf, "pong", 4) == 0);
            n = net_recv(c, buf, (int)sizeof(buf), 3000);
            CHECK(n == 0);
            net_close(c);
        }
    }
    mock_join(&g_mock);
    CHECK(g_mock.result == 0);
    CHECK(g_mock.received_len == 4
          && memcmp(g_mock.received, "ping", 4) == 0);

    /* --- 2: connection refused -> non-zero, readable err --- */
    {
        char err[256];
        port = reserve_dead_port();
        CHECK(port > 0);
        c = NULL;
        err[0] = '\0';
        if (port > 0)
            CHECK(net_connect(&c, "127.0.0.1", (unsigned short)port, 3000,
                              NULL, 0, err, (int)sizeof(err)) != 0);
        CHECK(c == NULL);
        CHECK(err[0] != '\0');
        if (c != NULL)
            net_close(c);
    }

    /* --- 3: 100000-byte send reassembled from small recvs --- */
    {
        static char payload[100000];
        char err[256];
        int i;
        for (i = 0; i < 100000; i++)
            payload[i] = (char)(i * 7 + 1);
        g_mock.mode = M_ECHO_BIG;
        g_mock.arg = 100000;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        if (port > 0) {
            c = NULL;
            CHECK(net_connect(&c, "127.0.0.1", (unsigned short)port, 3000,
                              NULL, 0, err, (int)sizeof(err)) == 0);
            if (c != NULL) {
                char k = 0;
                int n;
                CHECK(net_send(c, payload, 100000, 5000) == 0);
                n = net_recv(c, &k, 1, 5000);
                CHECK(n == 1 && k == 'K');
                net_close(c);
            }
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
        CHECK(g_mock.received_len == 100000);
        CHECK(memcmp(g_mock.received, payload, 100000) == 0);
    }

    /* --- 4: peer silent for 3 s -> net_recv(500 ms) == -1, fast --- */
    {
        char err[256];
        g_mock.mode = M_SILENT;
        g_mock.arg = 3000;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        if (port > 0) {
            c = NULL;
            CHECK(net_connect(&c, "127.0.0.1", (unsigned short)port, 3000,
                              NULL, 0, err, (int)sizeof(err)) == 0);
            if (c != NULL) {
                char buf[8];
                DWORD t0 = GetTickCount();
                DWORD dt;
                int n = net_recv(c, buf, (int)sizeof(buf), 500);
                dt = GetTickCount() - t0;
                CHECK(n == -1);
                CHECK(dt < 2500);
                net_close(c);
            }
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
    }

    /* --- 5: proxy CONNECT, no auth --- */
    memset(&px, 0, sizeof(px));
    strcpy(px.host, "127.0.0.1");
    g_mock.mode = M_PROXY;
    g_mock.arg = 0;
    CHECK(mock_start(&g_mock) == 0);
    px.port = mock_port(&g_mock);
    CHECK(px.port > 0);
    if (px.port > 0) {
        char err[256];
        c = NULL;
        err[0] = '\0';
        CHECK(net_connect(&c, "example.com", 443, 3000, &px, 1,
                          err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            char buf[16];
            int n;
            CHECK(net_send(c, "hello", 5, 3000) == 0);
            n = net_recv(c, buf, (int)sizeof(buf), 3000);
            CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);
            net_close(c);
        }
    }
    mock_join(&g_mock);
    CHECK(g_mock.result == 0);

    /* --- 6: proxy CONNECT with Basic auth --- */
    strcpy(px.username, "user");
    strcpy(px.password, "pass");
    g_mock.mode = M_PROXY_AUTH;
    g_mock.arg = 0;
    CHECK(mock_start(&g_mock) == 0);
    px.port = mock_port(&g_mock);
    CHECK(px.port > 0);
    if (px.port > 0) {
        char err[256];
        c = NULL;
        err[0] = '\0';
        CHECK(net_connect(&c, "example.com", 443, 3000, &px, 1,
                          err, (int)sizeof(err)) == 0);
        if (c != NULL) {
            char buf[16];
            int n;
            CHECK(net_send(c, "hello", 5, 3000) == 0);
            n = net_recv(c, buf, (int)sizeof(buf), 3000);
            CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);
            net_close(c);
        }
    }
    mock_join(&g_mock);
    CHECK(g_mock.result == 0);

    /* --- 7: proxy answers 407 -> net_connect fails, readable err --- */
    g_mock.mode = M_PROXY_407;
    g_mock.arg = 0;
    CHECK(mock_start(&g_mock) == 0);
    px.port = mock_port(&g_mock);
    CHECK(px.port > 0);
    if (px.port > 0) {
        char err[256];
        c = NULL;
        err[0] = '\0';
        CHECK(net_connect(&c, "example.com", 443, 3000, &px, 1,
                          err, (int)sizeof(err)) != 0);
        CHECK(c == NULL);
        CHECK(err[0] != '\0');
        if (c != NULL)
            net_close(c);
    }
    mock_join(&g_mock);
    CHECK(g_mock.result == 0);

    /* --- 8: use_proxy set but proxy host empty -> direct connect --- */
    {
        ProxyCfg pxempty;
        char err[256];
        memset(&pxempty, 0, sizeof(pxempty));
        g_mock.mode = M_RECV_SEND_CLOSE;
        g_mock.arg = 2;
        CHECK(mock_start(&g_mock) == 0);
        port = mock_port(&g_mock);
        CHECK(port > 0);
        if (port > 0) {
            c = NULL;
            CHECK(net_connect(&c, "127.0.0.1", (unsigned short)port, 3000,
                              &pxempty, 1, err, (int)sizeof(err)) == 0);
            if (c != NULL) {
                CHECK(net_send(c, "hi", 2, 3000) == 0);
                net_close(c);
            }
        }
        mock_join(&g_mock);
        CHECK(g_mock.result == 0);
        CHECK(g_mock.received_len == 2
              && memcmp(g_mock.received, "hi", 2) == 0);
    }

    /* --- 9: URL parsing --- */
    test_parse_url();

    WSACleanup();
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d checks failed)\n", g_fail);
    return 1;
}
