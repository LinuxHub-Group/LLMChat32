/*
 * session.h - in-memory chat session model.
 *
 * A Session is an ordered list of messages; each Msg holds an ordered
 * list of typed content blocks. Block text is narrow UTF-8; for
 * BLOCK_IMAGE it stores the source URL/path instead of visible text.
 * Everything is heap-allocated; session_free() releases every block
 * text, every block array, the message array and the Session itself.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_SESSION_H
#define LLMCHAT32_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ROLE_SYSTEM, ROLE_USER, ROLE_ASSIST } Role;
typedef enum { BLOCK_TEXT, BLOCK_THINK, BLOCK_IMAGE } BlockKind;
typedef struct { BlockKind kind; char *text; } Block;   /* text: UTF-8；IMAGE 存来源 URL/路径 */
typedef struct { Role role; Block *b; int nb; } Msg;
typedef struct { char id[24]; char title[64]; Msg *m; int nm; } Session;

/* Allocate a zeroed session and copy id (truncated to fit). */
Session* session_new(const char *id);

/* Append an empty message with the given role; NULL on OOM. */
Msg*     session_add_msg(Session*, Role);

/* Append a block with a malloc'd copy of utf8_text; NULL on OOM. */
Block*   msg_add_block(Msg*, BlockKind, const char *utf8_text);

/* Free all block texts, block arrays, the message array, the Session. */
void     session_free(Session*);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_SESSION_H */
