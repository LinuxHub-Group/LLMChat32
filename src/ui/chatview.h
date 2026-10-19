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

/* Notification messages posted to g_hwnd_main when a link inside the
 * view is clicked (Task 18). Defined once here; consumers must use
 * these names, never raw WM_APP offsets. */
#define WM_APP_OPEN_THINK   (WM_APP + 0x110)   /* wParam = msg index  */
#define WM_APP_OPEN_IMAGE   (WM_APP + 0x111)   /* wParam = msg index,
                                                  lParam = block index */
#define WM_APP_RECALL       (WM_APP + 0x112)   /* wParam = msg index of a
                                                  right-clicked ROLE_USER
                                                  bubble the user asked to
                                                  recall (Task 20) */

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

/* Set (or clear) the background image. path_utf8 is decoded, blended
 * over white at opacity (0-100; clamped, <= 0 means plain white) and
 * pinned to the client bottom-right corner on every repaint, under the
 * bubbles and independent of scrolling. NULL or empty path clears the
 * image back to plain white. The old bitmap is freed on every call;
 * a missing file or decode failure silently keeps the white background.
 * Callable before the view exists (startup): the bitmap is built
 * lazily-owned state and the first paint uses it. */
void chatview_set_background(const char *path_utf8, int opacity);

/* Hit-test a CLIENT-AREA point against the link rectangles of the last
 * layout (pt.y is a client coordinate; the view scroll offset is added
 * here). Returns 1 and fills msg_idx/kind/block_idx when pt is inside a
 * link: kind 0 = think link (block_idx = -1), kind 1 = image block
 * link. Forces a lazy layout when the cached one is stale. Returns 0
 * when there is no link under the point. */
int chatview_hit_link(POINT pt, int *msg_idx, int *kind, int *block_idx);

/* Hit-test a CLIENT-AREA point against the message bubbles of the last
 * layout (pt.y is a client coordinate; the scroll offset is added here),
 * like chatview_hit_link but matching whole bubbles instead of link
 * rects. Returns the message index, or -1 when the point is outside
 * every bubble. Forces a lazy layout when the cached one is stale. */
int chatview_msg_at(POINT pt);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_CHATVIEW_H */
