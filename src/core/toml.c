/*
 * toml.c - TOML subset parser implementation (see toml.h).
 *
 * Line-oriented: the input is split at '\n', each line is classified
 * as blank / comment / header / key-value and dispatched to the
 * caller's callbacks. Values are tokenized strictly - a number token
 * must be consumed entirely by strtol/strtod and a string must close
 * on the same line - so garbage like "12x" or "name = "unterminated
 * is an error, never a silent partial parse.
 */
#include <errno.h>
#include <stdio.h>      /* sprintf */
#include <stdlib.h>     /* strtol, strtod */
#include <string.h>

#include "toml.h"

typedef struct {
    const char *cur;
    const char *end;
    int line;
    char *err;
    int errcap;
    TomlSecFn on_section;
    TomlKvFn on_kv;
    void *ctx;
} TP;

static int t_fail(TP *p, const char *msg)
{
    if (p->err && p->errcap > 0)
        sprintf(p->err, "%s at line %d", msg, p->line);
    return -1;
}

static int is_hspace(char c)
{
    return c == ' ' || c == '\t';
}

static const char *skip_hspace(const char *q, const char *le)
{
    while (q < le && is_hspace(*q))
        q++;
    return q;
}

/* Scan a section name (between [ and ]): trim inner whitespace,
 * validate the bare-key character set, copy into out. */
static int scan_section_name(TP *p, const char **pq, const char *le,
                             char *out, int outcap, int *is_array)
{
    const char *q = *pq;
    const char *name;
    const char *tail;
    int len;

    *is_array = 0;
    if (q < le && *q == '[') {
        *is_array = 1;
        q++;
    }
    q = skip_hspace(q, le);
    name = q;
    while (q < le && *q != ']')
        q++;
    if (q >= le)
        return t_fail(p, "unterminated section header");
    tail = q;
    while (tail > name && is_hspace(tail[-1]))
        tail--;
    if (*is_array) {
        if (q < le && q + 1 < le && q[1] == ']')
            q += 2;
        else
            return t_fail(p, "unterminated array table header");
    } else {
        q++;
    }
    if (tail == name)
        return t_fail(p, "empty section name");
    len = (int)(tail - name);
    if (len >= outcap)
        return t_fail(p, "section name too long");
    memcpy(out, name, (size_t)len);
    out[len] = '\0';
    *pq = q;
    return 0;
}

static int parse_string(TP *p, const char **pq, const char *le, TomlVal *v)
{
    const char *q = *pq + 1;    /* skip opening quote */
    char *out = v->str;
    int n = 0;

    while (q < le && *q != '"') {
        unsigned char c = (unsigned char)*q;
        if (c == '\\') {
            char e;
            q++;
            if (q >= le)
                return t_fail(p, "unterminated string");
            e = *q;
            if (e == '"') c = '"';
            else if (e == '\\') c = '\\';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
            else return t_fail(p, "unsupported escape in string");
            q++;
        } else {
            if (c < 0x20)
                return t_fail(p, "control character in string");
            q++;
        }
        if (n + 1 >= (int)sizeof v->str)
            return t_fail(p, "string value too long");
        out[n++] = (char)c;
    }
    if (q >= le)
        return t_fail(p, "unterminated string");
    q++;                        /* skip closing quote */
    out[n] = '\0';
    v->type = TVAL_STR;
    *pq = q;
    return 0;
}

static int parse_number(TP *p, const char **pq, const char *le, TomlVal *v)
{
    const char *q = *pq;
    const char *start = q;
    char tok[64];
    int len;
    int is_float = 0;
    char *ep;

    while (q < le &&
           (*q == '+' || *q == '-' || (*q >= '0' && *q <= '9') ||
            *q == '.' || *q == 'e' || *q == 'E')) {
        if (*q == '.' || *q == 'e' || *q == 'E')
            is_float = 1;
        q++;
    }
    len = (int)(q - start);
    if (len <= 0 || len >= (int)sizeof tok)
        return t_fail(p, "bad number");
    memcpy(tok, start, (size_t)len);
    tok[len] = '\0';
    errno = 0;
    if (is_float) {
        v->fval = strtod(tok, &ep);
        if (ep != tok + len)
            return t_fail(p, "bad number");
        v->type = TVAL_FLOAT;
    } else {
        v->ival = strtol(tok, &ep, 10);
        if (ep != tok + len)
            return t_fail(p, "bad number");
        v->type = TVAL_INT;
    }
    if (errno == ERANGE)
        return t_fail(p, "number out of range");
    *pq = q;
    return 0;
}

static int parse_value(TP *p, const char **pq, const char *le, TomlVal *v)
{
    const char *q = skip_hspace(*pq, le);

    if (q >= le)
        return t_fail(p, "missing value");
    if (*q == '"') {
        if (parse_string(p, &q, le, v) != 0)
            return -1;
    } else if (*q == 't' || *q == 'f') {
        if ((int)(le - q) >= 4 && memcmp(q, "true", 4) == 0) {
            v->type = TVAL_BOOL;
            v->bval = 1;
            q += 4;
        } else if ((int)(le - q) >= 5 && memcmp(q, "false", 5) == 0) {
            v->type = TVAL_BOOL;
            v->bval = 0;
            q += 5;
        } else {
            return t_fail(p, "invalid value");
        }
    } else if ((*q >= '0' && *q <= '9') || *q == '-' || *q == '+') {
        if (parse_number(p, &q, le, v) != 0)
            return -1;
    } else {
        return t_fail(p, "unsupported value");
    }
    q = skip_hspace(q, le);
    if (q < le && *q != '#')
        return t_fail(p, "trailing garbage after value");
    *pq = q;
    return 0;
}

static int parse_line(TP *p, const char *ls, const char *le)
{
    const char *q = skip_hspace(ls, le);

    if (q >= le || *q == '#')
        return 0;
    if (*q == '[') {
        char sec[32];
        int is_array;
        q++;  /* past '[' */
        if (scan_section_name(p, &q, le, sec, (int)sizeof sec, &is_array) != 0)
            return -1;
        q = skip_hspace(q, le);
        if (q < le && *q != '#')
            return t_fail(p, "trailing garbage after section header");
        return p->on_section(p->ctx, sec, is_array);
    } else {
        const char *kstart = q;
        char key[64];
        TomlVal v;
        int len;

        while (q < le &&
               ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                (*q >= '0' && *q <= '9') || *q == '_' || *q == '-'))
            q++;
        len = (int)(q - kstart);
        if (len <= 0)
            return t_fail(p, "expected key");
        if (len >= (int)sizeof key)
            return t_fail(p, "key too long");
        memcpy(key, kstart, (size_t)len);
        key[len] = '\0';
        q = skip_hspace(q, le);
        if (q >= le || *q != '=')
            return t_fail(p, "expected '='");
        q++;
        memset(&v, 0, sizeof v);
        if (parse_value(p, &q, le, &v) != 0)
            return -1;
        return p->on_kv(p->ctx, key, &v, p->err, p->errcap);
    }
}

int toml_parse(const char *text, int len,
               TomlSecFn on_section, TomlKvFn on_kv, void *ctx,
               char *err, int errcap)
{
    TP p;

    if (err && errcap > 0)
        err[0] = '\0';
    if (!text) {
        if (err && errcap > 0)
            sprintf(err, "no input");
        return -1;
    }
    if (len < 0)
        len = (int)strlen(text);
    p.cur = text;
    p.end = text + len;
    p.line = 1;
    p.err = err;
    p.errcap = errcap;
    p.on_section = on_section;
    p.on_kv = on_kv;
    p.ctx = ctx;
    while (p.cur < p.end) {
        const char *eol = p.cur;
        int rc;

        while (eol < p.end && *eol != '\n')
            eol++;
        rc = parse_line(&p, p.cur, eol);
        if (rc != 0)
            return rc;
        p.cur = (eol < p.end) ? eol + 1 : eol;
        p.line++;
    }
    return 0;
}
