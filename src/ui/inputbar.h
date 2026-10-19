/*
 * inputbar.h - bottom input strip: multiline edit + model combobox +
 * send/abort button.
 *
 * A registered child window class "LlmInputBar" hosting three standard
 * controls. mainwin sizes it into the strip directly above the status
 * strip (INPUTBAR_HEIGHT pixels) and reacts to the forwarded
 * notifications:
 *
 *   WM_COMMAND IDIB_SEND  (BN_CLICKED)  - send button click, or Ctrl+Enter
 *                                         in the edit (the subclass turns
 *                                         Ctrl+Enter into this exact
 *                                         notification; a plain Enter
 *                                         still inserts a newline)
 *   WM_COMMAND IDIB_MODEL (CBN_SELCHANGE)- model combobox selection changed
 *
 * Text discipline (project red line): the edit stores CP_ACP bytes (the
 * OS owns its content); inputbar_get_text() converts CP_ACP -> UTF-16 ->
 * UTF-8 so callers only ever see narrow UTF-8. All text placed INTO the
 * controls (button caption, combo items) goes through the ui_* outlets.
 */
#ifndef LLMCHAT32_UI_INPUTBAR_H
#define LLMCHAT32_UI_INPUTBAR_H

#include <windows.h>

#include "../core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Height of the input strip in the mainwin client area. */
#define INPUTBAR_HEIGHT  64

/* Child control ids, unique inside the inputbar; the bar forwards them
 * to its parent as WM_COMMAND notifications. */
#define IDIB_EDIT   1
#define IDIB_MODEL  2
#define IDIB_SEND   3

/* Upper bound (bytes) accepted from the edit by inputbar_get_text. */
#define INPUTBAR_TEXT_MAX 8192

/* Register "LlmInputBar" (first call only) and create the child window
 * with the edit/combo/button inside. Returns the bar HWND or NULL on
 * failure; mainwin sizes it. */
HWND inputbar_create(HWND parent);

/* (Re)fill the model dropdown from cfg->m[i].name, UTF-8 -> CP_ACP via
 * the ui_* outlet, and select entry 0. Call at startup and whenever the
 * config changes. */
void inputbar_set_models(const Config *cfg);

/* Current dropdown selection: an index into cfg->m[], 0 when empty or
 * nothing is selected. */
int  inputbar_selected_model(void);

/* Copy the edit content into buf as narrow UTF-8 (CP_ACP -> UTF-16 ->
 * UTF-8), always NUL-terminated when cap > 0. Trailing bytes that do not
 * fit are cut on a character boundary. */
void inputbar_get_text(char *buf, int cap);

/* Empty the edit. */
void inputbar_clear(void);

/* Switch the button between S_BTN_SEND and S_BTN_ABORT; pass the current
 * app busy flag. */
void inputbar_set_sending(int busy);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_INPUTBAR_H */
