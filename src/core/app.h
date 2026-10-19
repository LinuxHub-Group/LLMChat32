/*
 * app.h - application orchestration layer between the UI and the core.
 *
 * app_send() appends the user message (plus an empty assistant message)
 * to the Session, then runs llm_send on a worker thread. The worker never
 * touches the Session: response fragments cross a CRITICAL_SECTION-guarded
 * queue as heap blocks, and app_pump() (driven by the UI thread or by
 * tests) drains the queue, appends blocks to the assistant message, fires
 * the AppEvents callbacks and history_save()s the Session on done/error.
 * app_abort() sets a cancel flag the transport polls; the terminal event
 * is still on_done(2) + save + on_busy(0). app_recall() deletes msg_idx
 * and all later messages from memory and persists the truncation.
 *
 * Exactly-once contract: on_busy(1) fires synchronously from app_send;
 * exactly one of on_done/on_error fires from the pump at the terminal
 * event, then on_busy(0). No UI headers may be included here.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_APP_H
#define LLMCHAT32_APP_H

#include "config.h"
#include "llm.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct App App;

App* app_new(const Config*);
void app_free(App*);
int  app_send(App*, Session*, const char *user_text_utf8);  /* 0=started (async) */
void app_abort(App*);
int  app_busy(App*);                                        /* send in progress */
int  app_recall(App*, Session*, int msg_idx, char *err, int errcap);

/* UI <-> core event sink, same shape as LlmEvents plus msg index. */
typedef struct {
  void (*on_text)(void *ctx, int msg_idx, const char *frag, int len);
  void (*on_think)(void *ctx, int msg_idx, const char *frag, int len);
  void (*on_done)(void *ctx, int msg_idx, int finish);
  void (*on_error)(void *ctx, int msg_idx, const char *msg);
  void (*on_busy)(void *ctx, int busy);
} AppEvents;

void app_set_events(App*, const AppEvents*, void *ctx);

/*
 * Drain the worker->UI event queue. Call on the UI thread (timer or
 * message driven) or directly from tests; safe to call when empty.
 */
void app_pump(App*);

/*
 * Test hook: replace the transport implementation (llm_send by default)
 * with one of the identical signature, e.g. a canned-SSE playback.
 * Passing NULL restores the real network transport.
 */
typedef int (*AppTransportFn)(const ModelCfg *m, const ProxyCfg *proxy,
                              const Session *s, const LlmEvents *ev, void *ctx,
                              const volatile int *cancel,
                              char *err, int errcap);
void app_set_transport_hook(App*, AppTransportFn);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_APP_H */
