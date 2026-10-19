/*
 * test_llm_stream.c - llm.c response parsing unit tests (SSE + non-stream).
 *
 * Console app (built with -bc). Feeds canned SSE byte streams and full
 * JSON bodies to the parse layer with NO network:
 *
 *   1: anthropic stream - content_block_delta thinking_delta -> on_think,
 *      text_delta -> on_text, message_delta stop_reason=end_turn ->
 *      on_done(0); max_tokens -> on_done(1). Stream fed whole, in two
 *      halves (mid-event split) and byte-at-a-time; results identical.
 *   2: chat stream - choices[0].delta.reasoning_content -> on_think,
 *      content -> on_text, finish_reason:"stop" then data:[DONE] ->
 *      on_done(0) exactly once; finish_reason:"length" -> on_done(1).
 *   3: responses stream - response.output_text.delta -> on_text,
 *      response.reasoning_summary_text.delta -> on_think,
 *      response.completed -> on_done(0).
 *   4: non-stream - one full JSON body per protocol -> exactly one
 *      on_text (plus on_think for thinking content) + on_done(finish).
 *   5: error extraction - {"error":{"message":"..."}} bodies (anthropic
 *      and openai shapes, plus the string form) -> llm_extract_error
 *      pulls the message; garbage is rejected.
 *   6: byte fidelity - a UTF-8 emoji cut in half across two chunks
 *      arrives byte-identical (no character reassembly anywhere);
 *      byte-at-a-time feeding gives the same result.
 *   7: line overflow - one SSE line > 4096 bytes is dropped safely and
 *      the following events still parse.
 *   8: live (optional) - only when LLM_LIVE_URL and LLM_LIVE_KEY are set;
 *      otherwise prints a skip line. Not required for GREEN.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc $(WOLFSSL_OPTS) -fe=tests\test_llm_stream.exe ^
 *          src\tests\test_llm_stream.c src\core\llm.c src\core\net.c ^
 *          src\core\tls.c src\core\http.c src\core\json.c ^
 *          src\core\config.c src\core\session.c src\core\utf.c ^
 *          build\wolfssl.lib ws2_32.lib advapi32.lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "../core/llm.h"

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

/* --- recorder ------------------------------------------------------------- */

#define REC_CAP 65536

typedef struct {
    char text[REC_CAP];  long tlen;  int text_count;
    char think[REC_CAP]; long klen;  int think_count;
    int done_count; int last_finish;
    int err_count;  char err[1024];
} Rec;

static void rec_text(void *ctx, const char *frag, int len)
{
    Rec *r = ctx;

    if (r->tlen + len > REC_CAP - 1)
        len = (int)(REC_CAP - 1 - r->tlen);
    if (len > 0) {
        memcpy(r->text + r->tlen, frag, (size_t)len);
        r->tlen += len;
        r->text[r->tlen] = '\0';
        r->text_count++;
    }
}

static void rec_think(void *ctx, const char *frag, int len)
{
    Rec *r = ctx;

    if (r->klen + len > REC_CAP - 1)
        len = (int)(REC_CAP - 1 - r->klen);
    if (len > 0) {
        memcpy(r->think + r->klen, frag, (size_t)len);
        r->klen += len;
        r->think[r->klen] = '\0';
        r->think_count++;
    }
}

static void rec_done(void *ctx, int finish)
{
    Rec *r = ctx;

    r->done_count++;
    r->last_finish = finish;
}

static void rec_error(void *ctx, const char *msg)
{
    Rec *r = ctx;

    r->err_count++;
    strncpy(r->err, msg, sizeof(r->err) - 1);
    r->err[sizeof(r->err) - 1] = '\0';
}

static void rec_reset(Rec *r)
{
    memset(r, 0, sizeof *r);
}

/* --- feeding helpers ------------------------------------------------------- */

static void feed_all(const ModelCfg *m, LlmSse *st, Rec *r, const char *sse)
{
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };

    llm_feed_sse(m, sse, (int)strlen(sse), st, &ev, r);
}

/* feed in two halves; split lands mid-event (strlen/2) */
static void feed_halves(const ModelCfg *m, LlmSse *st, Rec *r, const char *sse)
{
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };
    int n = (int)strlen(sse);

    llm_feed_sse(m, sse, n / 2, st, &ev, r);
    llm_feed_sse(m, sse + n / 2, n - n / 2, st, &ev, r);
}

/* feed one byte at a time */
static void feed_bytes(const ModelCfg *m, LlmSse *st, Rec *r, const char *sse)
{
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };
    int i, n = (int)strlen(sse);

    for (i = 0; i < n; i++)
        llm_feed_sse(m, sse + i, 1, st, &ev, r);
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
    strcpy(m->reasoning_effort, "medium");
    m->thinking_budget = 0;
    m->max_tokens = 4096;
    m->max_context_chars = 0;
    m->temperature = -1.0;
    m->use_proxy = 0;
    m->verify_tls = 1;
}

/* --- canned streams --------------------------------------------------------- */

static const char SSE_ANTHROPIC[] =
    "event: message_start\r\n"
    "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\"}}\r\n"
    "\r\n"
    "event: content_block_start\r\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,"
    "\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\r\n"
    "\r\n"
    "event: content_block_delta\r\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"Let me \"}}\r\n"
    "\r\n"
    "event: content_block_delta\r\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"think.\"}}\r\n"
    "\r\n"
    "event: content_block_stop\r\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\r\n"
    "\r\n"
    "event: content_block_start\r\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,"
    "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\r\n"
    "\r\n"
    "event: content_block_delta\r\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\r\n"
    "\r\n"
    "event: content_block_delta\r\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\" world!\"}}\r\n"
    "\r\n"
    "event: content_block_stop\r\n"
    "data: {\"type\":\"content_block_stop\",\"index\":1}\r\n"
    "\r\n"
    "event: message_delta\r\n"
    "data: {\"type\":\"message_delta\",\"delta\":"
    "{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},"
    "\"usage\":{\"output_tokens\":10}}\r\n"
    "\r\n"
    "event: message_stop\r\n"
    "data: {\"type\":\"message_stop\"}\r\n"
    "\r\n";

static const char SSE_ANTHROPIC_MAX[] =
    "event: content_block_delta\r\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"short\"}}\r\n"
    "\r\n"
    "event: message_delta\r\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\"}}\r\n"
    "\r\n";

static const char SSE_CHAT[] =
    "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hmm\"},\"index\":0}]}\r\n"
    "\r\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"},\"index\":0}]}\r\n"
    "\r\n"
    ": keepalive comment line\r\n"
    "\r\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"!\"},\"index\":0}]}\r\n"
    "\r\n"
    "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\",\"index\":0}]}\r\n"
    "\r\n"
    "data: [DONE]\r\n"
    "\r\n";

static const char SSE_CHAT_LENGTH[] =
    "data: {\"choices\":[{\"delta\":{\"content\":\"cut\"},\"index\":0}]}\r\n"
    "\r\n"
    "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\",\"index\":0}]}\r\n"
    "\r\n";

/* one bare-LF event mixed in: the parser must tolerate LF line ends */
static const char SSE_RESPONSES[] =
    "data: {\"type\":\"response.reasoning_summary_text.delta\",\"delta\":\"think\","
    "\"item_id\":\"rs_1\",\"output_index\":0,\"summary_index\":0}\r\n"
    "\r\n"
    "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hel\","
    "\"item_id\":\"msg_1\",\"output_index\":0,\"content_index\":0}\n"
    "\n"
    "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\r\n"
    "\r\n";

/* --- canned non-stream bodies ------------------------------------------------ */

static const char BODY_ANTHROPIC[] =
    "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\","
    "\"model\":\"test-model-1\","
    "\"content\":[{\"type\":\"thinking\",\"thinking\":\"deep\"},"
    "{\"type\":\"text\",\"text\":\"Answer\"}],"
    "\"stop_reason\":\"end_turn\",\"usage\":{\"input_tokens\":5}}";

static const char BODY_CHAT[] =
    "{\"id\":\"chatcmpl_1\",\"choices\":[{\"message\":"
    "{\"role\":\"assistant\",\"reasoning_content\":\"deep\",\"content\":\"Answer\"},"
    "\"finish_reason\":\"stop\",\"index\":0}],\"usage\":{}}";

static const char BODY_CHAT_LENGTH[] =
    "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Cut\"},"
    "\"finish_reason\":\"length\",\"index\":0}],\"usage\":{}}";

static const char BODY_RESPONSES[] =
    "{\"status\":\"completed\",\"id\":\"resp_1\","
    "\"output\":[{\"type\":\"reasoning\",\"id\":\"rs_1\","
    "\"summary\":[{\"type\":\"summary_text\",\"text\":\"deep\"}]},"
    "{\"type\":\"message\",\"id\":\"msg_1\",\"role\":\"assistant\","
    "\"content\":[{\"type\":\"output_text\",\"text\":\"Answer\"}]}],"
    "\"usage\":{}}";

/* --- canned error bodies ----------------------------------------------------- */

static const char ERR_ANTHROPIC[] =
    "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\","
    "\"message\":\"invalid key\"}}";

static const char ERR_OPENAI[] =
    "{\"error\":{\"message\":\"Incorrect API key\","
    "\"type\":\"authentication_error\",\"code\":\"invalid_api_key\"}}";

static const char ERR_STRING_FORM[] = "{\"error\":\"boom\"}";

/* --- scenarios --------------------------------------------------------------- */

static void test_anthropic_stream(void)
{
    ModelCfg m;
    LlmSse st;
    Rec r;

    make_model(&m, "anthropic");

    /* whole */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, SSE_ANTHROPIC);
    CHECK(strcmp(r.think, "Let me think.") == 0);
    CHECK(strcmp(r.text, "Hello world!") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);
    CHECK(r.err_count == 0);

    /* halves (mid-event split) */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_halves(&m, &st, &r, SSE_ANTHROPIC);
    CHECK(strcmp(r.think, "Let me think.") == 0);
    CHECK(strcmp(r.text, "Hello world!") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* byte at a time */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_bytes(&m, &st, &r, SSE_ANTHROPIC);
    CHECK(strcmp(r.think, "Let me think.") == 0);
    CHECK(strcmp(r.text, "Hello world!") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* max_tokens -> finish 1 */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, SSE_ANTHROPIC_MAX);
    CHECK(strcmp(r.text, "short") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 1);
    CHECK(r.err_count == 0);
}

static void test_chat_stream(void)
{
    ModelCfg m;
    LlmSse st;
    Rec r;

    make_model(&m, "chat");

    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, SSE_CHAT);
    CHECK(strcmp(r.think, "hmm") == 0);
    CHECK(strcmp(r.text, "Hi!") == 0);
    /* finish_reason fired done(0); the trailing [DONE] must not re-fire */
    CHECK(r.done_count == 1 && r.last_finish == 0);
    CHECK(r.err_count == 0);

    /* halves */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_halves(&m, &st, &r, SSE_CHAT);
    CHECK(strcmp(r.think, "hmm") == 0);
    CHECK(strcmp(r.text, "Hi!") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* finish_reason "length" -> done(1), no [DONE] line */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, SSE_CHAT_LENGTH);
    CHECK(strcmp(r.text, "cut") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 1);
}

static void test_responses_stream(void)
{
    ModelCfg m;
    LlmSse st;
    Rec r;

    make_model(&m, "responses");

    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, SSE_RESPONSES);
    CHECK(strcmp(r.think, "think") == 0);
    CHECK(strcmp(r.text, "Hel") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);
    CHECK(r.err_count == 0);
}

static void test_nonstream(void)
{
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };
    ModelCfg m;
    Rec r;

    make_model(&m, "anthropic");
    m.stream = 0;
    rec_reset(&r);
    llm_parse_nonstream(&m, BODY_ANTHROPIC, (int)strlen(BODY_ANTHROPIC),
                        &ev, &r);
    CHECK(r.text_count == 1 && strcmp(r.text, "Answer") == 0);
    CHECK(r.think_count == 1 && strcmp(r.think, "deep") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);
    CHECK(r.err_count == 0);

    make_model(&m, "chat");
    m.stream = 0;
    rec_reset(&r);
    llm_parse_nonstream(&m, BODY_CHAT, (int)strlen(BODY_CHAT), &ev, &r);
    CHECK(r.text_count == 1 && strcmp(r.text, "Answer") == 0);
    CHECK(r.think_count == 1 && strcmp(r.think, "deep") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* chat finish_reason "length" -> done(1) */
    rec_reset(&r);
    llm_parse_nonstream(&m, BODY_CHAT_LENGTH, (int)strlen(BODY_CHAT_LENGTH),
                        &ev, &r);
    CHECK(r.text_count == 1 && strcmp(r.text, "Cut") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 1);

    make_model(&m, "responses");
    m.stream = 0;
    rec_reset(&r);
    llm_parse_nonstream(&m, BODY_RESPONSES, (int)strlen(BODY_RESPONSES),
                        &ev, &r);
    CHECK(r.text_count == 1 && strcmp(r.text, "Answer") == 0);
    CHECK(r.think_count == 1 && strcmp(r.think, "deep") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* garbage body -> on_error, no on_done */
    rec_reset(&r);
    llm_parse_nonstream(&m, "not json", 8, &ev, &r);
    CHECK(r.err_count == 1);
    CHECK(r.done_count == 0);
}

static void test_error_extraction(void)
{
    char msg[256];

    CHECK(llm_extract_error(ERR_ANTHROPIC, (int)strlen(ERR_ANTHROPIC),
                            msg, (int)sizeof(msg)) == 0);
    CHECK(strcmp(msg, "invalid key") == 0);

    CHECK(llm_extract_error(ERR_OPENAI, (int)strlen(ERR_OPENAI),
                            msg, (int)sizeof(msg)) == 0);
    CHECK(strcmp(msg, "Incorrect API key") == 0);

    /* string form of "error" also works */
    CHECK(llm_extract_error(ERR_STRING_FORM, (int)strlen(ERR_STRING_FORM),
                            msg, (int)sizeof(msg)) == 0);
    CHECK(strcmp(msg, "boom") == 0);

    /* no error object / garbage -> non-zero, msg untouched meaningfully */
    CHECK(llm_extract_error("{\"foo\":1}", 9, msg, (int)sizeof(msg)) != 0);
    CHECK(llm_extract_error("not json", 8, msg, (int)sizeof(msg)) != 0);
}

static void test_byte_fidelity(void)
{
    /* raw UTF-8 (incl. the 4-byte rocket emoji) inside the JSON strings;
       the emoji bytes are cut in half across the two chunks */
    static const char sse[] =
        "data: {\"choices\":[{\"delta\":{\"content\":\"\xE4\xBD\xA0\xE5\xA5\xBD\"},\"index\":0}]}\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" \xF0\x9F\x9A\x80!\"},\"index\":0}]}\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\",\"index\":0}]}\r\n"
        "\r\n";
    static const char expect[] = "\xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x9A\x80!";
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };
    ModelCfg m;
    LlmSse st;
    Rec r;
    const char *em;
    int off, n;

    make_model(&m, "chat");

    /* split inside the emoji (after its first 2 bytes) */
    em = strstr(sse, "\xF0\x9F\x9A\x80");
    CHECK(em != NULL);
    off = (int)(em - sse) + 2;
    n = (int)strlen(sse);
    rec_reset(&r);
    llm_sse_reset(&st);
    llm_feed_sse(&m, sse, off, &st, &ev, &r);
    llm_feed_sse(&m, sse + off, n - off, &st, &ev, &r);
    CHECK(r.tlen == (long)strlen(expect));
    CHECK(memcmp(r.text, expect, strlen(expect)) == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);

    /* byte-at-a-time gives byte-identical output too */
    rec_reset(&r);
    llm_sse_reset(&st);
    feed_bytes(&m, &st, &r, sse);
    CHECK(r.tlen == (long)strlen(expect));
    CHECK(memcmp(r.text, expect, strlen(expect)) == 0);
}

static void test_line_overflow(void)
{
    /* one data line of 5000 'x' bytes (> 4096) must be dropped without
       desyncing the parser: the event after it still parses */
    static const char tail[] =
        "data: {\"choices\":[{\"delta\":{\"content\":\"tail\"},\"index\":0}]}\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\",\"index\":0}]}\r\n"
        "\r\n";
    char xs[5001];
    char big[6200];
    ModelCfg m;
    LlmSse st;
    Rec r;
    int n;

    memset(xs, 'x', 5000);
    xs[5000] = '\0';
    make_model(&m, "chat");
    n = sprintf(big,
                "data: {\"choices\":[{\"delta\":{\"content\":\"%s\"},\"index\":0}]}\r\n"
                "\r\n", xs);
    CHECK(n > 4096);
    memcpy(big + n, tail, strlen(tail) + 1);

    rec_reset(&r);
    llm_sse_reset(&st);
    feed_all(&m, &st, &r, big);
    CHECK(strcmp(r.text, "tail") == 0);
    CHECK(r.done_count == 1 && r.last_finish == 0);
    CHECK(r.err_count == 0);
}

static void test_live(void)
{
    const char *url = getenv("LLM_LIVE_URL");
    const char *key = getenv("LLM_LIVE_KEY");
    static const LlmEvents ev = { rec_text, rec_think, rec_done, rec_error };
    WSADATA wd;
    ModelCfg m;
    Session *s;
    Msg *msg;
    Rec r;
    char err[256];
    int rc;

    if (!url || !key || !*url || !*key) {
        printf("ok live scenario skipped (LLM_LIVE_URL/LLM_LIVE_KEY unset)\n");
        return;
    }
    if (WSAStartup(MAKEWORD(2, 0), &wd) != 0) {
        printf("FAIL WSAStartup failed\n");
        g_fail++;
        return;
    }

    make_model(&m, getenv("LLM_LIVE_API") ? getenv("LLM_LIVE_API") : "chat");
    strncpy(m.base_url, url, sizeof(m.base_url) - 1);
    m.base_url[sizeof(m.base_url) - 1] = '\0';
    strncpy(m.api_key, key, sizeof(m.api_key) - 1);
    m.api_key[sizeof(m.api_key) - 1] = '\0';
    if (getenv("LLM_LIVE_MODEL")) {
        strncpy(m.model_id, getenv("LLM_LIVE_MODEL"),
                sizeof(m.model_id) - 1);
        m.model_id[sizeof(m.model_id) - 1] = '\0';
    }

    s = session_new("live");
    msg = session_add_msg(s, ROLE_USER);
    msg_add_block(msg, BLOCK_TEXT, "Say hi in one word.");

    rec_reset(&r);
    err[0] = '\0';
    rc = llm_send(&m, NULL, s, &ev, &r, NULL, err, (int)sizeof(err));
    printf("ok live rc=%d text='%s' think='%s' done=%d/%d err='%s' '%s'\n",
           rc, r.text, r.think, r.last_finish, r.done_count, err, r.err);
    CHECK(rc == 0);
    CHECK(r.done_count == 1);
    CHECK(r.err_count == 0);

    session_free(s);
}

int main(void)
{
    test_anthropic_stream();
    test_chat_stream();
    test_responses_stream();
    test_nonstream();
    test_error_extraction();
    test_byte_fidelity();
    test_line_overflow();
    test_live();

    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d checks failed)\n", g_fail);
    return 1;
}
