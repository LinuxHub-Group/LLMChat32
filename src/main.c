/*
 * main.c - LLMChat32 entry point.
 *
 * Windows 98 compatible LLM chat client.
 * Loads config (defaults + optional config.toml overlay), picks the UI
 * language, creates the main window, runs the message loop.
 */
#include <windows.h>

#include "core/config.h"
#include "ui/mainwin.h"
#include "ui/strings.h"
#include "version.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow )
{
    Config cfg;
    char err[256];
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)LLM_VERSION;

    config_defaults( &cfg );
    config_load( &cfg, "config.toml", err, sizeof err );
    /* a missing config.toml is fine - defaults stay in place */

    ui_lang_init();

    if( ui_mainwin_init( hInstance, nCmdShow, &cfg ) == NULL )
        return 1;

    while( GetMessage( &msg, NULL, 0, 0 ) ) {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }
    return (int)msg.wParam;
}
