/*
 * session.c - in-memory chat session model (see session.h).
 *
 * session_new zeroes the struct and copies the id. session_add_msg
 * grows the message array with realloc (NULL on OOM, array unchanged).
 * msg_add_block stores a malloc'd copy of the text. session_free walks
 * every message releasing each block text and block array, then the
 * message array and the Session itself.
 */
#include <stdlib.h>     /* malloc, realloc, free */
#include <string.h>

#include "session.h"

Session* session_new(const char *id)
{
    Session *s = (Session *)malloc(sizeof(Session));

    if (!s)
        return NULL;
    memset(s, 0, sizeof *s);
    if (id) {
        strncpy(s->id, id, sizeof s->id - 1);
        s->id[sizeof s->id - 1] = '\0';
    }
    return s;
}

Msg* session_add_msg(Session *s, Role role)
{
    Msg *nm;

    if (!s)
        return NULL;
    nm = (Msg *)realloc(s->m, (size_t)(s->nm + 1) * sizeof(Msg));
    if (!nm)
        return NULL;
    s->m = nm;
    s->m[s->nm].role = role;
    s->m[s->nm].b = NULL;
    s->m[s->nm].nb = 0;
    s->nm++;
    return &s->m[s->nm - 1];
}

Block* msg_add_block(Msg *m, BlockKind kind, const char *utf8_text)
{
    Block *nb;
    char *copy;
    size_t len;

    if (!m || !utf8_text)
        return NULL;
    len = strlen(utf8_text) + 1;
    copy = (char *)malloc(len);
    if (!copy)
        return NULL;
    memcpy(copy, utf8_text, len);
    nb = (Block *)realloc(m->b, (size_t)(m->nb + 1) * sizeof(Block));
    if (!nb) {
        free(copy);
        return NULL;
    }
    m->b = nb;
    m->b[m->nb].kind = kind;
    m->b[m->nb].text = copy;
    m->nb++;
    return &m->b[m->nb - 1];
}

void session_free(Session *s)
{
    int i;
    int j;

    if (!s)
        return;
    for (i = 0; i < s->nm; i++) {
        for (j = 0; j < s->m[i].nb; j++)
            free(s->m[i].b[j].text);
        free(s->m[i].b);
    }
    free(s->m);
    free(s);
}
