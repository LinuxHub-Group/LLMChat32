/*
 * test_json.c - JSON DOM parser and streaming builder unit tests.
 *
 * Console app (built with -bc). Verifies the exact assertions from the
 * task spec: parsing the brief's document with nested array / CJK
 * string / bool / null / negative fraction; rejection of malformed or
 * truncated input with a non-empty error message; \uXXXX unescape for
 * BMP and surrogate pairs; rejection of invalid escapes; builder output
 * byte-exactness (comma placement, escaping); builder->parser round
 * trip; and overflow semantics (flag set once, never writes past cap).
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_json.exe ^
 *          src\tests\test_json.c src\core\json.c src\core\utf.c
 */
#include <stdio.h>
#include <string.h>

#include "../core/json.h"

static int g_fail = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (cond) {                                                  \
            printf("ok %s\n", #cond);                                \
        } else {                                                     \
            printf("FAIL %s (line %d)\n", #cond, __LINE__);          \
            g_fail++;                                                \
        }                                                            \
    } while (0)

int main(void)
{
    char err[128];
    char buf[256];
    char guard[16];
    JB jb;
    JVal *r;
    JVal *a;
    JVal *b;
    const char *s;

    /* --- the brief's document: {"a":[1,"x中",true,null],"b":-2.5} --- */
    {
        static const char doc[] = "{\"a\":[1,\"x\xe4\xb8\xad\",true,null],\"b\":-2.5}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        CHECK(json_type(r) == JOBJ);
        a = json_obj_get(r, "a");
        CHECK(a != NULL && json_type(a) == JARR);
        CHECK(json_arr_len(a) == 4);
        CHECK(json_type(json_arr_get(a, 0)) == JNUM);
        CHECK(json_num(json_arr_get(a, 0)) == 1.0);
        CHECK(json_type(json_arr_get(a, 1)) == JSTR);
        CHECK(strcmp(json_str(json_arr_get(a, 1)), "x\xe4\xb8\xad") == 0);
        CHECK(json_type(json_arr_get(a, 2)) == JBOOL);
        CHECK(json_bool(json_arr_get(a, 2)) == 1);
        CHECK(json_type(json_arr_get(a, 3)) == JNULL);
        CHECK(json_bool(json_arr_get(a, 3)) == 0);
        b = json_obj_get(r, "b");
        CHECK(b != NULL && json_type(b) == JNUM);
        CHECK(json_num(b) == -2.5);
        CHECK(json_obj_get(r, "nope") == NULL);
        CHECK(json_arr_get(a, -1) == NULL);
        CHECK(json_arr_get(a, 4) == NULL);
        json_free(r);
    }

    /* --- malformed / truncated input: NULL result + non-empty err --- */
    err[0] = '\0';
    CHECK(json_parse("{\"a\":}", 6, err, sizeof err) == NULL);
    CHECK(err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"a\":1", 6, err, sizeof err) == NULL);
    CHECK(err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("", 0, err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("   ", 3, err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"a\":1} x", 9, err, sizeof err) == NULL && err[0] != '\0');
    /* NULL err buffer must be tolerated */
    CHECK(json_parse("{\"a\":}", 6, NULL, 0) == NULL);
    CHECK(json_parse(NULL, 0, err, sizeof err) == NULL);

    /* --- whitespace tolerance: servers pretty-print with ", " / "\n  "
     * separators (found by the Task 20 stream=false e2e: json.dumps
     * default separators broke object key parsing after a comma) --- */
    {
        static const char doc[] =
            "{\r\n  \"choices\": [\r\n    {\r\n      \"message\": {\r\n"
            "        \"role\": \"assistant\",\r\n        \"content\": \"x\"\r\n"
            "      },\r\n      \"finish_reason\": \"stop\"\r\n    }\r\n  ]\r\n}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        a = json_obj_get(r, "choices");
        CHECK(a != NULL && json_type(a) == JARR && json_arr_len(a) == 1);
        b = json_obj_get(json_arr_get(a, 0), "message");
        CHECK(b != NULL
              && strcmp(json_str(json_obj_get(b, "content")), "x") == 0);
        CHECK(strcmp(json_str(json_obj_get(json_arr_get(a, 0),
                                           "finish_reason")),
                     "stop") == 0);
        json_free(r);
    }
    {
        static const char doc[] = "{\"a\": 1, \"b\": [1, 2, 3], \"c\": {\"d\": true, \"e\": null}}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        CHECK(json_num(json_obj_get(r, "a")) == 1.0);
        CHECK(json_arr_len(json_obj_get(r, "b")) == 3);
        CHECK(json_bool(json_obj_get(json_obj_get(r, "c"), "d")) == 1);
        CHECK(json_type(json_obj_get(json_obj_get(r, "c"), "e")) == JNULL);
        json_free(r);
    }

    /* --- \uXXXX unescape: BMP --- */
    {
        static const char doc[] = "{\"s\":\"\\u4e2d\"}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        CHECK(strcmp(json_str(json_obj_get(r, "s")), "\xe4\xb8\xad") == 0);
        json_free(r);
    }

    /* --- \uXXXX unescape: surrogate pair D83D DE00 -> U+1F600 --- */
    {
        static const char doc[] = "{\"s\":\"\\ud83d\\ude00\"}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        s = json_str(json_obj_get(r, "s"));
        CHECK(s != NULL && strlen(s) == 4);
        CHECK((unsigned char)s[0] == 0xF0u);
        CHECK((unsigned char)s[1] == 0x9Fu);
        CHECK((unsigned char)s[2] == 0x98u);
        CHECK((unsigned char)s[3] == 0x80u);
        json_free(r);
    }

    /* --- short escapes and mixed content --- */
    {
        static const char doc[] = "{\"s\":\"a\\u0041\\n\\t\\\\\\\"\\/\\b\\f\\r\"}";

        err[0] = '\0';
        r = json_parse(doc, (int)sizeof(doc) - 1, err, sizeof err);
        CHECK(r != NULL);
        CHECK(strcmp(json_str(json_obj_get(r, "s")), "aA\n\t\\\"/\b\f\r") == 0);
        json_free(r);
    }

    /* --- invalid escapes and broken surrogates must be rejected --- */
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"\\uZZZZ\"}", (int)strlen("{\"s\":\"\\uZZZZ\"}"), err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"\\ud83d\"}", (int)strlen("{\"s\":\"\\ud83d\"}"), err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"\\ud83d\\u0041\"}", (int)strlen("{\"s\":\"\\ud83d\\u0041\"}"), err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"\\udc00\"}", (int)strlen("{\"s\":\"\\udc00\"}"), err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"\\q\"}", 10, err, sizeof err) == NULL && err[0] != '\0');
    /* raw control character inside a string is not valid JSON */
    err[0] = '\0';
    CHECK(json_parse("{\"s\":\"a\nb\"}", 11, err, sizeof err) == NULL && err[0] != '\0');

    /* --- empty containers and nesting --- */
    r = json_parse("{}", 2, err, sizeof err);
    CHECK(r != NULL && json_type(r) == JOBJ && json_obj_get(r, "a") == NULL);
    json_free(r);
    r = json_parse("[]", 2, err, sizeof err);
    CHECK(r != NULL && json_type(r) == JARR && json_arr_len(r) == 0);
    json_free(r);
    r = json_parse("[[1,2],[3]]", 11, err, sizeof err);
    CHECK(r != NULL && json_arr_len(r) == 2);
    CHECK(json_arr_len(json_arr_get(r, 0)) == 2);
    CHECK(json_num(json_arr_get(json_arr_get(r, 0), 1)) == 2.0);
    CHECK(json_arr_len(json_arr_get(r, 1)) == 1);
    json_free(r);
    r = json_parse("{\"o\":{\"k\":[true,false]}}", 24, err, sizeof err);
    CHECK(r != NULL);
    b = json_obj_get(json_obj_get(r, "o"), "k");
    CHECK(b != NULL && json_arr_len(b) == 2);
    CHECK(json_bool(json_arr_get(b, 0)) == 1);
    CHECK(json_bool(json_arr_get(b, 1)) == 0);
    json_free(r);

    /* --- number grammar: exponent / fraction / strictness --- */
    r = json_parse("[1e3,0.5,-0.25]", 15, err, sizeof err);
    CHECK(r != NULL);
    CHECK(json_num(json_arr_get(r, 0)) == 1000.0);
    CHECK(json_num(json_arr_get(r, 1)) == 0.5);
    CHECK(json_num(json_arr_get(r, 2)) == -0.25);
    json_free(r);
    err[0] = '\0';
    CHECK(json_parse("[01]", 4, err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("[1.]", 4, err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("[+1]", 4, err, sizeof err) == NULL && err[0] != '\0');
    err[0] = '\0';
    CHECK(json_parse("[.5]", 4, err, sizeof err) == NULL && err[0] != '\0');

    /* --- builder: byte-exact output, comma placement, escaping --- */
    jb_init(&jb, buf, (int)sizeof buf);
    jb_obj_begin(&jb);
      jb_key(&jb, "a"); jb_int(&jb, 1);
      jb_key(&jb, "b");
      jb_arr_begin(&jb);
        jb_int(&jb, 2); jb_int(&jb, 3);
        jb_arr_begin(&jb); jb_bool(&jb, 1); jb_bool(&jb, 0); jb_arr_end(&jb);
      jb_arr_end(&jb);
      jb_key(&jb, "c"); jb_num(&jb, -2.5);
      jb_key(&jb, "d"); jb_str(&jb, "x\"\\\n");
    jb_obj_end(&jb);
    CHECK(jb.overflow == 0);
    CHECK(strcmp(buf, "{\"a\":1,\"b\":[2,3,[true,false]],\"c\":-2.5,\"d\":\"x\\\"\\\\\\n\"}") == 0);

    /* empty containers via builder */
    jb_init(&jb, buf, (int)sizeof buf);
    jb_obj_begin(&jb); jb_obj_end(&jb);
    CHECK(jb.overflow == 0 && strcmp(buf, "{}") == 0);
    jb_init(&jb, buf, (int)sizeof buf);
    jb_arr_begin(&jb); jb_arr_end(&jb);
    CHECK(jb.overflow == 0 && strcmp(buf, "[]") == 0);
    jb_init(&jb, buf, (int)sizeof buf);
    jb_arr_begin(&jb);
      jb_obj_begin(&jb); jb_key(&jb, "k"); jb_int(&jb, 1); jb_obj_end(&jb);
      jb_obj_begin(&jb); jb_obj_end(&jb);
    jb_arr_end(&jb);
    CHECK(jb.overflow == 0 && strcmp(buf, "[{\"k\":1},{}]") == 0);

    /* --- builder -> parser round trip --- */
    jb_init(&jb, buf, (int)sizeof buf);
    jb_obj_begin(&jb);
      jb_key(&jb, "msg"); jb_str(&jb, "hi \xe4\xb8\xad \xf0\x9f\x98\x80");
      jb_key(&jb, "n");   jb_int(&jb, -42);
      jb_key(&jb, "f");   jb_num(&jb, 0.1);
      jb_key(&jb, "big"); jb_num(&jb, 1e300);
      jb_key(&jb, "t");   jb_bool(&jb, 1);
      jb_key(&jb, "u");   jb_bool(&jb, 0);
      jb_key(&jb, "z");   jb_raw(&jb, "null");
    jb_obj_end(&jb);
    CHECK(jb.overflow == 0);
    err[0] = '\0';
    r = json_parse(buf, jb.len, err, sizeof err);
    CHECK(r != NULL);
    CHECK(strcmp(json_str(json_obj_get(r, "msg")), "hi \xe4\xb8\xad \xf0\x9f\x98\x80") == 0);
    CHECK(json_num(json_obj_get(r, "n")) == -42.0);
    CHECK(json_num(json_obj_get(r, "f")) == 0.1);
    CHECK(json_num(json_obj_get(r, "big")) == 1e300);
    CHECK(json_bool(json_obj_get(r, "t")) == 1);
    CHECK(json_bool(json_obj_get(r, "u")) == 0);
    CHECK(json_type(json_obj_get(r, "z")) == JNULL);
    json_free(r);

    /* --- overflow: flag set, len capped, nothing written past cap --- */
    memset(guard, 'X', sizeof guard);
    jb_init(&jb, guard, 8);
    CHECK(jb.overflow == 0 && jb.len == 0 && guard[0] == '\0');
    jb_obj_begin(&jb);
    jb_key(&jb, "aaaaaaaa");
    jb_str(&jb, "bbbbbbbb");
    jb_obj_end(&jb);
    jb_int(&jb, 1234567);
    CHECK(jb.overflow == 1);
    CHECK(jb.len <= 8 - 1);
    CHECK(guard[7] == '\0');
    CHECK(guard[8] == 'X');
    CHECK(guard[15] == 'X');

    if (g_fail == 0) {
        printf("PASS test_json\n");
        return 0;
    }
    printf("FAIL test_json (%d failures)\n", g_fail);
    return 1;
}
