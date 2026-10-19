/*
 * dlg_view.h - modal "view" dialogs for chat content links (Task 18).
 *
 *   dlg_view_text  - read-only multiline viewer for think blocks.
 *   dlg_view_image - downloads an image URL over the model's transport
 *                    policy (image_fetch), decodes it, shows it
 *                    aspect-fitted, and offers 「另存为…」 to save the
 *                    fetched bytes.
 *
 * Both are plain popup windows with their own WndProc (no .rc / no
 * DLGTEMPLATE) that disable the owner, run a private modal message loop
 * (IsDialogMessage so ESC/keyboard work in the children), and re-enable
 * the owner on every close path (OK / ESC / title-bar X). All strings
 * reach the controls through the ui_* outlets.
 *
 * dlg_view_image runs the download on the calling thread and blocks it
 * (Task 18 v1 ruling, documented in image.h: a slow URL freezes the UI;
 * Win98 has a single UI thread anyway).
 */
#ifndef LLMCHAT32_UI_DLG_VIEW_H
#define LLMCHAT32_UI_DLG_VIEW_H

#include <windows.h>

#include "../core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Modal read-only text viewer. title_utf8 goes to the caption,
 * text_utf8 fills the edit. */
void dlg_view_text(HWND owner, const char *title_utf8, const char *text_utf8);

/* Modal image viewer for url_utf8. m/p provide the transport policy
 * (use_proxy / verify_tls), exactly like an LLM send. */
void dlg_view_image(HWND owner, const char *url_utf8, const ModelCfg *m,
                    const ProxyCfg *p);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_DLG_VIEW_H */
