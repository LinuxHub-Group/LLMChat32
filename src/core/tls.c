/*
 * tls.c - TLS connection layer (see tls.h for the contract).
 *
 * wolfSSL is driven through custom IO callbacks (wolfSSL_CTX_SetIORecv /
 * wolfSSL_CTX_SetIOSend) whose context is the TlsConn; they forward to
 * net_recv/net_send, so blocking semantics and timeouts stay exactly
 * those of the net layer. Callback return values follow the
 * WOLFSSL_CBIO_ERR_* convention: a successful read/write returns the
 * byte count, peer close maps to WOLFSSL_CBIO_ERR_CONN_CLOSE, and a
 * timeout/socket error maps to WOLFSSL_CBIO_ERR_TIMEOUT /
 * WOLFSSL_CBIO_ERR_GENERAL (fatal for TLS, surfacing as -1 from
 * tls_recv/tls_send just like net_recv/net_send).
 *
 * Trust anchors come from certs_roots.h because the library is built
 * with NO_FILESYSTEM: the ca_file parameter of tls_connect is reserved
 * for a future file-loading build and is intentionally unused.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <stdio.h>      /* sprintf, vsnprintf */
#include <stdlib.h>     /* malloc, free */
#include <string.h>
#include <stdarg.h>     /* va_list for set_errf */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

/* wolfSSL headers must come after winsock2.h (USE_WINDOWS_API pulls in
 * winsock2.h itself; including it first keeps the winsock version straight).
 * settings.h picks up user_settings.h via -DWOLFSSL_USER_SETTINGS. ssl.h
 * also pulls in error-ssl.h (WOLFSSL_CBIO_ERR_*). */
#include <wolfssl/ssl.h>

#include "tls.h"
#include "certs_roots.h"

struct TlsConn {
    WOLFSSL_CTX *ctx;
    WOLFSSL     *ssl;
    NetConn     *tcp;          /* borrowed, never closed here */
    int         timeout_ms;    /* active op timeout for the IO callbacks */
};

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
}

/* printf-style variant of set_err: formats into err honoring errcap,
 * so messages with embedded host names can never overrun the buffer */
static void set_errf(char *err, int errcap, const char *fmt, ...)
{
    if (err && errcap > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, (size_t)errcap, fmt, ap);
        err[errcap - 1] = '\0';
        va_end(ap);
    }
}

/* --- wolfSSL IO callbacks ------------------------------------------------ */

/* recv: >0 byte count, 0 -> peer closed, <0 -> timeout/error.
 * net_recv already has exactly that contract, so the mapping is direct. */
static int tls_io_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    TlsConn *c = (TlsConn *)ctx;
    int r;

    (void)ssl;
    if (c == NULL || c->tcp == NULL)
        return WOLFSSL_CBIO_ERR_GENERAL;
    r = net_recv(c->tcp, buf, sz, c->timeout_ms);
    if (r > 0)
        return r;
    if (r == 0)
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return WOLFSSL_CBIO_ERR_TIMEOUT;
}

/* send: net_send loops until the whole buffer is written, so a 0 return
 * means all sz bytes went out and anything else is an error */
static int tls_io_send(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    TlsConn *c = (TlsConn *)ctx;

    (void)ssl;
    if (c == NULL || c->tcp == NULL)
        return WOLFSSL_CBIO_ERR_GENERAL;
    if (net_send(c->tcp, buf, sz, c->timeout_ms) == 0)
        return sz;
    return WOLFSSL_CBIO_ERR_GENERAL;
}

/* wolfSSL_Init once per process; tls_close never balances it with
 * wolfSSL_Cleanup (the app may hold other TLS objects) */
static int tls_ensure_init(void)
{
    static int started;

    if (!started) {
        if (wolfSSL_Init() != WOLFSSL_SUCCESS)
            return -1;
        started = 1;
    }
    return 0;
}

/* free everything except the borrowed NetConn */
static void tls_free(TlsConn *c)
{
    if (c != NULL) {
        if (c->ssl != NULL)
            wolfSSL_free(c->ssl);
        if (c->ctx != NULL)
            wolfSSL_CTX_free(c->ctx);
        free(c);
    }
}

/* --- public API ------------------------------------------------------------ */

int tls_connect(TlsConn **out, NetConn *tcp, const char *sni_host,
                const char *ca_file, int verify, char *err, int errcap)
{
    TlsConn *c;
    int ret;

    (void)ca_file;   /* reserved: roots are compiled in (NO_FILESYSTEM) */
    if (out)
        *out = NULL;
    if (err && errcap > 0)
        err[0] = '\0';
    if (out == NULL || tcp == NULL) {
        set_err(err, errcap, "bad arguments to tls_connect");
        return -1;
    }
    if (tls_ensure_init() != 0) {
        set_err(err, errcap, "wolfSSL_Init failed");
        return -1;
    }

    c = (TlsConn *)malloc(sizeof(*c));
    if (c == NULL) {
        set_err(err, errcap, "out of memory");
        return -1;
    }
    c->ctx = NULL;
    c->ssl = NULL;
    c->tcp = tcp;
    c->timeout_ms = NET_TIMEOUT_RW_MS;   /* handshake IO timeout */

    c->ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
    if (c->ctx == NULL) {
        set_err(err, errcap, "wolfSSL_CTX_new failed");
        tls_free(c);
        return -1;
    }

    /* trust anchors: embedded GTS roots (see certs_roots.h) */
    if (verify) {
        wolfSSL_CTX_set_verify(c->ctx, WOLFSSL_VERIFY_PEER, NULL);
        ret = wolfSSL_CTX_load_verify_buffer(c->ctx,
                    (const unsigned char *)gts_root_r4_pem,
                    (long)strlen(gts_root_r4_pem), WOLFSSL_FILETYPE_PEM);
        if (ret == WOLFSSL_SUCCESS)
            ret = wolfSSL_CTX_load_verify_buffer(c->ctx,
                    (const unsigned char *)gts_root_r1_pem,
                    (long)strlen(gts_root_r1_pem), WOLFSSL_FILETYPE_PEM);
        if (ret != WOLFSSL_SUCCESS) {
            set_err(err, errcap, "loading embedded CA roots failed");
            tls_free(c);
            return -1;
        }
    } else {
        wolfSSL_CTX_set_verify(c->ctx, WOLFSSL_VERIFY_NONE, NULL);
    }

    /* transport: our callbacks over the borrowed NetConn */
    wolfSSL_CTX_SetIORecv(c->ctx, tls_io_recv);
    wolfSSL_CTX_SetIOSend(c->ctx, tls_io_send);

    c->ssl = wolfSSL_new(c->ctx);
    if (c->ssl == NULL) {
        set_err(err, errcap, "wolfSSL_new failed");
        tls_free(c);
        return -1;
    }
    wolfSSL_SetIOReadCtx(c->ssl, c);
    wolfSSL_SetIOWriteCtx(c->ssl, c);

    if (sni_host != NULL && sni_host[0] != '\0') {
        ret = wolfSSL_UseSNI(c->ssl, WOLFSSL_SNI_HOST_NAME,
                             sni_host, (word16)strlen(sni_host));
        if (ret != WOLFSSL_SUCCESS) {
            set_err(err, errcap, "wolfSSL_UseSNI failed");
            tls_free(c);
            return -1;
        }
        if (verify) {
            /* name check on top of chain/date verification */
            ret = wolfSSL_check_domain_name(c->ssl, sni_host);
            if (ret != WOLFSSL_SUCCESS) {
                set_err(err, errcap, "bad SNI host name for verification");
                tls_free(c);
                return -1;
            }
        }
    }

    ret = wolfSSL_connect(c->ssl);
    if (ret != WOLFSSL_SUCCESS) {
        int e = wolfSSL_get_error(c->ssl, ret);
        char msg[WOLFSSL_MAX_ERROR_SZ];

        wolfSSL_ERR_error_string((unsigned long)e, msg);
        set_errf(err, errcap, "TLS handshake failed: %s (code %d)", msg, e);
        tls_free(c);
        return -1;
    }

    *out = c;
    return 0;
}

int tls_send(TlsConn *c, const void *data, int len, int timeout_ms)
{
    int ret;

    if (c == NULL || data == NULL || len < 0)
        return -1;
    c->timeout_ms = timeout_ms;
    ret = wolfSSL_write(c->ssl, data, len);
    return (ret == len) ? 0 : -1;
}

int tls_recv(TlsConn *c, void *buf, int cap, int timeout_ms)
{
    int ret;
    int e;

    if (c == NULL || buf == NULL || cap <= 0)
        return -1;
    c->timeout_ms = timeout_ms;
    ret = wolfSSL_read(c->ssl, buf, cap);
    if (ret > 0)
        return ret;
    if (ret == 0)
        return 0;   /* peer closed (close_notify or TCP FIN) */
    e = wolfSSL_get_error(c->ssl, ret);
    if (e == WOLFSSL_ERROR_ZERO_RETURN)
        return 0;   /* close_notify */
    return -1;
}

void tls_close(TlsConn *c)
{
    if (c != NULL) {
        /* best effort: the borrowed NetConn is closed by the caller and
         * carries the TCP-level teardown either way */
        if (c->ssl != NULL)
            (void)wolfSSL_shutdown(c->ssl);
        tls_free(c);
    }
}
