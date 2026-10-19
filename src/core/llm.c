/*
 * llm.c - LLM request construction for the three wire protocols.
 *
 * Implementation of llm.h. Pure construction only: turns a Session into
 * the protocol-specific request body with the JB streaming writer, plus
 * auth-header and context-window helpers. No network, no parsing
 * (that is Task 11).
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llm.h"
#include "json.h"

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
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
