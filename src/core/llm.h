/*
 * llm.h - LLM request construction AND response parsing for the three
 * wire protocols, plus the full-pipeline llm_send.
 *
 * Construction (Task 10) is pure functions only: llm_api_path,
 * llm_build_request, llm_build_headers, llm_truncate_context turn a
 * Session into protocol-specific request JSON with the JB writer.
 *
 * Parsing (Task 11) decodes responses with NO network of its own:
 * llm_feed_sse drives a byte-at-a-time SSE line buffer and dispatches
 * per-protocol events to an LlmEvents callback set; llm_parse_nonstream
 * does the same for one full JSON body; llm_extract_error pulls the
 * message out of a non-2xx error body. These three are test-visible so
 * canned streams can be fed directly without sockets.
 *
 * llm_send orchestrates the whole wire path: url parse -> net_connect
 * (proxy per ModelCfg.use_proxy + global ProxyCfg) -> tls_connect when
 * https -> http_send_request2/read_body2 (headers via llm_build_headers,
 * body via llm_build_request) -> stream or non-stream parse with cancel
 * checks -> cleanup (tls_close, net_close). Text fragments are passed
 * through byte-identically; the parser never reassembles characters.
 *
 *
 * Protocol field matrix (spec section 6):
 *   anthropic : /v1/messages       {model, max_tokens, system (top-level
 *               field), thinking, temperature?, stream, messages[]}
 *   chat      : /v1/chat/completions {model, messages[] (system travels
 *               as role:"system" entries), max_tokens, reasoning_effort,
 *               temperature?, stream}
 *   responses : /v1/responses      {model, input[] (same shape as chat
 *               messages), max_tokens, reasoning:{effort},
 *               temperature?, stream}
 *
 * Content assembly (spec section 6): thinking blocks are never sent;
 * image blocks go out as the literal text "[图片] url" inside the
 * message content; multiple blocks of one message are joined with '\n'.
 *
 * reasoning_effort -> thinking budget_tokens mapping (anthropic):
 *   low = 2000, medium = 8000, high = 32000; an unknown/empty effort
 *   falls back to medium (8000). ModelCfg.thinking_budget > 0 overrides
 *   the mapped value (user-configured budget, spec section 7).
 * temperature is emitted only when ModelCfg.temperature >= 0.
 *
 * Context truncation (llm_truncate_context, spec section 6): estimate
 * is the byte count (strlen) of text blocks, 1 token ~= 3.5 chars. The
 * oldest user/assistant messages are dropped until the remaining
 * window fits max_context_chars; <= 0 means unlimited (every message
 * kept). System messages are never counted and never dropped - the
 * caller always sends them regardless of *first_msg_idx.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_LLM_H
#define LLMCHAT32_LLM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"
#include "session.h"

/* URL path of the protocol endpoint ("" for an unknown api). */
const char *llm_api_path(const ModelCfg *m);

/*
 * Build the request body JSON for model m from session s into buf.
 * Returns 0 and NUL-terminates buf on success; on any failure returns
 * non-zero with a short message in err (when errcap > 0). Reasons:
 * bad argument, unknown api, out of memory, or the buffer overflowing
 * (JB tracks overflow instead of growing).
 */
int llm_build_request(const ModelCfg *m, const Session *s,
                      char *buf, int cap, char *err, int errcap);

/*
 * Write the protocol auth headers for m into buf as CRLF-terminated
 * lines: anthropic gets "x-api-key: <key>" plus
 * "anthropic-version: 2023-06-01", every other api gets
 * "Authorization: Bearer <key>". Returns the number of bytes written
 * (excluding the NUL) or -1 when buf is invalid or too small.
 */
int llm_build_headers(const ModelCfg *m, char *buf, int cap);

/*
 * Context truncation per spec section 6. *first_msg_idx receives the
 * index of the oldest non-system message still within budget and
 * *est_chars the estimated character count of the kept window (text
 * blocks only). System messages are excluded from the estimate and
 * never dropped. max_context_chars <= 0 means unlimited.
 */
void llm_truncate_context(const ModelCfg *m, const Session *s,
                          int *first_msg_idx, long *est_chars);

/* --- response parsing (Task 11) ------------------------------------------ */

/* Callback set for parsed response events. finish: 0=stop 1=length
 * 2=aborted -1=error; on_error carries a short human-readable message. */
typedef struct {
  void (*on_text)(void *ctx, const char *frag, int len);
  void (*on_think)(void *ctx, const char *frag, int len);
  void (*on_done)(void *ctx, int finish);  /* 0=stop 1=length 2=aborted -1=error */
  void (*on_error)(void *ctx, const char *msg);
} LlmEvents;

#define LLM_SSE_LINE_MAX 4096   /* SSE 行内容上限：超限行整体丢弃，解析继续 */
#define LLM_SSE_DATA_MAX 16384  /* 单事件 data: 聚合上限：超出丢弃整个事件 */

/* SSE 行缓冲器状态（栈分配即可）。字节透传：不做任何字符重组。 */
typedef struct {
  char line[LLM_SSE_LINE_MAX + 1];  /* 当前行（未遇 LF） */
  int  line_len;
  int  line_overflow;               /* 本行超限：丢字节到 LF 为止 */
  char data[LLM_SSE_DATA_MAX + 1];  /* data: 行聚合缓冲 */
  int  data_len;
  int  data_overflow;               /* 本事件 data 超限：丢弃整个事件 */
  int  done;                        /* on_done/on_error 已发，后续事件静默 */
} LlmSse;

void llm_sse_reset(LlmSse*);

/* Feed one raw chunk of the SSE byte stream (any split point, including
 * mid-UTF-8-character). data: lines aggregate until the blank line, then
 * the event JSON is dispatched per protocol. Never fails; malformed JSON
 * fires on_error once and silences the rest of the stream. */
void llm_feed_sse(const ModelCfg *m, const char *chunk, int len,
                  LlmSse *state, const LlmEvents *ev, void *ctx);

/* Non-streaming path: parse one complete JSON body, emit the text (and
 * thinking) content, then exactly one on_done(finish). Bad JSON fires
 * on_error and no on_done. */
void llm_parse_nonstream(const ModelCfg *m, const char *body, int len,
                         const LlmEvents *ev, void *ctx);

/* Extract error.message from a non-2xx response body. Covers the
 * anthropic and openai shapes ({"error":{"message":...}}) plus the
 * string form ({"error":"..."}). Returns 0 and fills msg when found. */
int  llm_extract_error(const char *body, int len, char *msg, int cap);

/*
 * Full pipeline: connect/TLS/send/parse/callback. Returns 0 when the
 * exchange finished (on_done fired with 0/1, or 2 when *cancel aborted
 * the read); -1 on any failure (on_error fired, err holds a readable
 * message). All callbacks are optional (NULL-safe); cancel may be NULL.
 */
int llm_send(const ModelCfg *m, const ProxyCfg *proxy, const Session *s,
             const LlmEvents *ev, void *ctx,
             const volatile int *cancel, char *err, int errcap);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_LLM_H */
