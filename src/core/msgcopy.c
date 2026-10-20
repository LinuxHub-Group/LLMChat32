/*
 * msg_copy_text - see session.h.
 *
 * Pure formatting over the block list: no allocation, bounded by the
 * caller's cap, reentrant. Two passes would need the block texts to be
 * stable - they are (const Msg), so first count, then copy.
 */
#include <string.h>

#include "session.h"

int msg_copy_text(const Msg *m, char *buf, int cap)
{
    static const char img[] = "[\xe5\x9b\xbe\xe7\x89\x87]";    /* [图片] */
    int need = 0;
    int sep = 0;
    int i;

    if (m == NULL)
        return 0;
    for (i = 0; i < m->nb; i++) {
        const Block *b = &m->b[i];
        const char *t = NULL;

        if (b->kind == BLOCK_TEXT)
            t = b->text;
        else if (b->kind == BLOCK_IMAGE)
            t = img;
        else
            continue;           /* THINK: never copied */
        if (t == NULL)
            continue;
        if (sep)
            need += 2;          /* "\n\n" */
        need += (int)strlen(t);
        sep = 1;
    }
    if (buf != NULL && cap > 0) {
        int n = 0;
        int first = 1;

        buf[0] = '\0';
        for (i = 0; i < m->nb && n < cap - 1; i++) {
            const Block *b = &m->b[i];
            const char *t = NULL;
            int len;

            if (b->kind == BLOCK_TEXT)
                t = b->text;
            else if (b->kind == BLOCK_IMAGE)
                t = img;
            else
                continue;
            if (t == NULL)
                continue;
            len = (int)strlen(t);
            if (!first && n + 2 < cap - 1) {
                buf[n++] = '\n';
                buf[n++] = '\n';
            }
            first = 0;
            while (len-- > 0 && n < cap - 1)
                buf[n++] = *t++;
            buf[n] = '\0';
        }
    }
    return need;
}
