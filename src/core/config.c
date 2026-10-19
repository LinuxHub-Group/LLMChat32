/*
 * config.c - Config load/save on top of the toml.c subset parser.
 *
 * Load: config_defaults() first, then toml_parse() overlays recognized
 * keys. Unknown keys and unknown sections are skipped silently (the
 * save ruling in config.h says they are dropped anyway). Type mismatches
 * (e.g. font_size = "big") and a model count over the fixed array are
 * hard errors with a readable message, never a crash.
 *
 * Save: the file is rewritten from scratch in field order via a ".tmp"
 * sibling and rename() over the target (see config.h ruling: unknown
 * keys/comments from the previous file are lost). Doubles are printed
 * with the shortest %g precision (15/16/17 digits) that round-trips
 * through strtod, so values like -1 and 0.7 survive byte-exact.
 */
#include <stdio.h>      /* fprintf, fopen, ... */
#include <stdlib.h>     /* malloc, free, strtod */
#include <string.h>

#include "toml.h"
#include "config.h"

#define MAX_MODELS 32

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
}

void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->g.font_name, "SimSun");
    c->g.font_size = 9;
    strcpy(c->g.history_dir, "history");
    c->g.bg_opacity = 40;
    c->g.window_width = 640;
    c->g.window_height = 480;
    /* proxy: empty host, port 0; models: none */
}

/* --- load-side typed setters: mismatch/overflow -> err, return -1 --- */

static int kv_err(char *err, int errcap, const char *key, const char *what)
{
    if (err && errcap > 0)
        sprintf(err, "'%s' expects %s", key, what);
    return -1;
}

static int set_str(char *dst, int cap, const TomlVal *v, const char *key,
                   char *err, int errcap)
{
    if (v->type != TVAL_STR)
        return kv_err(err, errcap, key, "a string");
    if ((int)strlen(v->str) >= cap)
        return kv_err(err, errcap, key, "a shorter string");
    strcpy(dst, v->str);
    return 0;
}

static int set_int(int *dst, const TomlVal *v, const char *key,
                   char *err, int errcap)
{
    if (v->type != TVAL_INT)
        return kv_err(err, errcap, key, "an integer");
    *dst = (int)v->ival;
    return 0;
}

static int set_long(long *dst, const TomlVal *v, const char *key,
                    char *err, int errcap)
{
    if (v->type != TVAL_INT)
        return kv_err(err, errcap, key, "an integer");
    *dst = v->ival;
    return 0;
}

static int set_bool(int *dst, const TomlVal *v, const char *key,
                    char *err, int errcap)
{
    if (v->type != TVAL_BOOL)
        return kv_err(err, errcap, key, "a boolean");
    *dst = v->bval;
    return 0;
}

/* doubles accept both TOML ints and floats (temperature = -1 is an int) */
static int set_double(double *dst, const TomlVal *v, const char *key,
                      char *err, int errcap)
{
    if (v->type == TVAL_INT)
        *dst = (double)v->ival;
    else if (v->type == TVAL_FLOAT)
        *dst = v->fval;
    else
        return kv_err(err, errcap, key, "a number");
    return 0;
}

enum { SEC_ROOT, SEC_GENERAL, SEC_PROXY, SEC_MODELS, SEC_UNKNOWN };

typedef struct {
    Config *cfg;
    int sec;
} LoadCtx;

static int on_section(void *ctx, const char *name, int is_array)
{
    LoadCtx *lc = (LoadCtx *)ctx;

    if (is_array) {
        if (strcmp(name, "models") != 0) {
            lc->sec = SEC_UNKNOWN;
            return 0;
        }
        if (lc->cfg->n_models >= MAX_MODELS)
            return -1;  /* err set by config_load wrapper below */
        lc->sec = SEC_MODELS;
        memset(&lc->cfg->m[lc->cfg->n_models], 0, sizeof(ModelCfg));
        lc->cfg->n_models++;
        return 0;
    }
    if (strcmp(name, "general") == 0)
        lc->sec = SEC_GENERAL;
    else if (strcmp(name, "proxy") == 0)
        lc->sec = SEC_PROXY;
    else
        lc->sec = SEC_UNKNOWN;
    return 0;
}

static int on_kv(void *ctx, const char *key, const TomlVal *v,
                 char *err, int errcap)
{
    LoadCtx *lc = (LoadCtx *)ctx;
    Config *c = lc->cfg;

    switch (lc->sec) {
    case SEC_GENERAL:
        if (strcmp(key, "font_name") == 0)
            return set_str(c->g.font_name, (int)sizeof c->g.font_name, v, key, err, errcap);
        if (strcmp(key, "font_size") == 0)
            return set_int(&c->g.font_size, v, key, err, errcap);
        if (strcmp(key, "history_dir") == 0)
            return set_str(c->g.history_dir, (int)sizeof c->g.history_dir, v, key, err, errcap);
        if (strcmp(key, "bg_image") == 0)
            return set_str(c->g.bg_image, (int)sizeof c->g.bg_image, v, key, err, errcap);
        if (strcmp(key, "bg_opacity") == 0)
            return set_int(&c->g.bg_opacity, v, key, err, errcap);
        if (strcmp(key, "window_width") == 0)
            return set_int(&c->g.window_width, v, key, err, errcap);
        if (strcmp(key, "window_height") == 0)
            return set_int(&c->g.window_height, v, key, err, errcap);
        return 0;  /* unknown key in known section: skip */
    case SEC_PROXY:
        if (strcmp(key, "host") == 0)
            return set_str(c->p.host, (int)sizeof c->p.host, v, key, err, errcap);
        if (strcmp(key, "port") == 0)
            return set_int(&c->p.port, v, key, err, errcap);
        if (strcmp(key, "username") == 0)
            return set_str(c->p.username, (int)sizeof c->p.username, v, key, err, errcap);
        if (strcmp(key, "password") == 0)
            return set_str(c->p.password, (int)sizeof c->p.password, v, key, err, errcap);
        return 0;
    case SEC_MODELS: {
        ModelCfg *m = &c->m[c->n_models - 1];
        if (strcmp(key, "name") == 0)
            return set_str(m->name, (int)sizeof m->name, v, key, err, errcap);
        if (strcmp(key, "api") == 0)
            return set_str(m->api, (int)sizeof m->api, v, key, err, errcap);
        if (strcmp(key, "base_url") == 0)
            return set_str(m->base_url, (int)sizeof m->base_url, v, key, err, errcap);
        if (strcmp(key, "api_key") == 0)
            return set_str(m->api_key, (int)sizeof m->api_key, v, key, err, errcap);
        if (strcmp(key, "model_id") == 0)
            return set_str(m->model_id, (int)sizeof m->model_id, v, key, err, errcap);
        if (strcmp(key, "use_proxy") == 0)
            return set_bool(&m->use_proxy, v, key, err, errcap);
        if (strcmp(key, "stream") == 0)
            return set_bool(&m->stream, v, key, err, errcap);
        if (strcmp(key, "reasoning") == 0)
            return set_bool(&m->reasoning, v, key, err, errcap);
        if (strcmp(key, "verify_tls") == 0)
            return set_bool(&m->verify_tls, v, key, err, errcap);
        if (strcmp(key, "reasoning_effort") == 0)
            return set_str(m->reasoning_effort, (int)sizeof m->reasoning_effort, v, key, err, errcap);
        if (strcmp(key, "thinking_budget") == 0)
            return set_long(&m->thinking_budget, v, key, err, errcap);
        if (strcmp(key, "max_tokens") == 0)
            return set_long(&m->max_tokens, v, key, err, errcap);
        if (strcmp(key, "max_context_chars") == 0)
            return set_long(&m->max_context_chars, v, key, err, errcap);
        if (strcmp(key, "temperature") == 0)
            return set_double(&m->temperature, v, key, err, errcap);
        return 0;
    }
    default:
        return 0;  /* root / unknown section: skip */
    }
}

int config_load(Config *cfg, const char *path, char *err, int errcap)
{
    FILE *fp;
    long sz;
    char *buf;
    int rc;
    LoadCtx lc;

    config_defaults(cfg);
    fp = fopen(path, "rb");
    if (!fp)
        return 0;  /* missing file: defaults, not an error */
    if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        set_err(err, errcap, "cannot read config file");
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        set_err(err, errcap, "out of memory");
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        set_err(err, errcap, "cannot read config file");
        return -1;
    }
    fclose(fp);
    buf[sz] = '\0';

    lc.cfg = cfg;
    lc.sec = SEC_ROOT;
    rc = toml_parse(buf, (int)sz, on_section, on_kv, &lc, err, errcap);
    free(buf);
    if (rc != 0) {
        /* callback abort (too many [[models]]) may not have set err */
        if (err && errcap > 0 && err[0] == '\0')
            set_err(err, errcap, "too many [[models]] entries (max 32)");
        return -1;
    }
    return 0;
}

/* --- save side --- */

static int wr_str(FILE *fp, const char *key, const char *val)
{
    const unsigned char *p = (const unsigned char *)val;

    if (fprintf(fp, "%s = \"", key) < 0)
        return -1;
    for (; *p; p++) {
        unsigned char c = *p;
        int r;

        if (c == '"') r = fprintf(fp, "\\\"");
        else if (c == '\\') r = fprintf(fp, "\\\\");
        else if (c == '\b') r = fprintf(fp, "\\b");
        else if (c == '\f') r = fprintf(fp, "\\f");
        else if (c == '\n') r = fprintf(fp, "\\n");
        else if (c == '\r') r = fprintf(fp, "\\r");
        else if (c == '\t') r = fprintf(fp, "\\t");
        else if (c < 0x20) r = fprintf(fp, "\\u%04x", (int)c);
        else r = fputc((int)c, fp);
        if (r < 0)
            return -1;
    }
    return fprintf(fp, "\"\n") < 0 ? -1 : 0;
}

static int wr_int(FILE *fp, const char *key, int val)
{
    return fprintf(fp, "%s = %d\n", key, val) < 0 ? -1 : 0;
}

static int wr_long(FILE *fp, const char *key, long val)
{
    return fprintf(fp, "%s = %ld\n", key, val) < 0 ? -1 : 0;
}

static int wr_bool(FILE *fp, const char *key, int val)
{
    return fprintf(fp, "%s = %s\n", key, val ? "true" : "false") < 0 ? -1 : 0;
}

/* Shortest %g precision that reads back as the same double. */
static void fmt_double(char *buf, double v)
{
    int prec;

    for (prec = 15; prec <= 17; prec++) {
        sprintf(buf, "%.*g", prec, v);
        if (strtod(buf, NULL) == v)
            break;
    }
}

static int wr_double(FILE *fp, const char *key, double val)
{
    char num[32];

    fmt_double(num, val);
    return fprintf(fp, "%s = %s\n", key, num) < 0 ? -1 : 0;
}

static int save_models(FILE *fp, const Config *c)
{
    int i;

    for (i = 0; i < c->n_models; i++) {
        const ModelCfg *m = &c->m[i];

        if (fprintf(fp, "[[models]]\n") < 0)
            return -1;
        if (wr_str(fp, "name", m->name) != 0)
            return -1;
        if (wr_str(fp, "api", m->api) != 0)
            return -1;
        if (wr_str(fp, "base_url", m->base_url) != 0)
            return -1;
        if (wr_str(fp, "api_key", m->api_key) != 0)
            return -1;
        if (wr_str(fp, "model_id", m->model_id) != 0)
            return -1;
        if (wr_bool(fp, "use_proxy", m->use_proxy) != 0)
            return -1;
        if (wr_bool(fp, "stream", m->stream) != 0)
            return -1;
        if (wr_bool(fp, "reasoning", m->reasoning) != 0)
            return -1;
        if (wr_str(fp, "reasoning_effort", m->reasoning_effort) != 0)
            return -1;
        if (wr_long(fp, "thinking_budget", m->thinking_budget) != 0)
            return -1;
        if (wr_long(fp, "max_tokens", m->max_tokens) != 0)
            return -1;
        if (wr_long(fp, "max_context_chars", m->max_context_chars) != 0)
            return -1;
        if (wr_double(fp, "temperature", m->temperature) != 0)
            return -1;
        if (wr_bool(fp, "verify_tls", m->verify_tls) != 0)
            return -1;
        if (fprintf(fp, "\n") < 0)
            return -1;
    }
    return 0;
}

int config_save(const Config *cfg, const char *path, char *err, int errcap)
{
    size_t plen = strlen(path);
    char *tmp = (char *)malloc(plen + 5);
    FILE *fp;
    int bad = 0;

    if (!tmp) {
        set_err(err, errcap, "out of memory");
        return -1;
    }
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5);

    fp = fopen(tmp, "wb");
    if (!fp) {
        set_err(err, errcap, "cannot open config file for writing");
        free(tmp);
        return -1;
    }

    if (fprintf(fp, "# LLMChat32 config\n\n") < 0)
        bad = 1;
    if (!bad && fprintf(fp, "[general]\n") < 0)
        bad = 1;
    if (!bad && wr_str(fp, "font_name", cfg->g.font_name) != 0)
        bad = 1;
    if (!bad && wr_int(fp, "font_size", cfg->g.font_size) != 0)
        bad = 1;
    if (!bad && wr_str(fp, "history_dir", cfg->g.history_dir) != 0)
        bad = 1;
    if (!bad && wr_str(fp, "bg_image", cfg->g.bg_image) != 0)
        bad = 1;
    if (!bad && wr_int(fp, "bg_opacity", cfg->g.bg_opacity) != 0)
        bad = 1;
    if (!bad && wr_int(fp, "window_width", cfg->g.window_width) != 0)
        bad = 1;
    if (!bad && wr_int(fp, "window_height", cfg->g.window_height) != 0)
        bad = 1;
    if (!bad && fprintf(fp, "\n[proxy]\n") < 0)
        bad = 1;
    if (!bad && wr_str(fp, "host", cfg->p.host) != 0)
        bad = 1;
    if (!bad && wr_int(fp, "port", cfg->p.port) != 0)
        bad = 1;
    if (!bad && wr_str(fp, "username", cfg->p.username) != 0)
        bad = 1;
    if (!bad && wr_str(fp, "password", cfg->p.password) != 0)
        bad = 1;
    if (!bad && fprintf(fp, "\n") < 0)
        bad = 1;
    if (!bad && save_models(fp, cfg) != 0)
        bad = 1;

    /* fprintf/fputc failures set the stream error indicator; a full
     * disk typically surfaces only at the final fclose() flush. */
    if (ferror(fp))
        bad = 1;
    if (fclose(fp) != 0)
        bad = 1;
    if (bad) {
        set_err(err, errcap, "failed to write config file (disk full?)");
        remove(tmp);
        free(tmp);
        return -1;
    }

    remove(path);  /* best effort: rename() cannot replace on Win32 */
    if (rename(tmp, path) != 0) {
        set_err(err, errcap, "failed to replace config file");
        remove(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}
