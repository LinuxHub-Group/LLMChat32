/*
 * dlg_config.h - modal model-configuration dialog (Task 19).
 *
 *   int dlg_config_run(HWND owner, Config *cfg);
 *
 * Edits cfg->m[] (the model list) on a LOCAL COPY: 确定 validates the
 * copy, rewrites "config.toml" via config_save() and only then copies
 * the result back into *cfg and returns 1. 取消 / ESC / title-bar X
 * return 0 and leave *cfg untouched.
 *
 * Plain popup window with its own WndProc (no .rc / no DLGTEMPLATE),
 * exactly the pattern dlg_view.c established: the owner is disabled
 * while a private modal loop runs (IsDialogMessage for ESC/key routing),
 * and re-enabled on every close path. Global proxy / general settings
 * are deliberately OUT OF SCOPE here (future preferences dialog; proxy
 * stays hand-edited in the TOML file).
 *
 * All strings reach the controls through the ui_* outlets; text read
 * back from controls is CP_ACP bytes converted to UTF-8 (see the
 * get_utf8 recipe in dlg_config.c).
 */
#ifndef LLMCHAT32_UI_DLG_CONFIG_H
#define LLMCHAT32_UI_DLG_CONFIG_H

#include <windows.h>

#include "../core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Modal model-list editor. Returns 1 when saved (cfg updated + TOML
 * rewritten), 0 on cancel. */
int dlg_config_run(HWND owner, Config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_DLG_CONFIG_H */
