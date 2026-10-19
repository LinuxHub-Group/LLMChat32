/*
 * history.c - JSON persistence for sessions (see history.h).
 *
 * Save: the document {"id":..,"title":..,"msgs":[{"role":..,"blocks":
 * [{"k":..,"t":..}]}]} is built with the json.h JB writer into a heap
 * buffer sized at an exact upper bound (8 bytes per string char covers
 * the worst \u00XX escaping plus quotes), then written to "<id>.tmp"
 * and renamed over "<dir>/<id>.json". Any failure returns non-zero with
 * a message in err; the Session is const and never touched.
 *
 * Load: the whole file is parsed with json_parse into a temporary
 * Session built message by message; only on full success is it swapped
 * into *s (the old content freed). On any failure the temp content is
 * freed and *s is cleared to an empty session - still valid for
 * session_free / history_save.
 *
 * List: FindFirstFileA over "<dir>\\*.json"; names are stripped of the
 * ".json" suffix, filtered to at most 23 chars, and insertion-sorted
 * with strcmp so enumeration order does not leak into the result.
 */
#include <stdio.h>      /* fopen, fprintf, fclose, remove, rename */
#include <stdlib.h>     /* malloc, free */
#include <string.h>
#include <windows.h>    /* FindFirstFileA, GetFileAttributesA */

#include "json.h"
#include "history.h"

static void set_err(char *err, int errcap, const char *msg)
{
    if (err && errcap > 0) {
        strncpy(err, msg, (size_t)errcap - 1);
        err[errcap - 1] = '\0';
    }
}

static void path_for(char *buf, const char *dir, const char *id,
                     const char *suffix)
{
    /* buffers are sized by callers; ids are <= 23 chars by design */
    sprintf(buf, "%s\\%s%s", dir, id, suffix);
}

/* --- role / kind <-> string encodings ----------------------------------- */

static const char* role_str(Role r)
{
    switch (r) {
    case ROLE_SYSTEM: return "system";
    case ROLE_ASSIST: return "assistant";
    default:          return "user";
    }
}

static const char* kind_str(BlockKind k)
{
    switch (k) {
    case BLOCK_THINK: return "think";
    case BLOCK_IMAGE: return "image";
    default:          return "text";
    }
}

static int role_from(const char *s, Role *out)
{
    if (strcmp(s, "system") == 0)    { *out = ROLE_SYSTEM; return 0; }
    if (strcmp(s, "user") == 0)      { *out = ROLE_USER;   return 0; }
    if (strcmp(s, "assistant") == 0) { *out = ROLE_ASSIST; return 0; }
    return -1;
}

static int kind_from(const char *s, BlockKind *out)
{
    if (strcmp(s, "text") == 0)  { *out = BLOCK_TEXT;  return 0; }
    if (strcmp(s, "think") == 0) { *out = BLOCK_THINK; return 0; }
    if (strcmp(s, "image") == 0) { *out = BLOCK_IMAGE; return 0; }
    return -1;
}

/* --- save ---------------------------------------------------------------- */

/* Worst case: every byte of every string escapes to 6 chars + 2 quotes,
 * plus a fixed allowance for keys, punctuation and numbers. */
static size_t doc_upper_bound(const Session *s)
{
    size_t n = 64;
    int i;
    int j;

    n += strlen(s->id) * 8 + strlen(s->title) * 8;
    for (i = 0; i < s->nm; i++) {
        n += 64;
        for (j = 0; j < s->m[i].nb; j++)
            n += 32 + strlen(s->m[i].b[j].text) * 8;
    }
    return n + 16;
}

int history_save(const Session *s, const char *dir, char *err, int errcap)
{
    size_t cap = doc_upper_bound(s);
    char *buf = (char *)malloc(cap);
    char path[600];
    JB jb;
    FILE *fp;
    int bad = 0;
    int i;
    int j;

    if (!buf) {
        set_err(err, errcap, "out of memory");
        return -1;
    }
    jb_init(&jb, buf, (int)cap);
    jb_obj_begin(&jb);
      jb_key(&jb, "id");    jb_str(&jb, s->id);
      jb_key(&jb, "title"); jb_str(&jb, s->title);
      jb_key(&jb, "msgs");
      jb_arr_begin(&jb);
        for (i = 0; i < s->nm; i++) {
            jb_obj_begin(&jb);
              jb_key(&jb, "role"); jb_str(&jb, role_str(s->m[i].role));
              jb_key(&jb, "blocks");
              jb_arr_begin(&jb);
                for (j = 0; j < s->m[i].nb; j++) {
                    jb_obj_begin(&jb);
                      jb_key(&jb, "k"); jb_str(&jb, kind_str(s->m[i].b[j].kind));
                      jb_key(&jb, "t"); jb_str(&jb, s->m[i].b[j].text);
                    jb_obj_end(&jb);
                }
              jb_arr_end(&jb);
            jb_obj_end(&jb);
        }
      jb_arr_end(&jb);
    jb_obj_end(&jb);
    if (jb.overflow) {
        /* cannot happen with the upper bound above; fail safe anyway */
        free(buf);
        set_err(err, errcap, "session too large to serialize");
        return -1;
    }

    path_for(path, dir, s->id, ".json.tmp");
    /* NTFS ignores the read-only attribute on directories, so an
       explicit check keeps "save into a read-only dir fails" true on
       every filesystem (on FAT/Win9x the fopen below fails anyway). */
    {
        DWORD a = GetFileAttributesA(dir);

        if (a != INVALID_FILE_ATTRIBUTES &&
            (a & FILE_ATTRIBUTE_READONLY) != 0) {
            set_err(err, errcap, "history directory is read-only");
            free(buf);
            return -1;
        }
    }
    fp = fopen(path, "wb");
    if (!fp) {
        set_err(err, errcap, "cannot open history file for writing");
        free(buf);
        return -1;
    }
    if (fwrite(buf, 1, (size_t)jb.len, fp) != (size_t)jb.len)
        bad = 1;
    if (fclose(fp) != 0)
        bad = 1;
    free(buf);
    if (bad) {
        set_err(err, errcap, "failed to write history file (disk full?)");
        remove(path);
        return -1;
    }

    path_for(path, dir, s->id, ".json");
    remove(path);  /* rename() cannot replace an existing file on Win32 */
    {
        char tmp[600];

        path_for(tmp, dir, s->id, ".json.tmp");
        if (rename(tmp, path) != 0) {
            set_err(err, errcap, "failed to replace history file");
            remove(tmp);
            return -1;
        }
    }
    return 0;
}

/* --- load ---------------------------------------------------------------- */

static void session_clear(Session *s)
{
    int i;
    int j;

    for (i = 0; i < s->nm; i++) {
        for (j = 0; j < s->m[i].nb; j++)
            free(s->m[i].b[j].text);
        free(s->m[i].b);
    }
    free(s->m);
    s->m = NULL;
    s->nm = 0;
}

int history_load(Session *s, const char *path, char *err, int errcap)
{
    FILE *fp;
    long sz;
    char *buf;
    JVal *root;
    JVal *msgs;
    JVal *v;
    Session t;          /* filled first, swapped in only on success */
    int i;
    int j;

    fp = fopen(path, "rb");
    if (!fp) {
        set_err(err, errcap, "cannot open history file");
        session_clear(s);
        s->id[0] = '\0';
        s->title[0] = '\0';
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        set_err(err, errcap, "cannot read history file");
        session_clear(s);
        s->id[0] = '\0';
        s->title[0] = '\0';
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        set_err(err, errcap, "out of memory");
        session_clear(s);
        s->id[0] = '\0';
        s->title[0] = '\0';
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        set_err(err, errcap, "cannot read history file");
        session_clear(s);
        s->id[0] = '\0';
        s->title[0] = '\0';
        return -1;
    }
    fclose(fp);
    buf[sz] = '\0';

    root = json_parse(buf, (int)sz, err, errcap);
    free(buf);
    if (!root) {
        session_clear(s);
        s->id[0] = '\0';
        s->title[0] = '\0';
        return -1;
    }

    memset(&t, 0, sizeof t);
    if (json_type(root) != JOBJ)
        goto bad;
    v = json_obj_get(root, "id");
    if (!v || json_type(v) != JSTR)
        goto bad;
    strncpy(t.id, json_str(v), sizeof t.id - 1);
    v = json_obj_get(root, "title");
    if (!v || json_type(v) != JSTR)
        goto bad;
    strncpy(t.title, json_str(v), sizeof t.title - 1);
    msgs = json_obj_get(root, "msgs");
    if (!msgs || json_type(msgs) != JARR)
        goto bad;
    for (i = 0; i < json_arr_len(msgs); i++) {
        JVal *mo = json_arr_get(msgs, i);
        JVal *blocks;
        JVal *rv;
        Msg *m;

        if (!mo || json_type(mo) != JOBJ)
            goto bad;
        rv = json_obj_get(mo, "role");
        if (!rv || json_type(rv) != JSTR)
            goto bad;
        m = session_add_msg(&t, ROLE_USER);
        if (!m)
            goto oom;
        if (role_from(json_str(rv), &m->role) != 0)
            goto bad;
        blocks = json_obj_get(mo, "blocks");
        if (!blocks || json_type(blocks) != JARR)
            goto bad;
        for (j = 0; j < json_arr_len(blocks); j++) {
            JVal *bo = json_arr_get(blocks, j);
            JVal *kv;
            JVal *tv;
            BlockKind k;

            if (!bo || json_type(bo) != JOBJ)
                goto bad;
            kv = json_obj_get(bo, "k");
            tv = json_obj_get(bo, "t");
            if (!kv || json_type(kv) != JSTR ||
                !tv || json_type(tv) != JSTR)
                goto bad;
            if (kind_from(json_str(kv), &k) != 0)
                goto bad;
            if (!msg_add_block(m, k, json_str(tv)))
                goto oom;
        }
    }
    json_free(root);

    /* success: drop old content, move temp in */
    session_clear(s);
    s->id[0] = '\0';
    s->title[0] = '\0';
    s->m = t.m;
    s->nm = t.nm;
    strcpy(s->id, t.id);
    strcpy(s->title, t.title);
    return 0;

oom:
    set_err(err, errcap, "out of memory");
bad:
    if (err && errcap > 0 && err[0] == '\0')
        set_err(err, errcap, "malformed history file");
    session_clear(&t);
    json_free(root);
    /* rollback: *s becomes an empty, savable session */
    session_clear(s);
    s->id[0] = '\0';
    s->title[0] = '\0';
    return -1;
}

/* --- delete / list -------------------------------------------------------- */

int history_delete(const char *dir, const char *id)
{
    char path[600];

    path_for(path, dir, id, ".json");
    if (remove(path) == 0)
        return 0;
    /* already gone counts as deleted */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return 0;
    return -1;
}

int history_list(const char *dir, char (*ids)[24], int max)
{
    char pat[600];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;

    if (max <= 0 || !ids)
        return 0;
    sprintf(pat, "%s\\*.json", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const char *name = fd.cFileName;
        size_t len = strlen(name);
        char id[24];
        int i;

        if (len <= 5 || len - 5 > 23)
            continue;           /* not "<id>.json" with id <= 23 chars */
        if (strcmp(name + len - 5, ".json") != 0)
            continue;
        memcpy(id, name, len - 5);
        id[len - 5] = '\0';
        /* insertion sort keeps the result lexicographic regardless of
           the enumeration order the filesystem returns */
        i = n < max ? n : max;
        if (i == max) {
            if (strcmp(id, ids[max - 1]) > 0)
                continue;       /* beyond the cap and sorts after it */
            i = max - 1;
        } else {
            n++;
        }
        while (i > 0 && strcmp(id, ids[i - 1]) < 0) {
            strcpy(ids[i], ids[i - 1]);
            i--;
        }
        strcpy(ids[i], id);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
