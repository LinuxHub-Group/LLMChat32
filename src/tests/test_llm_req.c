/*
 * test_llm_req.c - llm.c request construction unit tests (three protocols).
 *
 * Console app (built with -bc). Builds a fixed Session (system + two full
 * user/assistant rounds + a final user message mixing emoji and Chinese),
 * runs llm_build_request for each of the three wire protocols, parses the
 * emitted JSON with json_parse and verifies every field with json_obj_get:
 *
 *   1: anthropic - path /v1/messages; model/max_tokens top level; "system"
 *      as its own field; messages[] holds no system entry; thinking block
 *      of the assistant message is NOT sent; image block goes out as the
 *      text "[图片] url"; emoji/UTF-8 content survives byte-for-byte;
 *      thinking:{type:"enabled",budget_tokens} with the effort mapping
 *      low=2000/medium=8000/high=32000; stream:true.
 *   2: chat - path /v1/chat/completions; reasoning_effort at top level
 *      when reasoning=1 (absent when reasoning=0); temperature present
 *      only when >= 0; system travels as a role:"system" messages entry;
 *      stream:true.
 *   3: responses - path /v1/responses; reasoning:{effort}; the message
 *      list lives under "input" as an array whose first entry is system.
 *   4: truncation - llm_truncate_context drops the oldest user/assistant
 *      messages until the window fits max_context_chars (1 token ~= 3.5
 *      chars, estimate = char count of text blocks); system is never
 *      counted and survives in the built request; max_context_chars<=0
 *      means unlimited (first_msg_idx=0).
 *   5: llm_build_headers - anthropic gets "x-api-key" plus
 *      "anthropic-version: 2023-06-01"; chat/responses get
 *      "Authorization: Bearer <key>"; overflow is reported.
 *   6: errors - unknown api and a too-small buffer both fail with a
 *      non-empty err.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_llm_req.exe ^
 *          src\tests\test_llm_req.c src\core\llm.c src\core\json.c ^
 *          src\core\session.c src\core\utf.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/llm.h"
#include "../core/session.h"
#include "../core/json.h"

static int g_fail = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (cond) {                                                  \
            printf("ok %s\n", #cond);                                \
        } else {                                                     \
            printf("FAIL %s (line %d)\n", #cond, __LINE__);          \
            g_fail++;                                                \
        }                                                            \
    } while (0)

/* --- fixtures ----------------------------------------------------------- */

/*
 * system
 * user      "你好，world!"                                    (15 chars)
 * assistant "Hi there." + think "hmm secret"                  (9 sent)
 * user      "第二个问题 🚀" + image http://img.example/x.png  (20 + [图片] line)
 * assistant "答案二。"                                       (12 chars)
 * user      "final 问题 😀 mix 文本"                          (28 chars)
 * text-block char total (unlimited) = 15+9+20+12+28 = 84
 */
static Session *build_fixture(void)
{
    Session *s = session_new("t10");
    Msg *m;

    m = session_add_msg(s, ROLE_SYSTEM);
    msg_add_block(m, BLOCK_TEXT, "You are a helpful assistant.");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "你好，world!");
    m = session_add_msg(s, ROLE_ASSIST);
    msg_add_block(m, BLOCK_TEXT, "Hi there.");
    msg_add_block(m, BLOCK_THINK, "hmm secret thinking");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "第二个问题 🚀");
    msg_add_block(m, BLOCK_IMAGE, "http://img.example/x.png");
    m = session_add_msg(s, ROLE_ASSIST);
    msg_add_block(m, BLOCK_TEXT, "答案二。");
    m = session_add_msg(s, ROLE_USER);
    msg_add_block(m, BLOCK_TEXT, "final 问题 😀 mix 文本");
    return s;
}

static void make_model(ModelCfg *m, const char *api)
{
    memset(m, 0, sizeof *m);
    strcpy(m->name, "T");
    strcpy(m->api, api);
    strcpy(m->base_url, "https://api.example.com");
    strcpy(m->api_key, "key-12345");
    strcpy(m->model_id, "test-model-1");
    m->stream = 1;
    m->reasoning = 1;
    strcpy(m->reasoning_effort, "high");
    m->thinking_budget = 8000;
    m->max_tokens = 4096;
    m->max_context_chars = 0;   /* unlimited */
    m->temperature = -1.0;      /* <0: do not send */
}

/* build + parse; returns NULL (and frees) on failure */
static JVal *build_parse(const ModelCfg *m, const Session *s,
                         char *buf, int cap, char *err, int errcap)
{
    char perr[128];

    if (llm_build_request(m, s, buf, cap, err, errcap) != 0)
        return NULL;
    return json_parse(buf, -1, perr, (int)sizeof(perr));
}

int main(void)
{
    static char buf[16384];
    static char hdrs[1024];
    char err[256];
    Session *s;
    ModelCfg m;
    JVal *root;
    JVal *v;
    JVal *msgs;
    JVal *e;
    int i;
    int rc;
    int first;
    long est;

    /* --- 1: anthropic -------------------------------------------------- */
    s = build_fixture();
    make_model(&m, "anthropic");

    CHECK(llm_api_path(&m) != NULL
          && strcmp(llm_api_path(&m), "/v1/messages") == 0);

    err[0] = '\0';
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    CHECK(err[0] == '\0');

    if (root != NULL) {
        CHECK(strcmp(json_str(json_obj_get(root, "model")),
                     "test-model-1") == 0);
        CHECK(json_num(json_obj_get(root, "max_tokens")) == 4096.0);
        CHECK(strcmp(json_str(json_obj_get(root, "system")),
                     "You are a helpful assistant.") == 0);
        CHECK(json_bool(json_obj_get(root, "stream")) == 1);
        CHECK(json_obj_get(root, "temperature") == NULL);

        v = json_obj_get(root, "thinking");
        CHECK(v != NULL);
        CHECK(strcmp(json_str(json_obj_get(v, "type")), "enabled") == 0);
        CHECK(json_num(json_obj_get(v, "budget_tokens")) == 32000.0);

        msgs = json_obj_get(root, "messages");
        CHECK(json_type(msgs) == JARR);
        CHECK(json_arr_len(msgs) == 5);

        /* no system role anywhere inside messages[] */
        for (i = 0; i < json_arr_len(msgs); i++) {
            e = json_arr_get(msgs, i);
            if (strcmp(json_str(json_obj_get(e, "role")), "system") == 0)
                break;
        }
        CHECK(i == json_arr_len(msgs));

        /* roles alternate user/assistant */
        e = json_arr_get(msgs, 0);
        CHECK(strcmp(json_str(json_obj_get(e, "role")), "user") == 0);

        /* thinking block is not sent */
        e = json_arr_get(msgs, 1);
        CHECK(strcmp(json_str(json_obj_get(e, "content")),
                     "Hi there.") == 0);

        /* image block serialized as "[图片] url" */
        e = json_arr_get(msgs, 2);
        CHECK(strstr(json_str(json_obj_get(e, "content")),
                     "[图片] http://img.example/x.png") != NULL);

        /* emoji + Chinese survive byte-for-byte */
        e = json_arr_get(msgs, 4);
        CHECK(strcmp(json_str(json_obj_get(e, "content")),
                     "final 问题 😀 mix 文本") == 0);

        json_free(root);
    }

    /* effort -> budget mapping: medium=8000, low=2000 */
    strcpy(m.reasoning_effort, "medium");
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_num(json_obj_get(json_obj_get(root, "thinking"),
                                    "budget_tokens")) == 8000.0);
        json_free(root);
    }
    strcpy(m.reasoning_effort, "low");
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_num(json_obj_get(json_obj_get(root, "thinking"),
                                    "budget_tokens")) == 2000.0);
        json_free(root);
    }
    strcpy(m.reasoning_effort, "high");

    /* reasoning=0 -> no thinking key at all */
    m.reasoning = 0;
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_obj_get(root, "thinking") == NULL);
        json_free(root);
    }
    m.reasoning = 1;

    /* temperature >= 0 is sent */
    m.temperature = 0.5;
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_num(json_obj_get(root, "temperature")) == 0.5);
        json_free(root);
    }
    m.temperature = -1.0;

    /* --- 2: chat -------------------------------------------------------- */
    make_model(&m, "chat");
    CHECK(llm_api_path(&m) != NULL
          && strcmp(llm_api_path(&m), "/v1/chat/completions") == 0);

    err[0] = '\0';
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(strcmp(json_str(json_obj_get(root, "model")),
                     "test-model-1") == 0);
        CHECK(json_num(json_obj_get(root, "max_tokens")) == 4096.0);
        CHECK(json_bool(json_obj_get(root, "stream")) == 1);
        CHECK(strcmp(json_str(json_obj_get(root, "reasoning_effort")),
                     "high") == 0);
        CHECK(json_obj_get(root, "temperature") == NULL);

        msgs = json_obj_get(root, "messages");
        CHECK(json_type(msgs) == JARR);
        CHECK(json_arr_len(msgs) == 6);

        /* system rides inside messages[] as role:"system" */
        e = json_arr_get(msgs, 0);
        CHECK(strcmp(json_str(json_obj_get(e, "role")), "system") == 0);
        CHECK(strcmp(json_str(json_obj_get(e, "content")),
                     "You are a helpful assistant.") == 0);

        json_free(root);
    }

    /* reasoning=0 -> no reasoning_effort key */
    m.reasoning = 0;
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_obj_get(root, "reasoning_effort") == NULL);
        json_free(root);
    }
    m.reasoning = 1;

    /* temperature >= 0 -> present */
    m.temperature = 0.5;
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_num(json_obj_get(root, "temperature")) == 0.5);
        json_free(root);
    }
    m.temperature = -1.0;

    /* stream=false -> stream:false */
    m.stream = 0;
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(json_obj_get(root, "stream") != NULL
              && json_bool(json_obj_get(root, "stream")) == 0);
        json_free(root);
    }
    m.stream = 1;

    /* --- 3: responses ---------------------------------------------------- */
    make_model(&m, "responses");
    strcpy(m.reasoning_effort, "medium");
    CHECK(llm_api_path(&m) != NULL
          && strcmp(llm_api_path(&m), "/v1/responses") == 0);

    err[0] = '\0';
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(strcmp(json_str(json_obj_get(root, "model")),
                     "test-model-1") == 0);
        CHECK(json_bool(json_obj_get(root, "stream")) == 1);

        v = json_obj_get(root, "reasoning");
        CHECK(v != NULL);
        CHECK(strcmp(json_str(json_obj_get(v, "effort")), "medium") == 0);

        msgs = json_obj_get(root, "input");
        CHECK(json_type(msgs) == JARR);
        CHECK(json_arr_len(msgs) == 6);
        e = json_arr_get(msgs, 0);
        CHECK(strcmp(json_str(json_obj_get(e, "role")), "system") == 0);

        json_free(root);
    }

    /* --- 4: context truncation ------------------------------------------- */
    make_model(&m, "anthropic");
    m.max_context_chars = 50;   /* drops u1(15), a1(9), u2(20); keeps 40 */

    first = -1;
    est = -1;
    llm_truncate_context(&m, s, &first, &est);
    CHECK(first > 0);
    CHECK(first == 4);
    CHECK(est == 40);
    CHECK(est <= 50);

    /* system is never dropped: it still goes out, full text */
    err[0] = '\0';
    root = build_parse(&m, s, buf, (int)sizeof(buf), err, (int)sizeof(err));
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(strcmp(json_str(json_obj_get(root, "system")),
                     "You are a helpful assistant.") == 0);
        msgs = json_obj_get(root, "messages");
        CHECK(json_type(msgs) == JARR);
        CHECK(json_arr_len(msgs) == 2);
        e = json_arr_get(msgs, 0);
        CHECK(strcmp(json_str(json_obj_get(e, "content")), "答案二。") == 0);
        json_free(root);
    }

    /* unlimited when <= 0 */
    m.max_context_chars = 0;
    first = -1;
    est = -1;
    llm_truncate_context(&m, s, &first, &est);
    CHECK(first == 0);
    CHECK(est == 84);

    m.max_context_chars = -5;   /* any non-positive value: unlimited */
    first = -1;
    est = -1;
    llm_truncate_context(&m, s, &first, &est);
    CHECK(first == 0);
    CHECK(est == 84);
    m.max_context_chars = 0;

    /* --- 5: auth headers ------------------------------------------------- */
    make_model(&m, "anthropic");
    rc = llm_build_headers(&m, hdrs, (int)sizeof(hdrs));
    CHECK(rc == (int)strlen(hdrs));
    CHECK(strstr(hdrs, "x-api-key: key-12345") != NULL);
    CHECK(strstr(hdrs, "anthropic-version: 2023-06-01") != NULL);
    CHECK(strstr(hdrs, "Authorization") == NULL);

    make_model(&m, "chat");
    rc = llm_build_headers(&m, hdrs, (int)sizeof(hdrs));
    CHECK(rc == (int)strlen(hdrs));
    CHECK(strstr(hdrs, "Authorization: Bearer key-12345") != NULL);
    CHECK(strstr(hdrs, "x-api-key") == NULL);

    make_model(&m, "responses");
    rc = llm_build_headers(&m, hdrs, (int)sizeof(hdrs));
    CHECK(rc == (int)strlen(hdrs));
    CHECK(strstr(hdrs, "Authorization: Bearer key-12345") != NULL);

    /* overflow is reported, buffer stays NUL-terminated */
    hdrs[0] = 'x';
    rc = llm_build_headers(&m, hdrs, 8);
    CHECK(rc == -1);

    /* --- 6: error paths --------------------------------------------------- */
    make_model(&m, "bogus");
    err[0] = '\0';
    rc = llm_build_request(&m, s, buf, (int)sizeof(buf), err,
                           (int)sizeof(err));
    CHECK(rc != 0);
    CHECK(err[0] != '\0');

    make_model(&m, "anthropic");
    err[0] = '\0';
    rc = llm_build_request(&m, s, buf, 64, err, (int)sizeof(err));
    CHECK(rc != 0);
    CHECK(err[0] != '\0');

    session_free(s);

    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d checks failed)\n", g_fail);
    return 1;
}
