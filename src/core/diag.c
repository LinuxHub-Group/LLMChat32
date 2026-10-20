/*
 * diag.c - opt-in file diagnostic log for field debugging.
 *
 * diagf() appends one line to "llmdiag.txt" next to the EXECUTABLE
 * (not the CWD - shortcuts and Start-menu launches would miss a CWD
 * relative file). Logging activates only when that file already exists
 * (zero cost otherwise): create an empty llmdiag.txt beside the exe to
 * capture a send-path trace, delete it for normal operation.
 *
 * The first diagf() call also installs an unhandled-exception filter
 * that appends "CRASH code=.. addr=.. base=.." to the log, so a fatal
 * fault on a machine without a debugger still leaves its address
 * behind; the faulting module offset is addr - base.
 */
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

#include "diag.h"

static int g_diag_state;    /* 0 unknown, 1 enabled, 2 disabled */
static char g_path[MAX_PATH];

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
        if (g_diag_state == 1)
            SetUnhandledExceptionFilter(diag_except);
    }
    return g_diag_state == 1;
}

void diagf(const char *fmt, ...)
{
    va_list ap;
    FILE *fp;

    if (!diag_enabled())
        return;
    fp = fopen(g_path, "ab");
    if (!fp)
        return;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}
