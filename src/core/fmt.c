/*
 * fmt.c - minimal in-house printf-style formatter (see fmt.h).
 *
 * Pure integer/string formatting - no floating point, no locale, no
 * stdio, no heap, reentrant by construction. Buffers are bounded by the
 * caller's cap; every write goes through one bounds-checked putc so a
 * buggy caller can never overrun.
 */
#include "fmt.h"

typedef struct {
    char *buf;
    int cap;
    int n;              /* bytes written, excluding NUL */
} Fmt;

static void fputc_(Fmt *f, char c)
{
    if (f->cap > 0 && f->n < f->cap - 1)
        f->buf[f->n] = c;
    f->n++;
}

static void fpad(Fmt *f, char c, int count)
{
    while (count-- > 0)
        fputc_(f, c);
}

static int udigits(unsigned long v, int base)
{
    int n = 1;

    while (v >= (unsigned long)base) {
        v /= (unsigned long)base;
        n++;
    }
    return n;
}

static void fuint(Fmt *f, unsigned long v, int base, int upper)
{
    char tmp[40];
    int i = 0;
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0)
        tmp[i++] = '0';
    else
        while (v != 0 && i < (int)sizeof tmp) {
            tmp[i++] = d[v % (unsigned)base];
            v /= (unsigned)base;
        }
    while (i > 0)
        fputc_(f, tmp[--i]);
}

int fmt_vsnprintf(char *buf, int cap, const char *fmt, va_list ap)
{
    Fmt f;
    const char *p;

    f.buf = buf;
    f.cap = cap;
    f.n = 0;
    if (cap > 0 && buf != 0)
        buf[0] = '\0';
    if (fmt == 0)
        return 0;

    p = fmt;
    while (*p != '\0') {
        int minus = 0;
        int zero = 0;
        int width = -1;
        int prec = -1;
        int long_ = 0;
        int c;

        if (*p != '%') {
            fputc_(&f, *p++);
            continue;
        }
        p++;
        for (;;) {
            if (*p == '-') {
                minus = 1;
                p++;
            } else if (*p == '0') {
                zero = 1;
                p++;
            } else if (*p == '+' || *p == ' ' || *p == '#') {
                p++;
            } else {
                break;
            }
        }
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                minus = 1;
                width = -width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                if (width < 0)
                    width = 0;
                width = width * 10 + (*p - '0');
                p++;
            }
        }
        if (*p == '.') {
            p++;
            if (*p == '*') {
                prec = va_arg(ap, int);
                p++;
            } else {
                prec = 0;
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }
        if (*p == 'l') {
            long_ = 1;
            p++;
            if (*p == 'l') {
                long_ = 2;
                p++;
            }
        } else if (*p == 'h') {
            p++;
            if (*p == 'h')
                p++;
        }
        c = (unsigned char)*p;
        if (c == '\0')
            break;
        p++;
        switch (c) {
        case '%':
            fputc_(&f, '%');
            break;
        case 'd':
        case 'i': {
            long v = long_ ? va_arg(ap, long) : (long)va_arg(ap, int);
            unsigned long uv;
            int neg = 0;
            int digits;
            int pad;

            if (v < 0) {
                neg = 1;
                uv = (unsigned long)(-(v + 1)) + 1UL;
            } else {
                uv = (unsigned long)v;
            }
            digits = udigits(uv, 10);
            pad = prec > digits ? prec - digits
                 : (zero && !minus && width > digits + neg)
                     ? width - digits - neg : 0;
            if (neg)
                fputc_(&f, '-');
            fpad(&f, '0', pad);
            fuint(&f, uv, 10, 0);
            if (minus && width > digits + neg + pad)
                fpad(&f, ' ', width - digits - neg - pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long uv = long_ ? va_arg(ap, unsigned long)
                                     : (unsigned long)va_arg(ap, unsigned int);
            int base = (c == 'x' || c == 'X') ? 16 : (c == 'o') ? 8 : 10;
            int upper = (c == 'X');
            int digits = udigits(uv, base);
            int pad = prec > digits ? prec - digits
                      : (zero && !minus && width > digits)
                          ? width - digits : 0;

            fpad(&f, '0', pad);
            fuint(&f, uv, base, upper);
            if (minus && width > digits + pad)
                fpad(&f, ' ', width - digits - pad);
            break;
        }
        case 'c':
            fputc_(&f, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            int i;

            if (s == 0)
                s = "(null)";
            while (s[len] != '\0')
                len++;
            if (prec >= 0 && len > prec)
                len = prec;
            if (!minus && width > len)
                fpad(&f, ' ', width - len);
            for (i = 0; i < len; i++)
                fputc_(&f, s[i]);
            if (minus && width > len)
                fpad(&f, ' ', width - len);
            break;
        }
        case 'p':
            fputc_(&f, '0');
            fputc_(&f, 'x');
            fuint(&f, (unsigned long)va_arg(ap, void *), 16, 0);
            break;
        default:
            fputc_(&f, '%');
            fputc_(&f, (char)c);
            break;
        }
    }
    if (cap > 0 && buf != 0) {
        int end = f.n < cap - 1 ? f.n : cap - 1;

        buf[end] = '\0';
    }
    return f.n;
}

int fmt_snprintf(char *buf, int cap, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = fmt_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
