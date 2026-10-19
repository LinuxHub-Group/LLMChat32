/*
 * history.h - JSON persistence for sessions (spec §8 format).
 *
 * One file per session: "<dir>/<id>.json" holding
 *   {"id":"...","title":"...","msgs":[{"role":"user","blocks":[{"k":"text","t":"..."}]}]}
 * role encodings: "system"|"user"|"assistant"; block kinds: "text"|
 * "think"|"image". history_save writes "<id>.tmp" and renames it over
 * the target; on failure the Session is untouched. history_load parses
 * the file at the given path; any failure rolls the session back to an
 * empty state that can be saved again. history_delete removes
 * "<dir>/<id>.json" (idempotent). history_list enumerates the dir's
 * "*.json" files and returns the ids lexicographically sorted (up to max).
 *
 * The directory comes from the caller (e.g. GeneralCfg.history_dir);
 * this unit deliberately does not include config.h.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_HISTORY_H
#define LLMCHAT32_HISTORY_H

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write <dir>/<id>.json (tmp+rename). Non-zero + err on any failure. */
int history_save(const Session*, const char *dir, char *err, int errcap);

/* Parse path into *s. Any failure: non-zero + err, *s rolled back to
 * an empty session (kept fully freeable/savable). */
int history_load(Session*, const char *path, char *err, int errcap);

/* Remove <dir>/<id>.json. Non-zero only if the file still exists. */
int history_delete(const char *dir, const char *id);

/* Fill ids[] with the sorted session ids in dir; returns the count
 * (possibly capped by max). */
int history_list(const char *dir, char (*ids)[24], int max);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_HISTORY_H */
