/*
 * llm.h - LLM request construction for the three wire protocols.
 *
 * Pure functions only (no network). The parsing/send half (llm_send,
 * SSE event dispatch) is Task 11 and layers on top of these builders.
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
 *   falls back to medium (8000).
 * temperature is emitted only when ModelCfg.temperature >= 0.
 *
 * Context truncation (llm_truncate_context, spec section 6): estimate
 * is the character count of text blocks (1 token ~= 3.5 chars). The
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

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_LLM_H */
