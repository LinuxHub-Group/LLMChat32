/*
 * net.c - TCP connection layer (see net.h for the contract).
 *
 * Blocking sockets with select() timeouts. DNS via gethostbyname
 * (Windows 98 has no getaddrinfo). net_send loops until the whole
 * buffer is written. When use_proxy && proxy->host[0], the TCP
 * connection goes to the proxy and the target is requested with
 * "CONNECT host:port HTTP/1.1"; only a 2xx status line is accepted,
 * and proxy->username[0] adds a Proxy-Authorization header with a
 * locally implemented base64 encoder. The target host is never
 * resolved locally in the proxy path - the proxy resolves it.
 *
 * Timeouts are always caller parameters (timeout_ms); this file keeps
 * no defaults of its own. Callers wanting the standard limits pass
 * NET_TIMEOUT_CONNECT_MS / NET_TIMEOUT_RW_MS from net.h.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <stdio.h>      /* sprintf, sscanf */
#include <stdlib.h>     /* malloc, free */
#include <string.h>
#include <stdarg.h>     /* va_list for set_errf */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "net.h"

struct NetConn { SOCKET sock; };

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

/* wait until s is readable (want_write=0) or writable (want_write=1);
 * returns >0 ready, 0 timeout, -1 select error */
static int wait_fd(SOCKET s, int want_write, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(s, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, want_write ? NULL : &fds, want_write ? &fds : NULL,
                  NULL, &tv);
}

static int set_blocking(SOCKET s)
{
    u_long nb = 0;
    return ioctlsocket(s, FIONBIO, &nb);
}

/* DNS: gethostbyname (IPv4 only, as everywhere else in this layer) */
static int resolve_name(const char *host, unsigned short port,
                        struct sockaddr_in *sa)
{
    struct hostent *he;

    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    he = gethostbyname(host);
    if (he == NULL || he->h_addrtype != AF_INET ||
        he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
        return -1;
    memcpy(&sa->sin_addr, he->h_addr_list[0], sizeof(sa->sin_addr));
    return 0;
}

/* connect with a bounded wait: non-blocking connect + select, then the
 * socket is put back in blocking mode; returns 0 on success */
static int connect_timeboxed(SOCKET s, const struct sockaddr_in *sa,
                             int timeout_ms)
{
    u_long nb = 1;

    if (timeout_ms < 0)
        return connect(s, (const struct sockaddr *)sa, sizeof(*sa));

    if (ioctlsocket(s, FIONBIO, &nb) != 0)
        return -1;
    if (connect(s, (const struct sockaddr *)sa, sizeof(*sa)) != 0 &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        set_blocking(s);
        return -1;
    }
    if (wait_fd(s, 1, timeout_ms) <= 0) {   /* timeout or select error */
        set_blocking(s);
        return -1;
    }
    {
        int soerr = 0;
        int len = (int)sizeof(soerr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &len) != 0) {
            set_blocking(s);
            return -1;
        }
        if (soerr != 0) {
            WSASetLastError(soerr);
            set_blocking(s);
            return -1;
        }
    }
    return set_blocking(s);
}

/* --- proxy CONNECT ---------------------------------------------------- */

static const char b64_alpha[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_encode(const unsigned char *in, int len, char *out, int cap)
{
    int i = 0, o = 0;

    while (i < len) {
        unsigned v;
        int rem = len - i;

        if (o + 4 >= cap)
            return -1;
        if (rem >= 3) {
            v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8)
                | (unsigned)in[i + 2];
            out[o++] = b64_alpha[(v >> 18) & 63];
            out[o++] = b64_alpha[(v >> 12) & 63];
            out[o++] = b64_alpha[(v >> 6) & 63];
            out[o++] = b64_alpha[v & 63];
            i += 3;
        } else if (rem == 2) {
            v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
            out[o++] = b64_alpha[(v >> 18) & 63];
            out[o++] = b64_alpha[(v >> 12) & 63];
            out[o++] = b64_alpha[(v >> 6) & 63];
            out[o++] = '=';
            i += 2;
        } else {
            v = (unsigned)in[i] << 16;
            out[o++] = b64_alpha[(v >> 18) & 63];
            out[o++] = b64_alpha[(v >> 12) & 63];
            out[o++] = '=';
            out[o++] = '=';
            i += 1;
        }
    }
    out[o] = '\0';
    return 0;
}

static int proxy_send_request(NetConn *c, const char *host,
                              unsigned short port, const ProxyCfg *proxy,
                              int timeout_ms, char *err, int errcap)
{
    char req[1024];
    int n;

    /* host is pre-checked to fit (with the port digits and headers)
     * below; keep the sprintf bounded by that check */
    if (strlen(host) > 120) {
        set_err(err, errcap, "CONNECT target host name too long");
        return -1;
    }
    n = sprintf(req, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n",
                host, (unsigned)port, host, (unsigned)port);
    if (proxy->username[0]) {
        char cred[160];   /* "user:pass" from fixed 64-byte fields */
        char b64[220];

        sprintf(cred, "%.*s:%.*s", 63, proxy->username, 63, proxy->password);
        if (b64_encode((const unsigned char *)cred, (int)strlen(cred),
                       b64, (int)sizeof(b64)) != 0) {
            set_err(err, errcap, "proxy credentials too long");
            return -1;
        }
        n += sprintf(req + n, "Proxy-Authorization: Basic %s\r\n", b64);
    }
    n += sprintf(req + n, "\r\n");
    if (n < 0 || n >= (int)sizeof(req)) {
        set_err(err, errcap, "CONNECT request too long");
        return -1;
    }
    if (net_send(c, req, n, timeout_ms) != 0) {
        set_err(err, errcap, "sending CONNECT to proxy failed");
        return -1;
    }
    return 0;
}

/* read the proxy's CONNECT response through the end of the headers and
 * accept only a 2xx status line; any bytes past the headers belong to
 * the tunnel, so the caller must not have written anything yet */
static int proxy_read_response(NetConn *c, int timeout_ms,
                               char *err, int errcap)
{
    char buf[1024];
    int len = 0;
    int code;

    for (;;) {
        int n = net_recv(c, buf + len, (int)sizeof(buf) - 1 - len,
                         timeout_ms);
        if (n <= 0) {
            set_err(err, errcap, "proxy closed during CONNECT");
            return -1;
        }
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n") != NULL)
            break;
        if (len >= (int)sizeof(buf) - 1) {
            set_err(err, errcap, "proxy CONNECT reply too large");
            return -1;
        }
    }
    if (sscanf(buf, "HTTP/%*d.%*d %d", &code) != 1) {
        set_err(err, errcap, "bad proxy CONNECT reply");
        return -1;
    }
    if (code < 200 || code > 299) {
        set_errf(err, errcap, "proxy CONNECT refused (status %d)", code);
        return -1;
    }
    return 0;
}

/* --- public API -------------------------------------------------------- */

int net_connect(NetConn **out, const char *host, unsigned short port,
                int timeout_ms, const ProxyCfg *proxy, int use_proxy,
                char *err, int errcap)
{
    const char *chost = host;
    unsigned short cport = port;
    struct sockaddr_in sa;
    SOCKET s;
    NetConn *c;
    int via_proxy = (use_proxy && proxy && proxy->host[0]);

    if (out)
        *out = NULL;
    if (err && errcap > 0)
        err[0] = '\0';
    if (out == NULL || host == NULL || host[0] == '\0' || port == 0) {
        set_err(err, errcap, "bad arguments to net_connect");
        return -1;
    }
    if (via_proxy) {
        if (proxy->port <= 0 || proxy->port > 65535) {
            set_err(err, errcap, "bad proxy port");
            return -1;
        }
        chost = proxy->host;
        cport = (unsigned short)proxy->port;
    }
    if (resolve_name(chost, cport, &sa) != 0) {
        set_errf(err, errcap, "cannot resolve '%.*s' (wsa %d)", 100, chost,
                 WSAGetLastError());
        return -1;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        sprintf(err, "socket() failed (wsa %d)", WSAGetLastError());
        return -1;
    }
    if (connect_timeboxed(s, &sa, timeout_ms) != 0) {
        set_errf(err, errcap, "connect to '%.*s:%u' failed (wsa %d)", 100,
                 chost, (unsigned)cport, WSAGetLastError());
        closesocket(s);
        return -1;
    }
    c = (NetConn *)malloc(sizeof(*c));
    if (c == NULL) {
        set_err(err, errcap, "out of memory");
        closesocket(s);
        return -1;
    }
    c->sock = s;
    if (via_proxy &&
        (proxy_send_request(c, host, port, proxy, timeout_ms, err, errcap)
             != 0 ||
         proxy_read_response(c, timeout_ms, err, errcap) != 0)) {
        net_close(c);
        return -1;
    }
    *out = c;
    return 0;
}

int net_send(NetConn *c, const void *data, int len, int timeout_ms)
{
    const char *p = (const char *)data;
    int sent = 0;

    if (c == NULL || data == NULL || len < 0)
        return -1;
    while (sent < len) {
        int rc;
        if (timeout_ms >= 0 && wait_fd(c->sock, 1, timeout_ms) <= 0)
            return -1;
        rc = send(c->sock, p + sent, len - sent, 0);
        if (rc > 0) {
            sent += rc;
            continue;
        }
        if (rc < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        return -1;
    }
    return 0;
}

int net_recv(NetConn *c, void *buf, int cap, int timeout_ms)
{
    int rc;

    if (c == NULL || buf == NULL || cap <= 0)
        return -1;
    if (timeout_ms >= 0 && wait_fd(c->sock, 0, timeout_ms) <= 0)
        return -1;   /* timeout or select error */
    rc = recv(c->sock, (char *)buf, cap, 0);
    if (rc > 0)
        return rc;
    if (rc == 0)
        return 0;    /* peer closed */
    return -1;
}

void net_close(NetConn *c)
{
    if (c != NULL) {
        closesocket(c->sock);
        free(c);
    }
}

/* --- URL parsing ------------------------------------------------------- */

static int ci_prefix(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

int net_parse_url(const char *url, char *host, int hostcap,
                  unsigned short *port, char *path, int pathcap,
                  int *use_tls)
{
    const char *p;
    int tls, defport, hlen, plen, pnum = 0, has_port = 0, i;

    if (use_tls)
        *use_tls = 0;
    if (port)
        *port = 0;
    if (url == NULL || host == NULL || hostcap <= 0 ||
        port == NULL || path == NULL || pathcap <= 0)
        return -1;
    if (ci_prefix(url, "https://")) {
        tls = 1;
        defport = 443;
        p = url + 8;
    } else if (ci_prefix(url, "http://")) {
        tls = 0;
        defport = 80;
        p = url + 7;
    } else {
        return -1;
    }
    /* host[:port] */
    hlen = 0;
    while (p[hlen] != '\0' && p[hlen] != ':' && p[hlen] != '/')
        hlen++;
    if (hlen == 0 || hlen >= hostcap)
        return -1;
    memcpy(host, p, (size_t)hlen);
    host[hlen] = '\0';
    p += hlen;
    if (*p == ':') {
        p++;
        if (!is_digit(*p))
            return -1;
        while (is_digit(*p)) {
            pnum = pnum * 10 + (*p - '0');
            if (pnum > 65535)
                return -1;
            p++;
        }
        if (pnum == 0)
            return -1;
        has_port = 1;
    }
    *port = (unsigned short)(has_port ? pnum : defport);
    if (use_tls)
        *use_tls = tls;
    /* path: empty becomes "/", must start with '/', no raw spaces */
    if (*p == '\0') {
        if (pathcap < 2)
            return -1;
        strcpy(path, "/");
        return 0;
    }
    if (*p != '/')
        return -1;
    plen = (int)strlen(p);
    if (plen >= pathcap)
        return -1;
    for (i = 0; i < plen; i++) {
        if (p[i] == ' ')
            return -1;
    }
    memcpy(path, p, (size_t)plen + 1);
    return 0;
}
