/*
 * diag.c - opt-in file diagnostic log for field debugging.
 */
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

#include "diag.h"

static int g_diag_state;    /* 0 unknown, 1 enabled, 2 disabled */

static int diag_enabled(void)
{
    if (g_diag_state == 0)
        g_diag_state =
            GetFileAttributesA("llmdiag.txt") != 0xFFFFFFFF ? 1 : 2;
    return g_diag_state == 1;
}

void diagf(const char *fmt, ...)
{
    va_list ap;
    FILE *fp;

    if (!diag_enabled())
        return;
    fp = fopen("llmdiag.txt", "ab");
    if (!fp)
        return;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}
