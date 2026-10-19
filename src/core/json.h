/*
 * json.h - minimal JSON DOM parser and streaming writer.
 *
 * DOM tree of JVal nodes, one malloc per node (plus per-string storage).
 * The whole tree is released with json_free(). Parser is strict: any
 * malformed input (bad escapes, lone surrogates, truncated documents)
 * fails with NULL and a human-readable message in err.
 *
 * The writer (JB) emits compact JSON into a caller-provided fixed buffer
 * and tracks overflow instead of growing; once overflow is set every
 * further write is a no-op and the buffer stays NUL-terminated within
 * cap bytes.
 *
 * All functions are C89. Strings are narrow UTF-8 on both sides.
 */
#ifndef LLMCHAT32_JSON_H
#define LLMCHAT32_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JVal JVal;
typedef enum { JNULL, JBOOL, JNUM, JSTR, JARR, JOBJ } JType;

/* Parse the len bytes of text (len < 0 means strlen(text)).
 * Returns the root node, or NULL on any error with a short message
 * written into err (when errcap > 0). Frees nothing on success -
 * the caller owns the tree and must release it with json_free(). */
JVal* json_parse(const char *text, int len, char *err, int errcap); /* NULL=失败 */

/* Accessors. All are NULL-safe: wrong type or NULL node yields a
 * neutral value (0 / 0.0 / "" / NULL) rather than undefined behavior. */
JType      json_type(const JVal*);
int        json_bool(const JVal*);
double     json_num(const JVal*);
const char* json_str(const JVal*);        /* 已反转义，UTF-8，生命周期随 json_free */
int        json_arr_len(const JVal*);
JVal*      json_arr_get(const JVal*, int);
JVal*      json_obj_get(const JVal*, const char *key);
void       json_free(JVal*);

/* Streaming writer over a fixed caller buffer. */
typedef struct { char *buf; int cap; int len; int overflow; } JB;

void jb_init(JB*, char *buf, int cap);
void jb_obj_begin(JB*); void jb_obj_end(JB*); void jb_arr_begin(JB*); void jb_arr_end(JB*);
void jb_key(JB*, const char *utf8); void jb_str(JB*, const char *utf8);
void jb_int(JB*, long); void jb_num(JB*, double); void jb_bool(JB*, int); void jb_raw(JB*, const char*);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_JSON_H */
