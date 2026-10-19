/*
 * image.c - stb_image wrapper (see image.h).
 *
 * STB_IMAGE_IMPLEMENTATION lives here and only here; the rest of the
 * project just calls image_decode_*() and image_free(). stb_image is
 * compiled in a reduced configuration - STBI_NO_HDR/PIC/PNM drops the
 * float/pow layer and formats we never feed it, STBI_NO_SIMD keeps the
 * scalar decoders only (no emmintrin.h, Win98-safe) - PNG/JPEG/BMP/
 * GIF/TGA/PSD still cover every input Task 16 needs. Allocator
 * overrides are not used, so image_free() is plain free().
 *
 * Open Watcom note: with the defines above, stb_image v2.30 builds
 * warning-free under our -wx, so no warning suppressions are needed
 * here (unlike third_party\wolfssl, which needs its own flag set).
 *
 * Task 18 adds image_fetch() to this unit: an HTTP(S) GET of an image
 * blob over the net/tls/http stack (same pipeline shape as llm_send),
 * so image.c now includes net.h/tls.h/http.h and image.obj references
 * those layers' symbols (see the makefile link lines).
 */
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "net.h"
#include "tls.h"

#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_SIMD   /* scalar fallbacks only: keeps OW away from emmintrin.h and stays Win98-safe */
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

int image_decode_mem(const void *data, int len, int *w, int *h,
                     unsigned char **rgba)
{
    unsigned char *p;
    int x, y, n;

    if (rgba == NULL || w == NULL || h == NULL
        || data == NULL || len <= 0)
        return -1;
    *rgba = NULL;
    p = stbi_load_from_memory((const stbi_uc *)data, len, &x, &y, &n, 4);
    if (p == NULL)
        return -1;
    *w = x;
    *h = y;
    *rgba = p;
    return 0;
}

int image_decode_file(const char *path_utf8, int *w, int *h,
                      unsigned char **rgba)
{
    FILE *fp;
    unsigned char *p;
    int x, y, n;

    if (rgba == NULL || w == NULL || h == NULL
        || path_utf8 == NULL || path_utf8[0] == '\0')
        return -1;
    *rgba = NULL;
    fp = fopen(path_utf8, "rb");
    if (fp == NULL)
        return -1;
    p = stbi_load_from_file(fp, &x, &y, &n, 4);
    fclose(fp);
    if (p == NULL)
        return -1;
    *w = x;
    *h = y;
    *rgba = p;
    return 0;
}

void image_free(unsigned char *rgba)
{
    free(rgba);
}

/* --- image_fetch: HTTP(S) GET of an image blob ---------------------- */

#define IMG_FETCH_CAP (32L * 1024L * 1024L)   /* response accumulator cap */

static int img_net_send(void *io, const void *d, int n, int t)
{
    return net_send((NetConn *)io, d, n, t);
}
static int img_net_recv(void *io, void *b, int c, int t)
{
    return net_recv((NetConn *)io, b, c, t);
}
static int img_tls_send(void *io, const void *d, int n, int t)
{
    return tls_send((TlsConn *)io, d, n, t);
}
static int img_tls_recv(void *io, void *b, int c, int t)
{
    return tls_recv((TlsConn *)io, b, c, t);
}

typedef struct {          /* growable accumulator for the GET body */
    char *p;
    long n;
    long cap;
    int overflow;
} ImgBuf;

static void img_sink(void *p, const char *data, int len, int last)
{
    ImgBuf *bb = p;
    char *np;
    long ncap;

    (void)last;
    if (len <= 0)
        return;
    if (bb->n + len > IMG_FETCH_CAP) {
        bb->overflow = 1;
        return;
    }
    if (bb->n + len > bb->cap) {
        ncap = bb->cap > 0 ? bb->cap : 8192;
        while (ncap < bb->n + len)
            ncap *= 2;
        np = (char *)realloc(bb->p, (size_t)ncap);
        if (np == NULL) {
            bb->overflow = 1;   /* OOM: treat like oversize, report error */
            return;
        }
        bb->p = np;
        bb->cap = ncap;
    }
    memcpy(bb->p + bb->n, data, (size_t)len);
    bb->n += len;
}

int image_fetch(const char *url, const ModelCfg *m, const ProxyCfg *proxy,
                void **data, int *len, char *err, int errcap)
{
    char host[256];
    unsigned short port;
    char path[1024];
    char hdrbuf[320];
    const char *hdrs[4];
    HttpResult res;
    NetConn *nc;
    TlsConn *tc;
    void *io;
    IoSend iosend;
    IoRecv iorecv;
    ImgBuf bb;
    int use_tls;
    int rd;
    int rc;

    if (err && errcap > 0)
        err[0] = '\0';
    if (data)
        *data = NULL;
    if (len)
        *len = 0;
    if (url == NULL || url[0] == '\0' || data == NULL || len == NULL
        || err == NULL || errcap <= 0) {
        if (err && errcap > 0) {
            strncpy(err, "bad argument", (size_t)errcap - 1);
            err[errcap - 1] = '\0';
        }
        return -1;
    }
    nc = NULL;
    tc = NULL;
    rc = -1;

    if (net_parse_url(url, host, (int)sizeof(host), &port, path,
                      (int)sizeof(path), &use_tls) != 0) {
        strncpy(err, "invalid url (http/https only)",
                (size_t)errcap - 1);
        err[errcap - 1] = '\0';
        return -1;
    }
    if ((use_tls && port != 443) || (!use_tls && port != 80))
        snprintf(hdrbuf, sizeof(hdrbuf), "Host: %s:%u", host,
                 (unsigned)port);
    else
        snprintf(hdrbuf, sizeof(hdrbuf), "Host: %s", host);
    hdrs[0] = hdrbuf;
    hdrs[1] = NULL;

    /* ModelCfg carries the transport policy: use_proxy/verify_tls come
     * from the currently selected model, same as llm_send */
    if (net_connect(&nc, host, port, NET_TIMEOUT_CONNECT_MS, proxy,
                    m ? m->use_proxy : 0, err, errcap) != 0)
        return -1;
    if (use_tls) {
        if (tls_connect(&tc, nc, host, NULL,
                        m ? m->verify_tls : 1, err, errcap) != 0) {
            net_close(nc);
            return -1;
        }
        io = tc;
        iosend = img_tls_send;
        iorecv = img_tls_recv;
    } else {
        io = nc;
        iosend = img_net_send;
        iorecv = img_net_recv;
    }

    if (http_send_request2(io, iosend, iorecv, "GET", path, hdrs, NULL, 0,
                           &res, err, errcap) != 0)
        goto out;

    if (res.status >= 200 && res.status < 300) {
        bb.p = NULL;
        bb.n = 0;
        bb.cap = 0;
        bb.overflow = 0;
        rd = http_read_body2(io, iorecv, &res, NULL, img_sink, &bb,
                             err, errcap);
        if (rd == 0 && bb.n > 0 && !bb.overflow) {
            *data = bb.p;
            *len = (int)bb.n;
            rc = 0;
        } else {
            free(bb.p);
            if (rd == 0) {
                if (bb.overflow) {
                    strncpy(err, "response too large",
                            (size_t)errcap - 1);
                    err[errcap - 1] = '\0';
                } else {
                    strncpy(err, "empty response body",
                            (size_t)errcap - 1);
                    err[errcap - 1] = '\0';
                }
            }
        }
    } else {
        snprintf(err, (size_t)errcap, "HTTP %d", res.status);
        err[errcap - 1] = '\0';
    }

out:
    if (tc)
        tls_close(tc);
    net_close(nc);
    return rc;
}
