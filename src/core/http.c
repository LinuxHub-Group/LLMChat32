/*
 * http.c - HTTP/1.1 client layer (see http.h for the contract).
 *
 * Request serialization is a single bounded buffer (request heads are
 * small; the body is sent separately). The response head is read byte
 * at a time until CRLFCRLF (or a bare LFLF) so that body bytes arriving
 * in the same TCP segment are never consumed by the head reader - the
 * fixed interface leaves no place to stash overflow, and heads are at
 * most a few hundred bytes in practice. Status lines are parsed with
 * sscanf "HTTP/%*d.%*d %d", tolerant of missing reason text. Headers
 * are defolded first (obs-fold continuation lines joined with one
 * space), split on LF with CR stripped, and matched case-insensitively.
 *
 * Body framing: exact Content-Length bytes, or a chunked decoder that
 * reads each chunk-size line (hex, optional ;extension ignored), then
 * exactly that many bytes, then the terminating CRLF as an empty line,
 * until the 0-chunk; trailer lines follow and are skipped to the blank
 * line. With neither Content-Length nor chunked the body is read until
 * the peer closes the connection.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <stdio.h>      /* sprintf, sscanf, vsnprintf */
#include <stdlib.h>     /* strtol */
#include <string.h>
#include <stdarg.h>     /* va_list for set_errf / req_appendf */

#include "http.h"

#define HTTP_REQ_CAP   4096    /* serialized request head */
#define HTTP_HEAD_CAP  8192    /* response status line + headers */
#define HTTP_IO_CAP    4096    /* body read chunk */
#define HTTP_LINE_CAP   512    /* chunk-size / trailer line */

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
}

/* printf-style variant of set_err: formats into err honoring errcap,
 * so messages with embedded snippets can never overrun the buffer */
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

/* --- small text helpers -------------------------------------------------- */

static int ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        ch = ch - 'A' + 'a';
    return ch;
}

/* case-insensitive equality of a counted name against a literal */
static int name_is(const char *name, int nlen, const char *want)
{
    int wlen = (int)strlen(want);
    int i;

    while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\t'))
        nlen--;
    if (nlen != wlen)
        return 0;
    for (i = 0; i < nlen; i++) {
        if (ascii_lower((unsigned char)name[i])
            != ascii_lower((unsigned char)want[i]))
            return 0;
    }
    return 1;
}

/* case-insensitive substring search over a counted haystack */
static int ci_contains(const char *hay, int haylen, const char *needle)
{
    int nlen = (int)strlen(needle);
    int i, j;

    if (nlen == 0)
        return 1;
    if (nlen > haylen)
        return 0;
    for (i = 0; i + nlen <= haylen; i++) {
        for (j = 0; j < nlen; j++) {
            if (ascii_lower((unsigned char)hay[i + j])
                != ascii_lower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen)
            return 1;
    }
    return 0;
}

/* does the caller's "K: V" header use the given name (case-insensitive)? */
static int header_named(const char *hdr, const char *name)
{
    const char *colon = strchr(hdr, ':');
    int nlen;

    if (colon == NULL)
        return 0;
    nlen = (int)(colon - hdr);
    return name_is(hdr, nlen, name);
}

/* --- request serialization ----------------------------------------------- */

/* bounded sprintf-append into req; returns -1 (with nothing committed
 * past the current length) when the formatted text would not fit */
static int req_appendf(char *buf, int cap, int *n, const char *fmt, ...)
{
    va_list ap;
    int rc;

    if (*n >= cap - 1)
        return -1;
    va_start(ap, fmt);
    rc = vsnprintf(buf + *n, (size_t)(cap - *n), fmt, ap);
    va_end(ap);
    if (rc < 0 || rc >= cap - *n)
        return -1;
    *n += rc;
    return 0;
}

/* --- response head parsing ------------------------------------------------ */
/*
 * read_head - read the status line and headers byte at a time until
 * CRLFCRLF or a bare LFLF. Byte-at-a-time is deliberate: anything past
 * the head terminator belongs to the body and must stay in the socket
 * buffer for http_read_body.
 */
static int read_head(NetConn *c, char *head, int cap, char *err, int errcap)
{
    int n = 0;

    for (;;) {
        char b;
        int r = net_recv(c, &b, 1, NET_TIMEOUT_RW_MS);
        if (r <= 0) {
            set_err(err, errcap,
                    "connection closed or timed out during response headers");
            return -1;
        }
        if (n >= cap - 1) {
            set_err(err, errcap, "response headers too large");
            return -1;
        }
        head[n++] = b;
        head[n] = '\0';
        if ((n >= 4 && head[n - 4] == '\r' && head[n - 3] == '\n'
             && head[n - 2] == '\r' && head[n - 1] == '\n')
            || (n >= 2 && head[n - 2] == '\n' && head[n - 1] == '\n'))
            return 0;
    }
}

/*
 * defold - rewrite the head into a normalized form where every logical
 * header occupies exactly one line: obs-fold continuation lines (starting
 * with SP/HT) are joined onto the previous header with a single space.
 * The output never grows (each fold replaces a line break with a space),
 * so a same-sized buffer always suffices.
 */
static void defold(const char *head, char *out, int cap)
{
    const char *p = head;
    int o = 0;
    int lines = 0;

    while (*p && o < cap - 1) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        int fold = lines >= 2 && len > 0
                   && (p[0] == ' ' || p[0] == '\t');

        if (len > 0 && p[len - 1] == '\r')
            len--;
        if (fold) {
            int v = 0;
            while (v < len && (p[v] == ' ' || p[v] == '\t'))
                v++;
            if (o > 0 && out[o - 1] == '\n')
                o--;
            if (o < cap - 1)
                out[o++] = ' ';
            while (v < len && o < cap - 1)
                out[o++] = p[v++];
        } else {
            int i;
            for (i = 0; i < len && o < cap - 1; i++)
                out[o++] = p[i];
            if (o < cap - 1)
                out[o++] = '\n';
            lines++;
        }
        if (e == NULL)
            break;
        p = e + 1;
    }
    out[o] = '\0';
}

/* parse one header line "Name: value" from the defolded head */
static void parse_header_line(const char *line, int len, HttpResult *out)
{
    const char *colon = NULL;
    int i;
    const char *v;
    const char *vend;

    for (i = 0; i < len; i++) {
        if (line[i] == ':') {
            colon = line + i;
            break;
        }
    }
    if (colon == NULL)
        return;
    v = colon + 1;
    vend = line + len;
    while (v < vend && (*v == ' ' || *v == '\t'))
        v++;
    while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t'))
        vend--;

    if (name_is(line, (int)(colon - line), "content-length")) {
        const char *q = v;
        long val = 0;
        int digits = 0;

        while (q < vend && *q >= '0' && *q <= '9') {
            if (val <= 0x0fffffffL)   /* saturate past a sane bound */
                val = val * 10 + (*q - '0');
            digits++;
            q++;
        }
        if (digits > 0)
            out->content_length = val;
    } else if (name_is(line, (int)(colon - line), "transfer-encoding")) {
        if (ci_contains(v, (int)(vend - v), "chunked"))
            out->chunked = 1;
    } else if (name_is(line, (int)(colon - line), "content-type")) {
        int n = (int)(vend - v);
        if (n >= (int)sizeof(out->content_type))
            n = (int)sizeof(out->content_type) - 1;
        memcpy(out->content_type, v, (size_t)n);
        out->content_type[n] = '\0';
    }
}

/*
 * parse_head - split the defolded head into the status line and header
 * lines, filling *out. The head always ends with a blank line; LF and
 * CRLF line ends are both accepted.
 */
static int parse_head(const char *head, HttpResult *out,
                      char *err, int errcap)
{
    char def[HTTP_HEAD_CAP];
    const char *p;
    int status = 0;

    out->status = 0;
    out->chunked = 0;
    out->content_length = -1;
    out->content_type[0] = '\0';

    defold(head, def, (int)sizeof(def));

    /* status line: "HTTP/1.1 200" plus optional reason text; sscanf is
     * tolerant of extra whitespace and of a missing reason */
    if (sscanf(def, "HTTP/%*d.%*d %d", &status) != 1 || status < 100
        || status > 999) {
        set_errf(err, errcap, "bad status line '%.*s'", 40, def);
        return -1;
    }
    out->status = status;

    p = strchr(def, '\n');
    if (p == NULL)
        return 0;
    p++;
    while (*p) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);

        if (len == 0)
            break;              /* blank line ends the headers */
        if (len > 0 && p[len - 1] == '\r')
            len--;
        if (len > 0)
            parse_header_line(p, len, out);
        if (e == NULL)
            break;
        p = e + 1;
    }
    return 0;
}

/* --- body framing decoders ------------------------------------------------- */

/* rdline - read one line (LF-terminated, CR stripped) byte at a time;
 * used for chunk-size lines, per-chunk CRLF, and trailers, all of which
 * are short. An overlong line is a protocol error rather than a
 * truncated parse, which could desync the chunked decoder. */
static int rdline(NetConn *c, char *line, int cap, char *err, int errcap)
{
    int n = 0;

    for (;;) {
        char b;
        int r = net_recv(c, &b, 1, NET_TIMEOUT_RW_MS);
        if (r <= 0) {
            set_err(err, errcap,
                    "connection closed or timed out mid-chunk");
            return -1;
        }
        if (b == '\n') {
            if (n > 0 && line[n - 1] == '\r')
                n--;
            line[n] = '\0';
            return 0;
        }
        if (n >= cap - 1) {
            set_err(err, errcap, "chunk header line too long");
            return -1;
        }
        line[n++] = b;
    }
}

/* parse a chunk-size line: hex digits, optional ;extension (or anything
 * else) ignored. Returns the size, or -1 after setting err. */
static long parse_chunk_size(const char *line, char *err, int errcap)
{
    const char *p = line;
    unsigned long v = 0;
    int digits = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    for (;;) {
        char ch = *p;
        int d;

        if (ch >= '0' && ch <= '9')
            d = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F')
            d = ch - 'A' + 10;
        else
            break;
        v = v * 16 + (unsigned long)d;
        if (v > 0x7fffffffUL) {
            set_err(err, errcap, "chunk size too large");
            return -1;
        }
        digits++;
        p++;
    }
    if (digits == 0) {
        set_errf(err, errcap, "bad chunk size '%.*s'", 40, line);
        return -1;
    }
    return (long)v;
}

static int read_sized(NetConn *c, long total, const volatile int *cancel,
                      HttpBodyFn fn, void *ctx, char *err, int errcap)
{
    char buf[HTTP_IO_CAP];
    long got = 0;

    while (got < total) {
        long remaining = total - got;
        int want = remaining > (long)sizeof(buf)
                       ? (int)sizeof(buf) : (int)remaining;
        int r;

        if (cancel && *cancel)
            return -2;
        r = net_recv(c, buf, want, NET_TIMEOUT_RW_MS);
        if (r <= 0) {
            set_err(err, errcap,
                    "connection closed or timed out during body");
            return -1;
        }
        if (fn)
            fn(ctx, buf, r, 0);
        got += r;
    }
    if (fn)
        fn(ctx, "", 0, 1);
    return 0;
}

static int read_till_close(NetConn *c, const volatile int *cancel,
                           HttpBodyFn fn, void *ctx, char *err, int errcap)
{
    char buf[HTTP_IO_CAP];

    for (;;) {
        int r;

        if (cancel && *cancel)
            return -2;
        r = net_recv(c, buf, (int)sizeof(buf), NET_TIMEOUT_RW_MS);
        if (r < 0) {
            set_err(err, errcap, "error reading body");
            return -1;
        }
        if (r == 0)
            break;              /* peer closed: end of body */
        if (fn)
            fn(ctx, buf, r, 0);
    }
    if (fn)
        fn(ctx, "", 0, 1);
    return 0;
}

static int read_chunked(NetConn *c, const volatile int *cancel,
                        HttpBodyFn fn, void *ctx, char *err, int errcap)
{
    char line[HTTP_LINE_CAP];
    char buf[HTTP_IO_CAP];

    for (;;) {
        long size;
        long got = 0;

        if (cancel && *cancel)
            return -2;
        if (rdline(c, line, (int)sizeof(line), err, errcap) != 0)
            return -1;
        size = parse_chunk_size(line, err, errcap);
        if (size < 0)
            return -1;
        if (size == 0) {
            /* trailers until the blank line, then the body is done */
            for (;;) {
                if (cancel && *cancel)
                    return -2;
                if (rdline(c, line, (int)sizeof(line), err, errcap) != 0)
                    return -1;
                if (line[0] == '\0')
                    break;
            }
            if (fn)
                fn(ctx, "", 0, 1);
            return 0;
        }
        while (got < size) {
            long remaining = size - got;
            int want = remaining > (long)sizeof(buf)
                           ? (int)sizeof(buf) : (int)remaining;
            int r;

            if (cancel && *cancel)
                return -2;
            r = net_recv(c, buf, want, NET_TIMEOUT_RW_MS);
            if (r <= 0) {
                set_err(err, errcap,
                        "connection closed or timed out mid-chunk");
                return -1;
            }
            if (fn)
                fn(ctx, buf, r, 0);
            got += r;
        }
        /* each chunk ends with CRLF (read as an empty line; bare LF
         * tolerated), anything else is a protocol error */
        if (rdline(c, line, (int)sizeof(line), err, errcap) != 0)
            return -1;
        if (line[0] != '\0') {
            set_err(err, errcap, "malformed chunk terminator");
            return -1;
        }
    }
}

/* --- public API ------------------------------------------------------------ */

int http_send_request(NetConn *c, const char *method, const char *path,
                      const char *const *headers,
                      const char *body, int body_len,
                      HttpResult *out, char *err, int errcap)
{
    char req[HTTP_REQ_CAP];
    int n = 0;
    int i;

    if (err && errcap > 0)
        err[0] = '\0';
    if (out) {
        out->status = 0;
        out->chunked = 0;
        out->content_length = -1;
        out->content_type[0] = '\0';
    }
    if (c == NULL || method == NULL || path == NULL || out == NULL
        || body_len < 0 || (body == NULL && body_len > 0)) {
        set_err(err, errcap, "bad arguments to http_send_request");
        return -1;
    }

    /* serialize the request head; Host comes from the caller's headers */
    if (req_appendf(req, (int)sizeof(req), &n, "%s %s HTTP/1.1\r\n",
                    method, path) != 0) {
        set_err(err, errcap, "request line too long");
        return -1;
    }
    if (headers != NULL) {
        for (i = 0; headers[i] != NULL; i++) {
            if (req_appendf(req, (int)sizeof(req), &n, "%s\r\n",
                            headers[i]) != 0) {
                set_err(err, errcap, "request headers too long");
                return -1;
            }
        }
    }
    if (body_len > 0) {
        int has_cl = 0;

        if (headers != NULL) {
            for (i = 0; headers[i] != NULL; i++) {
                if (header_named(headers[i], "content-length")) {
                    has_cl = 1;
                    break;
                }
            }
        }
        if (!has_cl && req_appendf(req, (int)sizeof(req), &n,
                                   "Content-Length: %d\r\n",
                                   body_len) != 0) {
            set_err(err, errcap, "request headers too long");
            return -1;
        }
    }
    if (req_appendf(req, (int)sizeof(req), &n, "\r\n") != 0) {
        set_err(err, errcap, "request headers too long");
        return -1;
    }

    if (net_send(c, req, n, NET_TIMEOUT_RW_MS) != 0) {
        set_err(err, errcap, "sending request failed");
        return -1;
    }
    if (body_len > 0 && net_send(c, body, body_len, NET_TIMEOUT_RW_MS) != 0) {
        set_err(err, errcap, "sending request body failed");
        return -1;
    }

    /* read the response head, skipping interim 1xx (but not 101, which
     * switches protocols and cannot be skipped) */
    for (i = 0; i < 10; i++) {
        char head[HTTP_HEAD_CAP];

        if (read_head(c, head, (int)sizeof(head), err, errcap) != 0)
            return -1;
        if (parse_head(head, out, err, errcap) != 0)
            return -1;
        if (out->status >= 100 && out->status < 200 && out->status != 101)
            continue;
        return 0;
    }
    set_err(err, errcap, "too many interim responses");
    return -1;
}

int http_read_body(NetConn *c, const HttpResult *res,
                   const volatile int *cancel,
                   HttpBodyFn fn, void *ctx, char *err, int errcap)
{
    if (err && errcap > 0)
        err[0] = '\0';
    if (c == NULL || res == NULL) {
        set_err(err, errcap, "bad arguments to http_read_body");
        return -1;
    }
    if (res->chunked)
        return read_chunked(c, cancel, fn, ctx, err, errcap);
    if (res->content_length >= 0)
        return read_sized(c, res->content_length, cancel, fn, ctx,
                          err, errcap);
    return read_till_close(c, cancel, fn, ctx, err, errcap);
}
