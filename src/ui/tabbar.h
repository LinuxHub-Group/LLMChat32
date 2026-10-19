/*
 * tabbar.h - self-drawn tab strip child window.
 *
 * The tabbar is a registered child window class "LlmTabBar" living in the
 * top strip of the mainwin client area. It renders one tab per session
 * (text width + a close-glyph area) plus a fixed-width "+" tab on the
 * right. All text is drawn through the ui_* wrappers, never raw ANSI.
 *
 * Notifications are posted to g_hwnd_main (see mainwin.h):
 *
 *   WM_APP_TAB_CHANGED  - user selected a different tab, lParam = index
 *   WM_APP_TAB_CLOSE    - user confirmed closing tab, lParam = index
 *   WM_APP_TAB_NEW      - the "+" tab was clicked
 *
 * The tabbar does NOT delete anything itself: the owner (mainwin) reacts
 * to WM_APP_TAB_CLOSE (history_delete etc. land in a later task).
 */
#ifndef LLMCHAT32_UI_TABBAR_H
#define LLMCHAT32_UI_TABBAR_H

#include <windows.h>

#include "../core/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Notification messages posted to g_hwnd_main. Defined once here;
 * consumers must use these names, never raw WM_APP offsets. */
#define WM_APP_TAB_CHANGED  (WM_APP + 0x100)
#define WM_APP_TAB_CLOSE    (WM_APP + 0x101)
#define WM_APP_TAB_NEW      (WM_APP + 0x102)

/* Height of the tab strip in the mainwin client area. */
#define TABBAR_HEIGHT 28

/* Register "LlmTabBar" (first call only) and create the child window.
 * Returns the tabbar HWND or NULL on failure. mainwin sizes it. */
HWND tabbar_create(HWND parent);

/* Full refresh: copies the n session titles (list order = display order)
 * and re-measures every tab width. Selection resets to 0 when n > 0,
 * -1 when n == 0. The tabbar never owns the Session objects. */
void tabbar_set_sessions(Session **list, int n);

/* Current selection index, -1 when there are no tabs. */
int  tabbar_current(void);

/* Programmatic selection change; idx is clamped. No notification is
 * posted - the caller already knows. */
void tabbar_set_current(int idx);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_TABBAR_H */
