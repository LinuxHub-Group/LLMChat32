/*
 * test_tls_smoke.c - TLS handshake smoke test for the wolfSSL static lib.
 *
 * Windows console app (built with -bc). Connects to www.gstatic.com:443
 * (override with TEST_TLS_HOST, optionally "host:port"), performs a real
 * TLS handshake with certificate verification (GTS roots from memory -
 * the library is built with NO_FILESYSTEM), sends
 * "GET /generate_204 HTTP/1.1" and asserts the response contains "204".
 *
 * Prints one "ok ..." line per step and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -DWOLFSSL_USER_SETTINGS ^
 *          -i=third_party\wolfssl -i=third_party\wolfssl\ow ^
 *          -fe=tests\test_tls_smoke.exe src\tests\test_tls_smoke.c ^
 *          build\wolfssl.lib ws2_32.lib advapi32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>

/* wolfSSL headers must come after winsock2.h (USE_WINDOWS_API pulls in
 * winsock2.h itself; including it first keeps the winsock version straight).
 * settings.h picks up user_settings.h via -DWOLFSSL_USER_SETTINGS. */
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include "certs_google.h"

#define DEFAULT_HOST "www.gstatic.com"
#define HTTPS_PORT   443
#define IO_TIMEOUT_MS 15000
#define READ_BUFSZ   16384

static SOCKET     g_sock  = INVALID_SOCKET;
static WOLFSSL_CTX *g_ctx  = NULL;
static WOLFSSL    *g_ssl  = NULL;
static int        g_wsa_started;

static void cleanup(void)
{
    if (g_ssl != NULL) {
        wolfSSL_shutdown(g_ssl);
        wolfSSL_free(g_ssl);
        g_ssl = NULL;
    }
    if (g_ctx != NULL) {
        wolfSSL_CTX_free(g_ctx);
        g_ctx = NULL;
    }
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    if (g_wsa_started) {
        WSACleanup();
        g_wsa_started = 0;
    }
    wolfSSL_Cleanup();
}

static int fail(const char *what)
{
    fprintf(stderr, "[test_tls_smoke] FAIL: %s\n", what);
    cleanup();
    return 1;
}

static int fail_wolfssl(const char *what, WOLFSSL *ssl, int ret)
{
    int err = wolfSSL_get_error(ssl, ret);
    fprintf(stderr, "[test_tls_smoke] FAIL: %s (wolfSSL ret=%d err=%d)\n",
            what, ret, err);
    cleanup();
    return 1;
}

/*
 * Connect a blocking TCP socket to host:port. Returns 0 on success.
 */
static int tcp_connect(const char *host, unsigned short port)
{
    struct hostent *he;
    struct sockaddr_in sin;
    int timeout = IO_TIMEOUT_MS;
    int rc;

    he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);
    memcpy(&sin.sin_addr, he->h_addr_list[0],
           sizeof(sin.sin_addr) < (size_t)he->h_length ?
           sizeof(sin.sin_addr) : (size_t)he->h_length);

    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET)
        return -1;

    (void)setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO,
                     (const char *)&timeout, sizeof(timeout));
    (void)setsockopt(g_sock, SOL_SOCKET, SO_SNDTIMEO,
                     (const char *)&timeout, sizeof(timeout));

    rc = connect(g_sock, (struct sockaddr *)&sin, sizeof(sin));
    if (rc != 0) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return -1;
    }
    return 0;
}

int main(void)
{
    static char readbuf[READ_BUFSZ];
    const char *host;
    const char *port_sep;
    char hostbuf[256];
    unsigned short port = HTTPS_PORT;
    char request[384];
    WOLFSSL_METHOD *method;
    long loaded;
    size_t off;
    int ret;
    int n;
    WSADATA wsa;

    /* --- configuration ------------------------------------------------- */
    host = getenv("TEST_TLS_HOST");
    if (host == NULL || host[0] == '\0')
        host = DEFAULT_HOST;

    strncpy(hostbuf, host, sizeof(hostbuf) - 1);
    hostbuf[sizeof(hostbuf) - 1] = '\0';
    port_sep = strchr(hostbuf, ':');
    if (port_sep != NULL) {
        port = (unsigned short)atoi(port_sep + 1);
        if (port == 0)
            port = HTTPS_PORT;
        hostbuf[port_sep - hostbuf] = '\0';
    }
    host = hostbuf;

    /* --- Winsock ------------------------------------------------------- */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return fail("WSAStartup");
    g_wsa_started = 1;
    printf("[test_tls_smoke] ok: Winsock %u.%u\n",
           LOBYTE(wsa.wVersion), HIBYTE(wsa.wVersion));

    /* --- wolfSSL ------------------------------------------------------- */
    if (wolfSSL_Init() != WOLFSSL_SUCCESS)
        return fail("wolfSSL_Init");
    printf("[test_tls_smoke] ok: wolfSSL_Init\n");

    method = wolfSSLv23_client_method();
    {
        const char *vers = getenv("TEST_TLS_VERSION");
        if (vers != NULL && strcmp(vers, "1.2") == 0)
            method = wolfTLSv1_2_client_method();
        else if (vers != NULL && strcmp(vers, "1.3") == 0)
            method = wolfTLSv1_3_client_method();
    }
    if (method == NULL)
        return fail("client method selection");
    g_ctx = wolfSSL_CTX_new(method);
    if (g_ctx == NULL)
        return fail("wolfSSL_CTX_new");

    /* Verify the peer certificate (incl. validity dates). The roots are
     * compiled in because the library uses NO_FILESYSTEM. */
    wolfSSL_CTX_set_verify(g_ctx, WOLFSSL_VERIFY_PEER, NULL);
    loaded = wolfSSL_CTX_load_verify_buffer(g_ctx,
                (const unsigned char *)gts_root_r4_pem,
                (long)strlen(gts_root_r4_pem), WOLFSSL_FILETYPE_PEM);
    if (loaded != WOLFSSL_SUCCESS)
        return fail("load GTS Root R4");
    loaded = wolfSSL_CTX_load_verify_buffer(g_ctx,
                (const unsigned char *)gts_root_r1_pem,
                (long)strlen(gts_root_r1_pem), WOLFSSL_FILETYPE_PEM);
    if (loaded != WOLFSSL_SUCCESS)
        return fail("load GTS Root R1");
    printf("[test_tls_smoke] ok: verify roots loaded\n");

    /* --- TCP ----------------------------------------------------------- */
    if (tcp_connect(host, port) != 0)
        return fail("TCP connect");
    printf("[test_tls_smoke] ok: TCP connected to %s:%u\n", host, port);

    /* --- TLS ----------------------------------------------------------- */
    g_ssl = wolfSSL_new(g_ctx);
    if (g_ssl == NULL)
        return fail("wolfSSL_new");

    ret = wolfSSL_UseSNI(g_ssl, WOLFSSL_SNI_HOST_NAME,
                         host, (word16)strlen(host));
    if (ret != WOLFSSL_SUCCESS)
        return fail("wolfSSL_UseSNI");

    ret = wolfSSL_set_fd(g_ssl, (int)g_sock);
    if (ret != WOLFSSL_SUCCESS)
        return fail("wolfSSL_set_fd");

    /* no-op unless the library was built with DEBUG_WOLFSSL */
    wolfSSL_Debugging_ON();

    ret = wolfSSL_connect(g_ssl);
    if (ret != WOLFSSL_SUCCESS)
        return fail_wolfssl("TLS handshake", g_ssl, ret);
    printf("[test_tls_smoke] ok: TLS handshake (%s)\n",
           wolfSSL_get_version(g_ssl));

    /* --- HTTP ---------------------------------------------------------- */
    n = sprintf(request,
                "GET /generate_204 HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                host);
    if (n <= 0 || (size_t)n >= sizeof(request))
        return fail("build GET request");

    ret = wolfSSL_write(g_ssl, request, n);
    if (ret != n)
        return fail_wolfssl("wolfSSL_write GET", g_ssl, ret);

    off = 0;
    for (;;) {
        ret = wolfSSL_read(g_ssl, readbuf + off,
                           (int)(sizeof(readbuf) - 1 - off));
        if (ret > 0) {
            off += (size_t)ret;
            if (off >= sizeof(readbuf) - 1)
                break;
            continue;
        }
        /* ret <= 0: close_notify or error - the status line we need
         * arrives in the first flight, so either ends the read. */
        break;
    }
    readbuf[off] = '\0';

    if (strstr(readbuf, "204") == NULL) {
        fprintf(stderr, "[test_tls_smoke] FAIL: no \"204\" in response:\n%.*s\n",
                (int)(off < 400 ? off : 400), readbuf);
        cleanup();
        return 1;
    }
    printf("[test_tls_smoke] ok: GET /generate_204 -> HTTP 204\n");

    printf("[test_tls_smoke] PASS\n");
    cleanup();
    return 0;
}
