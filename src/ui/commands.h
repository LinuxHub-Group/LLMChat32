/*
 * commands.h - WM_COMMAND ids for the main-window menu.
 *
 * Kept in one place so later tasks (chat list, options dialog, about box)
 * share the same ids. Values start at 100 to stay clear of any
 * system-defined control ids.
 */
#ifndef LLMCHAT32_UI_COMMANDS_H
#define LLMCHAT32_UI_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    IDM_NEW_CHAT = 100,
    IDM_CLOSE_CHAT,
    IDM_RECALL,
    IDM_CLEAR,
    IDM_MODEL_CFG,
    IDM_ABOUT,
    IDM_EXIT
};

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_COMMANDS_H */
