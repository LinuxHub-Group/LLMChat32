/*
 * diag.c - opt-in file diagnostic log for field debugging.
 *
 * diagf() appends one line to "llmdiag.txt" next to the EXECUTABLE
 * (not the CWD - shortcuts and Start-menu launches would miss a CWD
 * relative file). Logging activates only when that file already exists
 * (zero cost otherwise): create an empty llmdiag.txt beside the exe to
 * capture a send-path trace, delete it for normal operation.
 *
 * The file is opened ONCE (first enabled call) and written with raw
 * WriteFile; a previous stdio implementation (fopen/vfprintf/fclose on
 * every paint, dozens of calls per second) crashed deep inside the
 * Watcom printf core on Windows 9x while the same code was fine on NT.
 * The first enabled call also installs an unhandled-exception filter
 * that appends "CRASH code=.. addr=.. base=.. off=..", so a fatal
 * fault on a debugger-less machine still leaves its address behind;
 * the faulting offset is addr - base.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#include "diag.h"
#include "fmt.h"

static int g_diag_state;    /* 0 unknown, 1 enabled, 2 disabled */
static char g_path[MAX_PATH];
static HANDLE g_file = NULL;

static LONG WINAPI diag_except(EXCEPTION_POINTERS *xp)
{
    HANDLE f;
    DWORD wr;
    char buf[160];
    DWORD base;

    if (xp == NULL || xp->ExceptionRecord == NULL)
        return EXCEPTION_CONTINUE_SEARCH;
    base = (DWORD)GetModuleHandleA(NULL);
    wsprintfA(buf, "CRASH code=%08lx addr=%08lx base=%08lx off=%08lx\r\n",
              (DWORD)xp->ExceptionRecord->ExceptionCode,
              (DWORD)xp->ExceptionRecord->ExceptionAddress, base,
              (DWORD)xp->ExceptionRecord->ExceptionAddress - base);
    f = CreateFileA(g_path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        SetFilePointer(f, 0, NULL, FILE_END);
        WriteFile(f, buf, lstrlenA(buf), &wr, NULL);
        CloseHandle(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static int diag_enabled(void)
{
    if (g_diag_state == 0) {
        DWORD n;

        n = GetModuleFileNameA(NULL, g_path, sizeof g_path);
        if (n > 4 && n < sizeof g_path) {
            while (n > 0 && g_path[n - 1] != '\\' && g_path[n - 1] != ':')
                n--;
            g_path[n] = '\0';
            lstrcatA(g_path, "llmdiag.txt");
            g_diag_state = GetFileAttributesA(g_path) != 0xFFFFFFFF
                               ? 1 : 2;
        } else {
            g_diag_state = 2;
        }
        if (g_diag_state == 1) {
            g_file = CreateFileA(g_path, GENERIC_WRITE, FILE_SHARE_READ,
                                 NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                 NULL);
            if (g_file == INVALID_HANDLE_VALUE) {
                g_file = NULL;
                g_diag_state = 2;
            } else {
                SetFilePointer(g_file, 0, NULL, FILE_END);
                SetUnhandledExceptionFilter(diag_except);
            }
        }
    }
    return g_diag_state == 1;
}

void diagf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    DWORD wr;
    int n;

    if (!diag_enabled())
        return;
    va_start(ap, fmt);
    n = fmt_vsnprintf(buf, (int)sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 2)
        n = (int)sizeof buf - 2;
    buf[n] = '\r';
    buf[n + 1] = '\n';
    WriteFile(g_file, buf, (DWORD)(n + 2), &wr, NULL);
}
