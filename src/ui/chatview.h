/*
 * chatview.h - self-drawn conversation view.
 *
 * Renders one Session (never owned): user messages as green right-aligned
 * bubbles, assistant messages as white left-aligned bubbles. BLOCK_THINK
 * is not rendered inline - an assistant message containing a think block
 * shows a 「思考过程」 link line (S_THINK_LINK) at the top of its bubble.
 * BLOCK_IMAGE renders as a small centered [图片] link line (S_IMAGE_LINK)
 * inside the bubble. Link rectangles are kept in a per-message list
 * inside chatview.c for Task 18 hit-testing/cursor work.
 *
 * Scrolling: 12px self-drawn scrollbar at the right edge (thumb drag,
 * track page clicks, mouse wheel). Painting is double-buffered through a
 * client-sized compatible bitmap before BitBlt.
 *
 * Layout is rebuilt from scratch on every repaint (and lazily from scroll
 * handlers); per-bubble incremental invalidation is deliberately not
 * cached - chat sizes make it unnecessary.
 */
#ifndef LLMCHAT32_UI_CHATVIEW_H
#define LLMCHAT32_UI_CHATVIEW_H

#include <windows.h>

#include "../core/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register "LlmChat" (first call only) and create the child window.
 * Returns the view HWND or NULL on failure; mainwin sizes it. */
HWND chatview_create(HWND parent);

/* Show a different session (NULL clears). The view pins to the bottom
 * and repaints; the Session stays owned by the caller. */
void chatview_set_session(Session*);

/* The message at msg_idx changed (streaming append). For now this
 * re-runs the full layout and repaints; per-bubble incremental redraw
 * is an optimization left out on purpose. */
void chatview_invalidate_msg(int msg_idx);

/* Pin the view to the newest message and repaint. */
void chatview_scroll_bottom(void);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_CHATVIEW_H */
