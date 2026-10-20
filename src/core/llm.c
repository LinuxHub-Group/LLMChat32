/*
 * llm.c - LLM request construction + response parsing + send pipeline.
 *
 * Task 10 half: turns a Session into the protocol-specific request body
 * with the JB streaming writer, plus auth-header and context-window
 * helpers (pure functions).
 *
 * Task 11 half (this file, below): an SSE line buffer that passes body
 * bytes through untouched (no character reassembly - a UTF-8 character
 * split across chunks arrives byte-identical), per-protocol event
 * dispatch (anthropic content_block_delta/message_delta, chat
 * choices[].delta/[DONE], responses output_text/reasoning_summary_text
 * deltas), a non-streaming whole-body parser, a non-2xx error-body
 * message extractor, and llm_send, which wires net_connect -> tls_connect
 * (https only) -> http_send_request2/read_body2 -> parse -> cleanup.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "llm.h"
#include "json.h"
#include "http.h"

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
}

/* printf-style variant of set_err: formatted messages can never
 * overrun err (http.c has its own private copy of this helper) */
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

const char *llm_api_path(const ModelCfg *m)
{
    if (!m)
        return "";
    if (strcmp(m->api, "anthropic") == 0)
        return "/v1/messages";
    if (strcmp(m->api, "chat") == 0)
        return "/v1/chat/completions";
    if (strcmp(m->api, "responses") == 0)
        return "/v1/responses";
    return "";
}

/* Compose the request path from the base_url path part and the protocol
 * endpoint (see llm.h for the exact rules). Pure string surgery so the
 * doubling-prone join is unit-testable. */
int llm_compose_path(const char *basepath, const char *api,
                     char *out, int cap)
{
    char base[1024];
    size_t blen, alen, need;

    if (!out || cap < 1)
        return -1;
    out[0] = '\0';
    if (!api)
        return -1;
    alen = strlen(api);
    if (!basepath || basepath[0] == '\0' || strcmp(basepath, "/") == 0) {
        if (alen + 1 > (size_t)cap)
            return -1;
        memcpy(out, api, alen + 1);
        return 0;
    }

    blen = strlen(basepath);
    if (blen >= sizeof base)
        return -1;
    memcpy(base, basepath, blen + 1);
    while (blen > 0 && base[blen - 1] == '/')
        base[--blen] = '\0';
    if (blen == 0) {            /* all slashes: treat as no path at all */
        if (alen + 1 > (size_t)cap)
            return -1;
        memcpy(out, api, alen + 1);
        return 0;
    }

    /* base already ends with the api path: the user pasted the full
     * endpoint URL into base_url (e.g. .../v1/chat/completions) */
    if (blen >= alen && strcmp(base + blen - alen, api) == 0) {
        if (blen >= (size_t)cap)
            return -1;
        memcpy(out, base, blen + 1);
        return 0;
    }

    /* OpenAI-style ".../v1" prefix: dedupe the version segment */
    if (strncmp(api, "/v1/", 4) == 0 && blen >= 3 &&
        strcmp(base + blen - 3, "/v1") == 0) {
        need = blen + alen - 3;
        if (need >= (size_t)cap)
            return -1;
        memcpy(out, base, blen);
        memcpy(out + blen, api + 3, alen - 3 + 1);
        return 0;
    }

    need = blen + alen;
    if (need >= (size_t)cap)
        return -1;
    memcpy(out, base, blen);
    memcpy(out + blen, api, alen + 1);
    return 0;
}

/* reasoning_effort -> anthropic thinking budget_tokens (see llm.h). */
static long effort_budget(const char *effort)
{
    if (effort && strcmp(effort, "low") == 0)
        return 2000;
    if (effort && strcmp(effort, "high") == 0)
        return 32000;
    return 8000;    /* medium, and unknown/empty falls back to medium */
}

/*
 * Effective thinking budget: a user-configured ModelCfg.thinking_budget
 * > 0 wins; otherwise fall back to the reasoning_effort mapping.
 */
static long thinking_budget(const ModelCfg *m)
{
    if (m->thinking_budget > 0)
        return m->thinking_budget;
    return effort_budget(m->reasoning_effort);
}

/* Estimated send cost of one message: text blocks only (spec section 6);
   thinking is never sent and images ride as a short "[图片] url" line,
   so neither is worth counting. */
static long msg_text_chars(const Msg *msg)
{
    long n = 0;
    int i;

    for (i = 0; i < msg->nb; i++)
        if (msg->b[i].kind == BLOCK_TEXT && msg->b[i].text)
            n += (long)strlen(msg->b[i].text);
    return n;
}

void llm_truncate_context(const ModelCfg *m, const Session *s,
                          int *first_msg_idx, long *est_chars)
{
    long budget, total, suffix;
    int i, first;

    if (!first_msg_idx || !est_chars)
        return;
    *first_msg_idx = 0;
    *est_chars = 0;
    if (!m || !s)
        return;

    budget = m->max_context_chars;
    total = 0;
    for (i = 0; i < s->nm; i++)
        if (s->m[i].role != ROLE_SYSTEM)
            total += msg_text_chars(&s->m[i]);

    if (budget <= 0 || total <= budget) {
        *est_chars = total;
        return;
    }

    /* drop the oldest user/assistant messages until the suffix fits */
    suffix = total;
    first = 0;
    for (i = 0; i < s->nm; i++) {
        if (s->m[i].role == ROLE_SYSTEM)
            continue;
        if (suffix <= budget)
            break;
        suffix -= msg_text_chars(&s->m[i]);
        first = i + 1;
    }
    *first_msg_idx = first;
    *est_chars = suffix;
}

/*
 * Join the sendable blocks of msg into a freshly malloc'd plain string:
 * text blocks verbatim, image blocks as "[图片] url", blocks separated
 * by '\n'. Thinking blocks are skipped (spec section 6). NULL on OOM.
 */
static char *msg_content(const Msg *msg)
{
    size_t need = 1;
    int i, n;
    char *out, *p;

    for (i = 0, n = 0; i < msg->nb; i++) {
        const Block *b = &msg->b[i];
        if (b->kind == BLOCK_THINK)
            continue;
        if (n > 0)
            need += 1;                  /* '\n' separator */
        if (b->kind == BLOCK_IMAGE)
            need += 9;                  /* "[图片] " */
        if (b->text)
            need += strlen(b->text);
        n++;
    }
    out = (char *)malloc(need);
    if (!out)
        return NULL;
    p = out;
    n = 0;
    for (i = 0; i < msg->nb; i++) {
        const Block *b = &msg->b[i];
        size_t len;
        if (b->kind == BLOCK_THINK)
            continue;
        if (n > 0)
            *p++ = '\n';
        if (b->kind == BLOCK_IMAGE) {
            memcpy(p, "[图片] ", 9);
            p += 9;
        }
        len = b->text ? strlen(b->text) : 0;
        if (len > 0) {
            memcpy(p, b->text, len);
            p += len;
        }
        n++;
    }
    *p = '\0';
    return out;
}

/* Join the text blocks of every system message into one malloc'd string
   (system never carries images/thinking here; empty string when none). */
static char *system_content(const Session *s)
{
    size_t need = 1;
    int i, j, n;
    char *out, *p;

    for (i = 0, n = 0; i < s->nm; i++) {
        if (s->m[i].role != ROLE_SYSTEM)
            continue;
        for (j = 0; j < s->m[i].nb; j++) {
            const Block *b = &s->m[i].b[j];
            if (b->kind != BLOCK_TEXT || !b->text)
                continue;
            if (n > 0)
                need += 1;
            need += strlen(b->text);
            n++;
        }
    }
    out = (char *)malloc(need);
    if (!out)
        return NULL;
    p = out;
    n = 0;
    for (i = 0; i < s->nm; i++) {
        if (s->m[i].role != ROLE_SYSTEM)
            continue;
        for (j = 0; j < s->m[i].nb; j++) {
            const Block *b = &s->m[i].b[j];
            size_t len;
            if (b->kind != BLOCK_TEXT || !b->text)
                continue;
            if (n > 0)
                *p++ = '\n';
            len = strlen(b->text);
            memcpy(p, b->text, len);
            p += len;
            n++;
        }
    }
    *p = '\0';
    return out;
}

static const char *role_name(Role r)
{
    switch (r) {
    case ROLE_SYSTEM: return "system";
    case ROLE_ASSIST: return "assistant";
    default:          return "user";
    }
}

/* {"role": <role>, "content": <content>} */
static void jb_msg(JB *b, const char *role, const char *content)
{
    jb_obj_begin(b);
    jb_key(b, "role");
    jb_str(b, role);
    jb_key(b, "content");
    jb_str(b, content);
    jb_obj_end(b);
}

/*
 * Append a message array under arrkey. When with_system is set every
 * system message comes first (system is never truncated); the kept
 * user/assistant window then starts at first_msg_idx. Anthropic passes
 * with_system=0 because its system text travels in the top-level field.
 * Returns 0, or -1 on OOM (partial JSON stays in b, caller aborts).
 */
static int jb_messages(JB *b, const char *arrkey, const Session *s,
                       int first_msg_idx, int with_system)
{
    int i;

    jb_key(b, arrkey);
    jb_arr_begin(b);
    for (i = 0; i < s->nm; i++) {
        const Msg *msg = &s->m[i];
        char *content;
        if (msg->role == ROLE_SYSTEM) {
            if (!with_system)
                continue;
        } else if (i < first_msg_idx) {
            continue;
        }
        content = msg_content(msg);
        if (!content)
            return -1;
        jb_msg(b, role_name(msg->role), content);
        free(content);
    }
    jb_arr_end(b);
    return 0;
}

int llm_build_request(const ModelCfg *m, const Session *s,
                      char *buf, int cap, char *err, int errcap)
{
    JB b;
    int is_anthropic, is_chat, first_msg_idx;
    long est_chars;

    if (err && errcap > 0)
        err[0] = '\0';
    if (!m || !s || !buf || cap <= 0) {
        set_err(err, errcap, "bad argument");
        return -1;
    }
    is_anthropic = strcmp(m->api, "anthropic") == 0;
    is_chat = strcmp(m->api, "chat") == 0;
    if (!is_anthropic && !is_chat && strcmp(m->api, "responses") != 0) {
        set_err(err, errcap, "unknown api");
        return -1;
    }

    llm_truncate_context(m, s, &first_msg_idx, &est_chars);

    jb_init(&b, buf, cap);
    jb_obj_begin(&b);
    jb_key(&b, "model");
    jb_str(&b, m->model_id);
    jb_key(&b, "max_tokens");
    jb_int(&b, m->max_tokens);

    if (is_anthropic) {
        char *sys = system_content(s);
        if (!sys) {
            set_err(err, errcap, "out of memory");
            return -1;
        }
        if (sys[0] != '\0') {
            jb_key(&b, "system");
            jb_str(&b, sys);
        }
        free(sys);
        if (m->reasoning) {
            jb_key(&b, "thinking");
            jb_obj_begin(&b);
            jb_key(&b, "type");
            jb_str(&b, "enabled");
            jb_key(&b, "budget_tokens");
            jb_int(&b, thinking_budget(m));
            jb_obj_end(&b);
        }
        if (jb_messages(&b, "messages", s, first_msg_idx, 0) != 0) {
            set_err(err, errcap, "out of memory");
            return -1;
        }
    } else {
        if (jb_messages(&b, is_chat ? "messages" : "input", s,
                        first_msg_idx, 1) != 0) {
            set_err(err, errcap, "out of memory");
            return -1;
        }
        if (m->reasoning) {
            if (is_chat) {
                jb_key(&b, "reasoning_effort");
                jb_str(&b, m->reasoning_effort);
            } else {
                jb_key(&b, "reasoning");
                jb_obj_begin(&b);
                jb_key(&b, "effort");
                jb_str(&b, m->reasoning_effort);
                jb_obj_end(&b);
            }
        }
    }

    if (m->temperature >= 0.0) {
        jb_key(&b, "temperature");
        jb_num(&b, m->temperature);
    }
    jb_key(&b, "stream");
    jb_bool(&b, m->stream);
    jb_obj_end(&b);

    if (b.overflow) {
        set_err(err, errcap, "request buffer overflow");
        return -1;
    }
    return 0;
}

int llm_build_headers(const ModelCfg *m, char *buf, int cap)
{
    int n;

    if (!m || !buf || cap <= 0)
        return -1;
    if (strcmp(m->api, "anthropic") == 0)
        n = snprintf(buf, (size_t)cap,
                     "x-api-key: %s\r\n"
                     "anthropic-version: 2023-06-01\r\n",
                     m->api_key);
    else
        n = snprintf(buf, (size_t)cap, "Authorization: Bearer %s\r\n",
                     m->api_key);
    if (n < 0 || n >= cap)
        return -1;
    return n;
}

/* --- response parsing (Task 11) ------------------------------------------- */

static int api_is(const ModelCfg *m, const char *api)
{
    return m && strcmp(m->api, api) == 0;
}

/* All event firing goes through these: they enforce the "exactly one
 * terminal callback" rule (once on_done or on_error fired, the stream
 * is silent) and NULL-guard every callback. */
static void fire_text(LlmSse *st, const LlmEvents *ev, void *ctx,
                      const char *s)
{
    if (st->done || !s || !*s || !ev->on_text)
        return;
    ev->on_text(ctx, s, (int)strlen(s));
}

static void fire_think(LlmSse *st, const LlmEvents *ev, void *ctx,
                       const char *s)
{
    if (st->done || !s || !*s || !ev->on_think)
        return;
    ev->on_think(ctx, s, (int)strlen(s));
}

static void fire_done(LlmSse *st, const LlmEvents *ev, void *ctx, int finish)
{
    if (st->done)
        return;
    st->done = 1;
    if (ev->on_done)
        ev->on_done(ctx, finish);
}

static void fire_error(LlmSse *st, const LlmEvents *ev, void *ctx,
                       const char *msg)
{
    if (st->done)
        return;
    st->done = 1;
    if (ev->on_error)
        ev->on_error(ctx, msg);
}

/* map a protocol finish string to the LlmEvents finish code:
 * the "length" token -> 1, anything else meaningful -> 0 */
static int finish_code(const char *reason, const char *length_token)
{
    if (reason && *reason && strcmp(reason, length_token) == 0)
        return 1;
    return 0;
}

/* --- per-protocol SSE event dispatch -------------------------------------- */

static void sse_anthropic(LlmSse *st, JVal *root, const LlmEvents *ev,
                          void *ctx)
{
    const char *type = json_str(json_obj_get(root, "type"));
    JVal *delta;

    if (strcmp(type, "content_block_delta") == 0) {
        delta = json_obj_get(root, "delta");
        if (json_type(delta) != JOBJ)
            return;
        if (strcmp(json_str(json_obj_get(delta, "type")),
                   "thinking_delta") == 0)
            fire_think(st, ev, ctx, json_str(json_obj_get(delta, "thinking")));
        else if (strcmp(json_str(json_obj_get(delta, "type")),
                        "text_delta") == 0)
            fire_text(st, ev, ctx, json_str(json_obj_get(delta, "text")));
    } else if (strcmp(type, "message_delta") == 0) {
        const char *sr;

        delta = json_obj_get(root, "delta");
        sr = json_str(json_obj_get(delta, "stop_reason"));
        if (sr && *sr)
            fire_done(st, ev, ctx, finish_code(sr, "max_tokens"));
    }
}

static void sse_chat(LlmSse *st, JVal *root, const LlmEvents *ev, void *ctx)
{
    JVal *choices = json_obj_get(root, "choices");
    JVal *c, *delta;
    const char *s;

    if (json_type(choices) != JARR || json_arr_len(choices) < 1)
        return;
    c = json_arr_get(choices, 0);
    if (json_type(c) != JOBJ)
        return;
    delta = json_obj_get(c, "delta");

    s = json_str(json_obj_get(delta, "reasoning_content"));
    if (*s)
        fire_think(st, ev, ctx, s);
    s = json_str(json_obj_get(delta, "content"));
    if (*s)
        fire_text(st, ev, ctx, s);

    s = json_str(json_obj_get(c, "finish_reason"));
    if (*s)
        fire_done(st, ev, ctx, finish_code(s, "length"));
}

static void sse_responses(LlmSse *st, JVal *root, const LlmEvents *ev,
                          void *ctx)
{
    const char *type = json_str(json_obj_get(root, "type"));
    const char *s;

    if (strcmp(type, "response.output_text.delta") == 0) {
        fire_text(st, ev, ctx, json_str(json_obj_get(root, "delta")));
    } else if (strcmp(type, "response.reasoning_summary_text.delta") == 0) {
        fire_think(st, ev, ctx, json_str(json_obj_get(root, "delta")));
    } else if (strcmp(type, "response.completed") == 0) {
        s = json_str(json_obj_get(json_obj_get(root, "response"), "status"));
        fire_done(st, ev, ctx, finish_code(s, "incomplete"));
    } else if (strcmp(type, "response.incomplete") == 0) {
        fire_done(st, ev, ctx, 1);
    }
}

/* dispatch one completed event (the aggregated data: payload) */
static void dispatch_event(LlmSse *st, const ModelCfg *m,
                           const LlmEvents *ev, void *ctx)
{
    JVal *root;
    char jerr[128];

    if (st->data_overflow || st->data_len == 0)
        return;
    st->data[st->data_len] = '\0';
    if (strcmp(st->data, "[DONE]") == 0) {   /* chat end-of-stream marker */
        fire_done(st, ev, ctx, 0);
        return;
    }
    root = json_parse(st->data, -1, jerr, (int)sizeof(jerr));
    if (!root) {
        fire_error(st, ev, ctx, "malformed event JSON");
        return;
    }
    if (api_is(m, "anthropic"))
        sse_anthropic(st, root, ev, ctx);
    else if (api_is(m, "chat"))
        sse_chat(st, root, ev, ctx);
    else if (api_is(m, "responses"))
        sse_responses(st, root, ev, ctx);
    else
        fire_error(st, ev, ctx, "unknown api");
    json_free(root);
}

void llm_sse_reset(LlmSse *st)
{
    if (st)
        memset(st, 0, sizeof *st);
}

/* one complete line arrived (LF seen). Blank line ends the event. */
static void sse_line(LlmSse *st, const ModelCfg *m, const LlmEvents *ev,
                     void *ctx)
{
    char *line = st->line;
    int len = st->line_len;

    st->line_len = 0;
    if (st->line_overflow) {           /* overlong line: dropped safely */
        st->line_overflow = 0;
        return;
    }
    if (len > 0 && line[len - 1] == '\r')
        line[--len] = '\0';
    line[len] = '\0';                  /* terminate for the strlen below */
    if (len == 0) {                    /* blank line: dispatch + reset */
        dispatch_event(st, m, ev, ctx);
        st->data_len = 0;
        st->data_overflow = 0;
        return;
    }
    if (len >= 5 && strncmp(line, "data:", 5) == 0) {
        const char *p = line + 5;
        int plen, sep;

        if (*p == ' ')                   /* SSE: strip ONE leading space */
            p++;
        plen = (int)strlen(p);
        sep = st->data_len > 0 ? 1 : 0;  /* join multi data: lines with \n */
        if (st->data_len + sep + plen > LLM_SSE_DATA_MAX) {
            st->data_overflow = 1;       /* oversized event: drop whole */
            return;
        }
        if (sep)
            st->data[st->data_len++] = '\n';
        memcpy(st->data + st->data_len, p, (size_t)plen);
        st->data_len += plen;
        return;
    }
    /* comment lines (:...) and fields we do not use (event:/id:/retry:)
     * are ignored */
}

void llm_feed_sse(const ModelCfg *m, const char *chunk, int len,
                  LlmSse *st, const LlmEvents *ev, void *ctx)
{
    int i;

    if (!st || !chunk || len <= 0)
        return;
    for (i = 0; i < len; i++) {
        char c = chunk[i];

        if (c == '\n') {
            sse_line(st, m, ev, ctx);
            continue;
        }
        if (st->line_overflow)
            continue;                  /* skip rest of the overlong line */
        if (st->line_len >= LLM_SSE_LINE_MAX) {
            st->line_overflow = 1;     /* over 4096: drop until LF */
            continue;
        }
        st->line[st->line_len++] = c;
    }
    st->line[st->line_len] = '\0';
}

/* --- non-streaming whole-body parse ---------------------------------------- */

static void ns_anthropic(LlmSse *st, JVal *root, const LlmEvents *ev,
                         void *ctx)
{
    JVal *content = json_obj_get(root, "content");
    const char *sr;
    int i;

    if (json_type(content) == JARR) {
        for (i = 0; i < json_arr_len(content); i++) {
            JVal *b = json_arr_get(content, i);
            const char *t = json_str(json_obj_get(b, "type"));

            if (strcmp(t, "thinking") == 0)
                fire_think(st, ev, ctx,
                           json_str(json_obj_get(b, "thinking")));
            else if (strcmp(t, "text") == 0)
                fire_text(st, ev, ctx, json_str(json_obj_get(b, "text")));
        }
    }
    sr = json_str(json_obj_get(root, "stop_reason"));
    fire_done(st, ev, ctx, finish_code(sr, "max_tokens"));
}

static void ns_chat(LlmSse *st, JVal *root, const LlmEvents *ev, void *ctx)
{
    JVal *choices = json_obj_get(root, "choices");
    JVal *c, *msg;
    const char *s;

    if (json_type(choices) == JARR && json_arr_len(choices) > 0)
        c = json_arr_get(choices, 0);
    else
        c = NULL;
    if (json_type(c) == JOBJ) {
        msg = json_obj_get(c, "message");
        s = json_str(json_obj_get(msg, "reasoning_content"));
        if (*s)
            fire_think(st, ev, ctx, s);
        s = json_str(json_obj_get(msg, "content"));
        if (*s)
            fire_text(st, ev, ctx, s);
        s = json_str(json_obj_get(c, "finish_reason"));
    } else {
        s = "";
    }
    fire_done(st, ev, ctx, finish_code(s, "length"));
}

static void ns_responses(LlmSse *st, JVal *root, const LlmEvents *ev,
                         void *ctx)
{
    JVal *output = json_obj_get(root, "output");
    const char *status;
    int i, j;

    if (json_type(output) == JARR) {
        for (i = 0; i < json_arr_len(output); i++) {
            JVal *item = json_arr_get(output, i);
            const char *t = json_str(json_obj_get(item, "type"));

            if (strcmp(t, "message") == 0) {
                JVal *content = json_obj_get(item, "content");

                if (json_type(content) == JARR) {
                    for (j = 0; j < json_arr_len(content); j++) {
                        JVal *b = json_arr_get(content, j);
                        if (strcmp(json_str(json_obj_get(b, "type")),
                                   "output_text") == 0)
                            fire_text(st, ev, ctx,
                                      json_str(json_obj_get(b, "text")));
                    }
                }
            } else if (strcmp(t, "reasoning") == 0) {
                JVal *summary = json_obj_get(item, "summary");

                if (json_type(summary) == JARR) {
                    for (j = 0; j < json_arr_len(summary); j++) {
                        JVal *b = json_arr_get(summary, j);
                        if (strcmp(json_str(json_obj_get(b, "type")),
                                   "summary_text") == 0)
                            fire_think(st, ev, ctx,
                                       json_str(json_obj_get(b, "text")));
                    }
                }
            }
        }
    }
    status = json_str(json_obj_get(root, "status"));
    fire_done(st, ev, ctx, finish_code(status, "incomplete"));
}

void llm_parse_nonstream(const ModelCfg *m, const char *body, int len,
                         const LlmEvents *ev, void *ctx)
{
    LlmSse st;
    JVal *root;
    char jerr[128];

    llm_sse_reset(&st);
    if (!ev)
        return;
    if (!body || len == 0) {
        fire_error(&st, ev, ctx, "empty response body");
        return;
    }
    if (len < 0)
        len = (int)strlen(body);
    root = json_parse(body, len, jerr, (int)sizeof(jerr));
    if (!root) {
        fire_error(&st, ev, ctx, "invalid response JSON");
        return;
    }
    if (api_is(m, "anthropic"))
        ns_anthropic(&st, root, ev, ctx);
    else if (api_is(m, "chat"))
        ns_chat(&st, root, ev, ctx);
    else if (api_is(m, "responses"))
        ns_responses(&st, root, ev, ctx);
    else
        fire_error(&st, ev, ctx, "unknown api");
    json_free(root);
}

/* --- non-2xx error body extraction ------------------------------------------ */

int llm_extract_error(const char *body, int len, char *msg, int cap)
{
    JVal *root, *e;
    const char *s;
    char jerr[128];
    int ok = -1;

    if (!body || !msg || cap <= 0)
        return -1;
    if (len < 0)
        len = (int)strlen(body);
    root = json_parse(body, len, jerr, (int)sizeof(jerr));
    if (!root)
        return -1;
    e = json_obj_get(root, "error");
    if (e) {
        if (json_type(e) == JSTR) {
            s = json_str(e);
            ok = 0;
        } else {
            s = json_str(json_obj_get(e, "message"));
            if (*s)
                ok = 0;
        }
    }
    if (ok == 0) {
        strncpy(msg, s, (size_t)cap - 1);
        msg[cap - 1] = '\0';
    }
    json_free(root);
    return ok;
}

/* --- llm_send: the full pipeline --------------------------------------------- */

#define LLM_BODY_CAP  (512L * 1024L)      /* request body build cap */
#define LLM_RESP_CAP  (16L * 1024L * 1024L) /* response accumulator cap */

/* transport adapters: IoSend/IoRecv over NetConn / TlsConn */
static int llm_net_send(void *io, const void *d, int n, int t)
{
    return net_send((NetConn *)io, d, n, t);
}
static int llm_net_recv(void *io, void *b, int c, int t)
{
    return net_recv((NetConn *)io, b, c, t);
}
static int llm_tls_send(void *io, const void *d, int n, int t)
{
    return tls_send((TlsConn *)io, d, n, t);
}
static int llm_tls_recv(void *io, void *b, int c, int t)
{
    return tls_recv((TlsConn *)io, b, c, t);
}

typedef struct {          /* stream read context for http_read_body2 */
    const ModelCfg *m;
    const LlmEvents *ev;
    void *ctx;
    LlmSse *st;
} SseSink;

static void sse_sink(void *p, const char *data, int len, int last)
{
    SseSink *sk = p;

    if (len > 0)
        llm_feed_sse(sk->m, data, len, sk->st, sk->ev, sk->ctx);
    if (last && !sk->st->done)
        fire_done(sk->st, sk->ev, sk->ctx, 0);  /* EOF without a protocol
                                                   finish event */
}

typedef struct {          /* growable accumulator for non-stream bodies */
    char *p;
    long n;
    long cap;
} BodyBuf;

static void body_sink(void *p, const char *data, int len, int last)
{
    BodyBuf *bb = p;
    char *np;
    long ncap;

    (void)last;
    if (len <= 0 || bb->n + len > LLM_RESP_CAP)
        return;
    if (bb->n + len > bb->cap) {
        ncap = bb->cap > 0 ? bb->cap : 8192;
        while (ncap < bb->n + len)
            ncap *= 2;
        np = (char *)realloc(bb->p, (size_t)ncap);
        if (!np)
            return;
        bb->p = np;
        bb->cap = ncap;
    }
    memcpy(bb->p + bb->n, data, (size_t)len);
    bb->n += len;
}

int llm_send(const ModelCfg *m, const ProxyCfg *proxy, const Session *s,
             const LlmEvents *ev, void *ctx,
             const volatile int *cancel, char *err, int errcap)
{
    char host[256];
    unsigned short port;
    char urlpath[1024];
    char fullpath[1200];
    char hdrbuf[1024];
    const char *hdrs[8];
    int nh;
    char *body;
    int body_len;
    HttpResult res;
    NetConn *nc;
    TlsConn *tc;
    void *io;
    IoSend iosend;
    IoRecv iorecv;
    LlmSse st;
    int use_tls;
    int rd;
    int rc;

    if (err && errcap > 0)
        err[0] = '\0';
    llm_sse_reset(&st);
    if (!m || !s || !ev || !err || errcap <= 0) {
        set_err(err, errcap, "bad argument");
        return -1;
    }
    nc = NULL;
    tc = NULL;
    body = NULL;
    rc = -1;

    /* url -> host/port/path (+tls flag) */
    if (net_parse_url(m->base_url, host, (int)sizeof(host), &port,
                      urlpath, (int)sizeof(urlpath), &use_tls) != 0) {
        set_err(err, errcap, "invalid base_url");
        fire_error(&st, ev, ctx, err);
        return -1;
    }
    {
        const char *ap = llm_api_path(m);

        if (ap[0] == '\0') {
            set_err(err, errcap, "unknown api");
            fire_error(&st, ev, ctx, err);
            return -1;
        }
        /* base_url path prefix joined with the protocol endpoint path
         * (handles "/v1" prefixes and full endpoint URLs) */
        if (llm_compose_path(urlpath, ap, fullpath,
                             (int)sizeof(fullpath)) != 0) {
            set_err(err, errcap, "base_url path too long");
            fire_error(&st, ev, ctx, err);
            return -1;
        }
    }

    /* request body */
    body = (char *)malloc((size_t)LLM_BODY_CAP);
    if (!body) {
        set_err(err, errcap, "out of memory");
        fire_error(&st, ev, ctx, err);
        return -1;
    }
    if (llm_build_request(m, s, body, (int)LLM_BODY_CAP, err, errcap) != 0) {
        fire_error(&st, ev, ctx, err);
        free(body);
        return -1;
    }
    body_len = (int)strlen(body);

    /* headers: Host + the auth block, split at CRLF for the {"K: V",
     * NULL} list http_send_request2 takes */
    nh = 0;
    if ((use_tls && port != 443) || (!use_tls && port != 80))
        snprintf(hdrbuf, sizeof(hdrbuf), "Host: %s:%u", host,
                 (unsigned)port);
    else
        snprintf(hdrbuf, sizeof(hdrbuf), "Host: %s", host);
    hdrs[nh++] = hdrbuf;
    {
        char *p = hdrbuf + strlen(hdrbuf) + 1;
        int n = llm_build_headers(m, p,
                                  (int)sizeof(hdrbuf) - (int)(p - hdrbuf));
        int left = (int)sizeof(hdrbuf) - (int)(p - hdrbuf);

        if (n < 0 || n >= left) {
            set_err(err, errcap, "request headers too long");
            fire_error(&st, ev, ctx, err);
            free(body);
            return -1;
        }
        for (;;) {
            char *cr = strstr(p, "\r\n");

            if (cr)
                *cr = '\0';
            if (nh < (int)(sizeof(hdrs) / sizeof(hdrs[0])) - 1)
                hdrs[nh++] = p;
            if (!cr)
                break;
            p = cr + 2;
            if (*p == '\0')
                break;
        }
    }
    hdrs[nh] = NULL;

    /* connect (proxy CONNECT tunnel when m->use_proxy && proxy->host) */
    if (net_connect(&nc, host, port, NET_TIMEOUT_CONNECT_MS, proxy,
                    m->use_proxy, err, errcap) != 0) {
        fire_error(&st, ev, ctx, err);
        free(body);
        return -1;
    }
    if (use_tls) {
        if (tls_connect(&tc, nc, host, NULL, m->verify_tls,
                        err, errcap) != 0) {
            fire_error(&st, ev, ctx, err);
            net_close(nc);
            free(body);
            return -1;
        }
        io = tc;
        iosend = llm_tls_send;
        iorecv = llm_tls_recv;
    } else {
        io = nc;
        iosend = llm_net_send;
        iorecv = llm_net_recv;
    }

    if (http_send_request2(io, iosend, iorecv, "POST", fullpath, hdrs,
                           body, body_len, &res, err, errcap) != 0) {
        fire_error(&st, ev, ctx, err);
        goto out;
    }

    if (res.status >= 200 && res.status < 300) {
        if (m->stream) {
            SseSink sk;

            sk.m = m;
            sk.ev = ev;
            sk.ctx = ctx;
            sk.st = &st;
            rd = http_read_body2(io, iorecv, &res, cancel, sse_sink, &sk,
                                 err, errcap);
            if (rd == -2) {                 /* aborted by caller */
                fire_done(&st, ev, ctx, 2);
                rc = 0;
            } else if (rd == 0) {
                rc = 0;                     /* on_done fired in the sink */
            } else {
                if (!st.done)
                    fire_error(&st, ev, ctx, err);
            }
        } else {
            BodyBuf bb;

            bb.p = NULL;
            bb.n = 0;
            bb.cap = 0;
            rd = http_read_body2(io, iorecv, &res, cancel, body_sink, &bb,
                                 err, errcap);
            if (rd == -2) {
                fire_done(&st, ev, ctx, 2);
                rc = 0;
            } else if (rd == 0) {
                llm_parse_nonstream(m, bb.p, (int)bb.n, ev, ctx);
                rc = 0;
            } else {
                if (!st.done)
                    fire_error(&st, ev, ctx, err);
            }
            free(bb.p);
        }
    } else {
        /* non-2xx: drain the body and mine it for an error message */
        BodyBuf bb;
        char msg[512];

        bb.p = NULL;
        bb.n = 0;
        bb.cap = 0;
        rd = http_read_body2(io, iorecv, &res, cancel, body_sink, &bb,
                             err, errcap);
        if (rd == -2) {
            fire_done(&st, ev, ctx, 2);
            free(bb.p);
            rc = 0;
            goto out;
        }
        if (rd == 0 && bb.p
            && llm_extract_error(bb.p, (int)bb.n, msg,
                                 (int)sizeof(msg)) == 0) {
            set_errf(err, errcap, "HTTP %d: %s", res.status, msg);
            fire_error(&st, ev, ctx, msg);
        } else {
            set_errf(err, errcap, "HTTP %d", res.status);
            fire_error(&st, ev, ctx, err);
        }
        free(bb.p);
    }

out:
    if (tc)
        tls_close(tc);
    net_close(nc);
    free(body);
    return rc;
}
