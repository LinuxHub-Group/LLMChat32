/*
 * test_history.c - session model and JSON history persistence tests.
 *
 * Console app (built with -bc). Verifies the task 6 assertions:
 * save->load round trip with system/user/assistant messages holding
 * text/think/image blocks (all fields equal); injected write failure
 * (read-only directory) makes history_save fail with a non-empty err
 * while the in-memory Session is untouched; a hand-truncated JSON file
 * makes history_load fail without crashing and the session rolls back
 * to an empty-but-savable state; history_list returns ids in
 * lexicographic order; history_delete removes the file.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_history.exe ^
 *          src\tests\test_history.c src\core\session.c ^
 *          src\core\history.c src\core\json.c src\core\utf.c
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../core/session.h"
#include "../core/history.h"

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

#define DIR_RW  "tests\\tmp_hist6"
#define DIR_RO  "tests\\tmp_hist6_ro"
#define DIR_BAD "tests\\tmp_hist6_bad"
#define DIR_LST "tests\\tmp_hist6_lst"
#define PATH_ID (DIR_RW "\\abc123.json")
#define PATH_BAD (DIR_BAD "\\bad.json")

static void write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

int main(void)
{
    char err[128];
    Session *s;
    Session *s2;
    Session *s3;
    Msg *msg;
    Block *blk;
    char ids[8][24];
    int n;
    int i;
    int j;
    FILE *fp;

    CreateDirectoryA(DIR_RW, NULL);
    CreateDirectoryA(DIR_RO, NULL);
    CreateDirectoryA(DIR_BAD, NULL);
    CreateDirectoryA(DIR_LST, NULL);
    SetFileAttributesA(DIR_RO, FILE_ATTRIBUTE_READONLY);

    /* --- build a session: system + user + assistant, all block kinds --- */
    s = session_new("abc123");
    CHECK(s != NULL);
    CHECK(strcmp(s->id, "abc123") == 0);
    CHECK(s->nm == 0 && s->m == NULL);

    strcpy(s->title, "hello \xe4\xb8\xad\xe6\x96\x87 \"q\"");

    msg = session_add_msg(s, ROLE_SYSTEM);
    CHECK(msg != NULL && s->nm == 1);
    blk = msg_add_block(msg, BLOCK_TEXT, "be helpful");
    CHECK(blk != NULL && blk->kind == BLOCK_TEXT);
    CHECK(strcmp(blk->text, "be helpful") == 0);

    msg = session_add_msg(s, ROLE_USER);
    msg_add_block(msg, BLOCK_TEXT, "line1\nline2 \xe4\xb8\xad");
    msg_add_block(msg, BLOCK_IMAGE, "C:\\pics\\a.png");

    msg = session_add_msg(s, ROLE_ASSIST);
    msg_add_block(msg, BLOCK_THINK, "thinking...\t\"q\" \\");
    msg_add_block(msg, BLOCK_TEXT, "answer");
    CHECK(s->nm == 3);
    CHECK(s->m[1].nb == 2 && s->m[2].nb == 2);

    /* --- save -> load round trip: every field must survive --- */
    err[0] = '\0';
    CHECK(history_save(s, DIR_RW, err, sizeof err) == 0);
    CHECK(err[0] == '\0');
    /* tmp sibling must be gone after the rename */
    fp = fopen(DIR_RW "\\abc123.json.tmp", "rb");
    CHECK(fp == NULL);
    if (fp)
        fclose(fp);

    s2 = session_new("abc123");
    err[0] = '\0';
    CHECK(history_load(s2, PATH_ID, err, sizeof err) == 0);
    CHECK(strcmp(s2->id, "abc123") == 0);
    CHECK(strcmp(s2->title, s->title) == 0);
    CHECK(s2->nm == 3);
    {
        static const int want_roles[3] =
            { ROLE_SYSTEM, ROLE_USER, ROLE_ASSIST };
        static const int want_kinds[3][2] =
            { { BLOCK_TEXT, -1 }, { BLOCK_TEXT, BLOCK_IMAGE },
              { BLOCK_THINK, BLOCK_TEXT } };
        static const char *const want_text[3][2] = {
            { "be helpful", NULL },
            { "line1\nline2 \xe4\xb8\xad", "C:\\pics\\a.png" },
            { "thinking...\t\"q\" \\", "answer" }
        };

        for (i = 0; i < 3; i++) {
            CHECK(s2->m[i].role == want_roles[i]);
            CHECK(s2->m[i].nb == (want_kinds[i][1] < 0 ? 1 : 2));
            for (j = 0; j < s2->m[i].nb; j++) {
                CHECK(s2->m[i].b[j].kind == want_kinds[i][j]);
                CHECK(strcmp(s2->m[i].b[j].text, want_text[i][j]) == 0);
            }
        }
    }

    /* --- injected write failure: read-only dir -> fail, memory intact --- */
    err[0] = '\0';
    CHECK(history_save(s, DIR_RO, err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    /* Session memory untouched */
    CHECK(s->nm == 3 && s->m[0].nb == 1 && s->m[1].nb == 2 &&
          s->m[2].nb == 2);
    CHECK(strcmp(s->m[0].b[0].text, "be helpful") == 0);
    CHECK(strcmp(s->m[2].b[1].text, "answer") == 0);
    CHECK(strcmp(s->title, "hello \xe4\xb8\xad\xe6\x96\x87 \"q\"") == 0);
    /* nothing leaked into the read-only dir */
    fp = fopen(DIR_RO "\\abc123.json", "rb");
    CHECK(fp == NULL);
    if (fp)
        fclose(fp);

    /* --- corrupted JSON (hand-truncated): load fails, no crash --- */
    write_file(PATH_BAD, "{\"id\":\"bad\",\"title\":\"x\",\"msgs\":[{");
    s3 = session_new("bad");
    strcpy(s3->title, "old title");
    err[0] = '\0';
    CHECK(history_load(s3, PATH_BAD, err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    /* rolled back to an empty session that can be saved again */
    CHECK(s3->nm == 0 && s3->m == NULL);
    err[0] = '\0';
    CHECK(history_save(s3, DIR_BAD, err, sizeof err) == 0);

    /* --- invalid-UTF-8 title (written by older autotitle bug): the
       load still succeeds but the title is dropped so autotitle can
       retry on the next send --- */
    write_file(DIR_BAD "\\badt.json",
               "{\"id\":\"badt\",\"title\":\"\xff\xfe broken\","
               "\"msgs\":[{\"role\":\"user\","
               "\"blocks\":[{\"k\":\"text\",\"t\":\"hi\"}]}]}");
    strcpy(s3->title, "old title");
    err[0] = '\0';
    CHECK(history_load(s3, DIR_BAD "\\badt.json", err, sizeof err) == 0);
    CHECK(s3->title[0] == '\0');
    CHECK(s3->nm == 1 && strcmp(s3->m[0].b[0].text, "hi") == 0);
    /* a well-formed CJK title survives untouched */
    write_file(DIR_BAD "\\goodt.json",
               "{\"id\":\"goodt\",\"title\":\"\xe4\xb8\xad\xe6\x96\x87\","
               "\"msgs\":[]}");
    err[0] = '\0';
    CHECK(history_load(s3, DIR_BAD "\\goodt.json", err, sizeof err) == 0);
    CHECK(strcmp(s3->title, "\xe4\xb8\xad\xe6\x96\x87") == 0);

    /* --- missing file is a hard error for history_load --- */
    err[0] = '\0';
    CHECK(history_load(s3, DIR_RW "\\no_such_id.json", err, sizeof err) != 0);
    CHECK(s3->nm == 0 && s3->m == NULL);

    /* --- history_list: lexicographic order regardless of creation --- */
    write_file(DIR_LST "\\1.json", "{}");
    write_file(DIR_LST "\\10.json", "{}");
    write_file(DIR_LST "\\2.json", "{}");
    n = history_list(DIR_LST, ids, 8);
    CHECK(n == 3);
    CHECK(n == 3 && strcmp(ids[0], "1") == 0);
    CHECK(n == 3 && strcmp(ids[1], "10") == 0);
    CHECK(n == 3 && strcmp(ids[2], "2") == 0);
    /* max cap is respected */
    n = history_list(DIR_LST, ids, 2);
    CHECK(n == 2);

    /* --- history_delete: file disappears --- */
    CHECK(history_delete(DIR_LST, "2") == 0);
    fp = fopen(DIR_LST "\\2.json", "rb");
    CHECK(fp == NULL);
    if (fp)
        fclose(fp);
    n = history_list(DIR_LST, ids, 8);
    CHECK(n == 2 && strcmp(ids[0], "1") == 0 && strcmp(ids[1], "10") == 0);
    CHECK(history_delete(DIR_LST, "2") == 0);  /* already gone: not an error */

    /* --- teardown --- */
    session_free(s);
    session_free(s2);
    session_free(s3);

    remove(PATH_ID);
    remove(DIR_RW "\\abc123.json.tmp");
    remove(DIR_BAD "\\bad.json");
    remove(DIR_BAD "\\bad.json.tmp");
    remove(DIR_BAD "\\badt.json");
    remove(DIR_BAD "\\badt.json.tmp");
    remove(DIR_BAD "\\goodt.json");
    remove(DIR_BAD "\\goodt.json.tmp");
    remove(DIR_BAD "\\.json");
    remove(DIR_BAD "\\.json.tmp");
    remove(DIR_LST "\\1.json");
    remove(DIR_LST "\\10.json");
    remove(DIR_LST "\\2.json");
    SetFileAttributesA(DIR_RO, FILE_ATTRIBUTE_DIRECTORY);
    RemoveDirectoryA(DIR_RW);
    RemoveDirectoryA(DIR_RO);
    RemoveDirectoryA(DIR_BAD);
    RemoveDirectoryA(DIR_LST);

    if (g_fail == 0) {
        printf("PASS test_history\n");
        return 0;
    }
    printf("FAIL test_history (%d failures)\n", g_fail);
    return 1;
}
