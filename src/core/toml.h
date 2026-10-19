/*
 * toml.h - TOML subset parser (line-oriented, callback-driven).
 *
 * Supported grammar:
 *   [section]            table header
 *   [[array-table]]      array-of-tables header (repeatable)
 *   key = "string"       basic string, escapes \" \\ \b \f \n \r \t
 *   key = 123 / -1.5e3   integers and floats
 *   key = true / false   booleans
 *   # comment            whole-line and trailing comments
 *
 * Anything outside this subset (dates, literal strings, inline tables,
 * arrays, dotted keys, multi-line strings, \uXXXX escapes) is rejected
 * with a non-zero return and a message in err. Malformed input never
 * crashes the parser: it always fails with a readable error.
 *
 * The caller supplies two callbacks. on_section() announces a header
 * (is_array_item distinguishes [[x]] from [x]); return non-zero to
 * abort the parse. on_kv() delivers one key/value pair at a time,
 * already typed; return non-zero to abort (err carries the reason).
 * Context between callbacks (current section, array index) is the
 * caller's own state, reachable via ctx.
 *
 * All functions are C89. Strings are narrow UTF-8 on both sides.
 */
#ifndef LLMCHAT32_TOML_H
#define LLMCHAT32_TOML_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TVAL_STR, TVAL_INT, TVAL_FLOAT, TVAL_BOOL } TomlType;

typedef struct {
    TomlType type;
    char str[512];  /* TVAL_STR: unescaped UTF-8 */
    long ival;      /* TVAL_INT */
    double fval;    /* TVAL_FLOAT */
    int bval;       /* TVAL_BOOL */
} TomlVal;

/* Return 0 to continue, non-zero to abort the parse (write err first). */
typedef int (*TomlSecFn)(void *ctx, const char *section, int is_array_item);
typedef int (*TomlKvFn)(void *ctx, const char *key, const TomlVal *val,
                        char *err, int errcap);

/* Parse the len bytes of text (len < 0 means strlen(text)).
 * Returns 0 on success; non-zero on any malformed input or when a
 * callback aborts, with a short message in err (when errcap > 0). */
int toml_parse(const char *text, int len,
               TomlSecFn on_section, TomlKvFn on_kv, void *ctx,
               char *err, int errcap);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_TOML_H */
