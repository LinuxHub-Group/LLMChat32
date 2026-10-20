/*
 * main.c - LLMChat32 entry point.
 *
 * Windows 98 compatible LLM chat client.
 * Loads config (defaults + optional config.toml overlay), picks the UI
 * language, initializes Winsock (net.h requires WSAStartup from the
 * caller), creates the main window, runs the message loop.
 */
#include <winsock2.h>
#include <windows.h>

#include "core/config.h"
#include "core/diag.h"
#include "ui/mainwin.h"
#include "ui/strings.h"
#include "ui/ui.h"
#include "version.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow )
{
    Config cfg;
    WSADATA wd;
    char err[256];
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)LLM_VERSION;

    /* net.c/tls.c/http.c need Winsock initialized by the caller */
    if( WSAStartup( MAKEWORD( 2, 0 ), &wd ) != 0 )
        return 1;

    config_defaults( &cfg );
    config_load( &cfg, "config.toml", err, sizeof err );
    /* a missing config.toml is fine - defaults stay in place */
    g_cfg = cfg;   /* process-wide copy for the UI layer (see config.h) */

    ui_lang_init();

    /* first diag touch: resolves the log path beside the exe and, when
       llmdiag.txt exists there, installs the crash-logger filter */
    diagf("=== llmchat32 %s start (win9x=%d) ===", LLM_VERSION, g_win9x);

    if( ui_mainwin_init( hInstance, nCmdShow, &cfg ) == NULL ) {
        WSACleanup();
        return 1;
    }

    while( GetMessage( &msg, NULL, 0, 0 ) ) {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }
    WSACleanup();
    return (int)msg.wParam;
}
