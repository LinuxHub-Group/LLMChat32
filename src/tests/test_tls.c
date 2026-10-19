/*
 * test_tls.c - TLS wrapper layer tests (tls.c: wolfSSL over NetConn).
 *
 * Console app (built with -bc). Live-network test against www.gstatic.com
 * (override with TEST_TLS_HOST, optionally "host:port"), like
 * test_tls_smoke but exercising the TlsConn wrapper API instead of raw
 * wolfSSL calls:
 *
 *   1: verify=1 - tls_connect succeeds (embedded GTS roots verify the
 *      chain; the ca_file argument is reserved in this NO_FILESYSTEM
 *      build and may name a nonexistent file), err stays empty.
 *      tls_send of "GET /generate_204" followed by tls_recv returns
 *      bytes containing "204"; reading on after the response returns 0
 *      (server close -> clean EOF, no crash).
 *   2: verify=0 - the unverified path handshakes and talks HTTP too.
 *   3: verification failure - TEST_TLS_FAIL_HOST (default
 *      www.baidu.com, whose chain is not signed by the embedded GTS
 *      roots) must fail tls_connect with a non-empty err mentioning the
 *      handshake.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -DWOLFSSL_USER_SETTINGS ^
 *          -i=third_party\wolfssl -i=third_party\wolfssl\ow ^
 *          -fe=tests\test_tls.exe src\tests\test_tls.c ^
 *          src\core\tls.c src\core\net.c ^
 *          build\wolfssl.lib ws2_32.lib advapi32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../core/net.h"
#include "../core/tls.h"

#define DEFAULT_HOST      "www.gstatic.com"
#define DEFAULT_FAIL_HOST "www.baidu.com"
#define HTTPS_PORT        443
#define READ_BUFSZ        16384

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

/* split "host" or "host:port" (TEST_TLS_HOST format) in place */
static void parse_hostport(const char *spec, char *host, int hostcap,
                           unsigned short *port)
{
    const char *sep = strchr(spec, ':');
    unsigned short p = HTTPS_PORT;

    if (sep != NULL) {
        unsigned v = 0;
        const char *q = sep + 1;

        while (*q >= '0' && *q <= '9')
            v = v * 10 + (unsigned)(*q++ - '0');
        if (v > 0 && v <= 65535)
            p = (unsigned short)v;
    }
    strncpy(host, spec, (size_t)hostcap - 1);
    host[hostcap - 1] = '\0';
    if (sep != NULL)
        host[sep - spec] = '\0';
    *port = p;
}

/*
 * One full HTTPS round trip through the wrapper: connect (verify per
 * argument), send "GET /generate_204", read until the status text "204"
 * was seen or the peer closed. Returns 0 when "204" arrived, non-zero
 * otherwise. *eof gets 1 when the read ended by peer close at an TLS
 * layer (tls_recv returned 0) rather than by error.
 */
static int https_204(const char *host, unsigned short port, int verify,
                     int *eof)
{
    NetConn *tcp = NULL;
    TlsConn *tls = NULL;
    static char buf[READ_BUFSZ];
    char err[256];
    char req[384];
    int n, ret;
    int found = 0;
    size_t off = 0;

    *eof = 0;
    if (net_connect(&tcp, host, port, NET_TIMEOUT_CONNECT_MS, NULL, 0,
                    err, (int)sizeof(err)) != 0) {
        printf("FAIL net_connect %s:%u: %s\n", host, (unsigned)port, err);
        g_fail++;
        return -1;
    }
    if (tls_connect(&tls, tcp, host, "certs/cacert.pem", verify,
                    err, (int)sizeof(err)) != 0) {
        printf("FAIL tls_connect(%s, verify=%d): %s\n", host, verify, err);
        g_fail++;
        net_close(tcp);
        return -1;
    }

    n = sprintf(req,
                "GET /generate_204 HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                host);
    ret = tls_send(tls, req, n, NET_TIMEOUT_RW_MS);
    CHECK(ret == 0);

    for (;;) {
        ret = tls_recv(tls, buf + off,
                       (int)(sizeof(buf) - 1 - off), NET_TIMEOUT_RW_MS);
        if (ret > 0) {
            off += (size_t)ret;
            if (strstr(buf, "204") != NULL) {
                found = 1;
                break;
            }
            if (off >= sizeof(buf) - 1) {
                printf("FAIL response buffer full without \"204\"\n");
                g_fail++;
                break;
            }
            continue;
        }
        if (ret == 0)
            *eof = 1;
        break;
    }
    buf[off < sizeof(buf) - 1 ? off : sizeof(buf) - 1] = '\0';
    if (ret <= 0 && !found) {
        printf("FAIL no \"204\" in response (tls_recv returned %d)\n", ret);
        g_fail++;
    }

    /* the response is small; after "204" the server closes. Reading on
     * must terminate with tls_recv returning 0, not crash or error out */
    if (found) {
        int tries;

        for (tries = 0; tries < 8; tries++) {
            ret = tls_recv(tls, buf, (int)sizeof(buf) - 1, NET_TIMEOUT_RW_MS);
            if (ret == 0) {
                *eof = 1;
                break;
            }
            if (ret < 0)
                break;
        }
        CHECK(ret == 0);
    }

    tls_close(tls);
    net_close(tcp);
    return found ? 0 : -1;
}

int main(void)
{
    char host[256];
    char failhost[256];
    unsigned short port, failport;
    const char *spec;
    int eof;

    /* --- configuration ------------------------------------------------- */
    spec = getenv("TEST_TLS_HOST");
    parse_hostport((spec && spec[0]) ? spec : DEFAULT_HOST,
                   host, (int)sizeof(host), &port);
    spec = getenv("TEST_TLS_FAIL_HOST");
    parse_hostport((spec && spec[0]) ? spec : DEFAULT_FAIL_HOST,
                   failhost, (int)sizeof(failhost), &failport);

    /* --- Winsock (tls.c does wolfSSL_Init internally, once) ------------ */
    {
        WSADATA wsa;

        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            printf("FAIL WSAStartup\n");
            return 1;
        }
        printf("ok Winsock %u.%u\n",
               LOBYTE(wsa.wVersion), HIBYTE(wsa.wVersion));
    }

    /* --- scenario 1: verified handshake + HTTP + clean EOF ------------- */
    printf("[test_tls] verify=1 handshake with %s:%u\n", host, (unsigned)port);
    CHECK(https_204(host, port, 1, &eof) == 0);
    CHECK(eof == 1);

    /* --- scenario 2: unverified handshake ------------------------------ */
    printf("[test_tls] verify=0 handshake with %s:%u\n", host, (unsigned)port);
    CHECK(https_204(host, port, 0, &eof) == 0);

    /* --- scenario 3: verification must reject unknown CAs -------------- */
    {
        NetConn *tcp = NULL;
        TlsConn *tls = NULL;
        char err[256];

        printf("[test_tls] verify=1 against %s:%u (expect failure)\n",
               failhost, (unsigned)failport);
        err[0] = '\0';
        if (net_connect(&tcp, failhost, failport, NET_TIMEOUT_CONNECT_MS,
                        NULL, 0, err, (int)sizeof(err)) != 0) {
            printf("FAIL net_connect %s:%u: %s\n",
                   failhost, (unsigned)failport, err);
            g_fail++;
        } else {
            int rc = tls_connect(&tls, tcp, failhost, "certs/cacert.pem",
                                 1, err, (int)sizeof(err));
            CHECK(rc != 0);
            CHECK(tls == NULL);
            CHECK(err[0] != '\0');
            CHECK(strstr(err, "handshake") != NULL);
            if (tls != NULL)
                tls_close(tls);
            net_close(tcp);
        }
    }

    WSACleanup();
    if (g_fail == 0) {
        printf("PASS test_tls\n");
        return 0;
    }
    printf("FAIL test_tls (%d failures)\n", g_fail);
    return 1;
}
