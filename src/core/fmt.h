/*
 * fmt.h - minimal in-house printf-style formatter.
 *
 * Replaces the Watcom CLIB printf core (vsnprintf/sprintf family) on
 * runtime error/logging paths. The CLIB __prtf_ formatter crashed
 * intermittently on Windows 9x (invalid page fault inside the format
 * scanner after one or more calls; the same binaries were fine on NT),
 * and a crash while formatting an error message hides the real fault.
 *
 * Supported conversions: d i u x X o c s p %, with flags '-' and '0',
 * field width (digits or '*'), precision ('.' digits or '.*') and
 * length modifiers l/ll/h/hh. %g/%e/%f are NOT supported (callers that
 * need exact C float formatting keep the CLIB sprintf - those paths
 * never run on 9x in normal operation).
 *
 * int fmt_vsnprintf(char *buf, int cap, const char *fmt, va_list ap);
 *   Writes at most cap bytes INCLUDING the NUL; returns the number of
 *   bytes that WOULD have been written (>= cap means truncation), like
 *   C99 snprintf. cap <= 0 writes nothing and returns the needed size.
 */
#ifndef LLMCHAT32_FMT_H
#define LLMCHAT32_FMT_H

#include <stdarg.h>

int fmt_vsnprintf(char *buf, int cap, const char *fmt, va_list ap);

/* Convenience variadic wrapper (C89). */
int fmt_snprintf(char *buf, int cap, const char *fmt, ...);

#endif
