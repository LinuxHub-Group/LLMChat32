/*
 * diag.h - opt-in file diagnostic log for field debugging.
 *
 * diagf() appends one line to "llmdiag.txt" in the current directory.
 * Logging activates only when a file named llmdiag.txt already exists
 * next to the running executable (zero cost otherwise): create an empty
 * file with that name to capture a send-path trace, delete it for
 * normal operation. Used to localize crashes that only reproduce on
 * real Windows 98 machines.
 */
#ifndef LLMCHAT32_DIAG_H
#define LLMCHAT32_DIAG_H

#include <windows.h>    /* HFONT */

void diagf(const char *fmt, ...);

#endif
