/*
 * mainwin.h - main window entry point.
 *
 * ui_mainwin_init() registers the window class, builds the menu from
 * LS() strings, creates the window sized from Config, and shows it.
 * The caller (WinMain) then runs the message loop; WM_DESTROY posts
 * WM_QUIT.
 */
#ifndef LLMCHAT32_UI_MAINWIN_H
#define LLMCHAT32_UI_MAINWIN_H

#include <windows.h>

#include "../core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

HWND ui_mainwin_init(HINSTANCE inst, int ncmdshow, Config *cfg);

/* main window handle, for child controls to notify */
extern HWND g_hwnd_main;

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_MAINWIN_H */
