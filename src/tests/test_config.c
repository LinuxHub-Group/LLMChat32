/*
 * test_config.c - TOML subset parser and Config load/save unit tests.
 *
 * Console app (built with -bc). Verifies the exact assertions from the
 * task brief: parsing the spec §7 sample document (with a second
 * [[models]] entry) field by field; '#' comment lines ignored;
 * config_load of a missing file leaves defaults in place (bg_opacity
 * 40, font_size 9) and returns 0; config_save output reloads losslessly
 * (every field equal, including the temperature <0 sentinel); saving
 * to an unwritable path fails with a non-empty error; malformed TOML
 * fails with a readable error instead of crashing.
 *
 * Prints one "ok ..." line per check and a final "PASS"/"FAIL" line.
 * Exit code: 0 on PASS, 1 on FAIL.
 *
 * Build (see root makefile):
 *   wcl386 -bt=nt -bm -wx -bc -fe=tests\test_config.exe ^
 *          src\tests\test_config.c src\core\toml.c src\core\config.c
 */
#include <stdio.h>
#include <string.h>

#include "../core/config.h"

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

/* The spec §7 sample document, plus a second [[models]] entry so that
 * n_models == 2 can be asserted. Non-ASCII appears only inside narrow
 * literals, per project rules. */
static const char g_sample[] =
    "# LLMChat32 config\n"
    "[general]\n"
    "font_name = \"SimSun\"\n"
    "font_size = 9\n"
    "history_dir = \"history\"\n"
    "bg_image = \"bg.png\"          # 空 = 无背景\n"
    "bg_opacity = 40              # 0-100，相对白色底\n"
    "window_width = 640\n"
    "window_height = 480\n"
    "\n"
    "[proxy]\n"
    "host = \"192.168.1.10\"\n"
    "port = 7890\n"
    "username = \"\"\n"
    "password = \"\"\n"
    "\n"
    "[[models]]\n"
    "name = \"Claude\"\n"
    "api = \"anthropic\"            # anthropic | chat | responses\n"
    "base_url = \"https://api.anthropic.com\"\n"
    "api_key = \"sk-ant-...\"\n"
    "model_id = \"claude-sonnet-4-5\"\n"
    "use_proxy = false\n"
    "stream = true\n"
    "reasoning = true\n"
    "reasoning_effort = \"medium\"  # low | medium | high\n"
    "thinking_budget = 8000       # anthropic 预算\n"
    "max_tokens = 4096\n"
    "max_context_chars = 120000\n"
    "temperature = -1             # <0 不传\n"
    "verify_tls = true\n"
    "\n"
    "[[models]]\n"
    "name = \"GPT\"\n"
    "api = \"chat\"\n"
    "base_url = \"https://api.openai.com\"\n"
    "api_key = \"sk-oai\"\n"
    "model_id = \"gpt-x\"\n"
    "use_proxy = true\n"
    "stream = false\n"
    "reasoning = false\n"
    "reasoning_effort = \"low\"\n"
    "thinking_budget = 2000\n"
    "max_tokens = 1024\n"
    "max_context_chars = 100\n"
    "temperature = 0.7\n"
    "verify_tls = false\n";

#define SAMPLE_PATH "__t5_sample.toml"
#define ROUNDTRIP_PATH "__t5_roundtrip.toml"

static int write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    size_t n;

    if (!fp)
        return -1;
    n = strlen(text);
    if (fwrite(text, 1, n, fp) != n) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

/* Field-by-field equality of two Config structs. */
static int cfg_equal(const Config *a, const Config *b)
{
    int i;

    if (strcmp(a->g.font_name, b->g.font_name) != 0)
        return 0;
    if (a->g.font_size != b->g.font_size)
        return 0;
    if (strcmp(a->g.history_dir, b->g.history_dir) != 0)
        return 0;
    if (strcmp(a->g.bg_image, b->g.bg_image) != 0)
        return 0;
    if (a->g.bg_opacity != b->g.bg_opacity)
        return 0;
    if (a->g.window_width != b->g.window_width)
        return 0;
    if (a->g.window_height != b->g.window_height)
        return 0;
    if (strcmp(a->p.host, b->p.host) != 0)
        return 0;
    if (a->p.port != b->p.port)
        return 0;
    if (strcmp(a->p.username, b->p.username) != 0)
        return 0;
    if (strcmp(a->p.password, b->p.password) != 0)
        return 0;
    if (a->n_models != b->n_models)
        return 0;
    for (i = 0; i < a->n_models; i++) {
        const ModelCfg *x = &a->m[i];
        const ModelCfg *y = &b->m[i];

        if (strcmp(x->name, y->name) != 0)
            return 0;
        if (strcmp(x->api, y->api) != 0)
            return 0;
        if (strcmp(x->base_url, y->base_url) != 0)
            return 0;
        if (strcmp(x->api_key, y->api_key) != 0)
            return 0;
        if (strcmp(x->model_id, y->model_id) != 0)
            return 0;
        if (x->use_proxy != y->use_proxy)
            return 0;
        if (x->stream != y->stream)
            return 0;
        if (x->reasoning != y->reasoning)
            return 0;
        if (x->verify_tls != y->verify_tls)
            return 0;
        if (strcmp(x->reasoning_effort, y->reasoning_effort) != 0)
            return 0;
        if (x->thinking_budget != y->thinking_budget)
            return 0;
        if (x->max_tokens != y->max_tokens)
            return 0;
        if (x->max_context_chars != y->max_context_chars)
            return 0;
        if (x->temperature != y->temperature)
            return 0;
    }
    return 1;
}

int main(void)
{
    Config cfg;
    Config back;
    char err[128];

    /* --- parse the spec §7 sample (plus second [[models]]) --- */
    CHECK(write_file(SAMPLE_PATH, g_sample) == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) == 0);
    CHECK(cfg.n_models == 2);
    /* general */
    CHECK(strcmp(cfg.g.font_name, "SimSun") == 0);
    CHECK(cfg.g.font_size == 9);
    CHECK(strcmp(cfg.g.history_dir, "history") == 0);
    CHECK(strcmp(cfg.g.bg_image, "bg.png") == 0);
    CHECK(cfg.g.bg_opacity == 40);
    CHECK(cfg.g.window_width == 640);
    CHECK(cfg.g.window_height == 480);
    /* proxy */
    CHECK(strcmp(cfg.p.host, "192.168.1.10") == 0);
    CHECK(cfg.p.port == 7890);
    CHECK(strcmp(cfg.p.username, "") == 0);
    CHECK(strcmp(cfg.p.password, "") == 0);
    /* models[0] - anthropic */
    CHECK(strcmp(cfg.m[0].name, "Claude") == 0);
    CHECK(strcmp(cfg.m[0].api, "anthropic") == 0);
    CHECK(strcmp(cfg.m[0].base_url, "https://api.anthropic.com") == 0);
    CHECK(strcmp(cfg.m[0].api_key, "sk-ant-...") == 0);
    CHECK(strcmp(cfg.m[0].model_id, "claude-sonnet-4-5") == 0);
    CHECK(cfg.m[0].use_proxy == 0);
    CHECK(cfg.m[0].stream == 1);
    CHECK(cfg.m[0].reasoning == 1);
    CHECK(strcmp(cfg.m[0].reasoning_effort, "medium") == 0);
    CHECK(cfg.m[0].thinking_budget == 8000);
    CHECK(cfg.m[0].max_tokens == 4096);
    CHECK(cfg.m[0].max_context_chars == 120000);
    CHECK(cfg.m[0].temperature == -1.0);
    CHECK(cfg.m[0].verify_tls == 1);
    /* models[1] - chat */
    CHECK(strcmp(cfg.m[1].name, "GPT") == 0);
    CHECK(strcmp(cfg.m[1].api, "chat") == 0);
    CHECK(cfg.m[1].use_proxy == 1);
    CHECK(cfg.m[1].stream == 0);
    CHECK(cfg.m[1].reasoning == 0);
    CHECK(strcmp(cfg.m[1].reasoning_effort, "low") == 0);
    CHECK(cfg.m[1].thinking_budget == 2000);
    CHECK(cfg.m[1].temperature == 0.7);
    CHECK(cfg.m[1].verify_tls == 0);
    remove(SAMPLE_PATH);

    /* --- minimal [[models]] entry: spec §5 defaults must fill the rest ---
       verify_tls on, streaming on, temperature sentinel, max_tokens sane;
       max_context_chars stays 0 (= unlimited). */
    {
        Config mini;

        CHECK(write_file(SAMPLE_PATH,
            "[[models]]\n"
            "name = \"Mini\"\n"
            "api = \"chat\"\n"
            "base_url = \"https://x\"\n"
            "api_key = \"k\"\n"
            "model_id = \"m\"\n") == 0);
        err[0] = '\0';
        CHECK(config_load(&mini, SAMPLE_PATH, err, sizeof err) == 0);
        CHECK(mini.n_models == 1);
        CHECK(mini.m[0].verify_tls == 1);
        CHECK(mini.m[0].stream == 1);
        CHECK(mini.m[0].temperature == -1.0);
        CHECK(mini.m[0].max_tokens == 4096);
        CHECK(mini.m[0].max_context_chars == 0);
        remove(SAMPLE_PATH);
    }

    /* --- CRLF line endings (Notepad/Notepad++ default) parse identically --- */
    {
        char crlf[sizeof(g_sample) * 2];
        const char *s = g_sample;
        char *d = crlf;
        Config cfg2;

        while (*s) {
            if (*s == '\n')
                *d++ = '\r';
            *d++ = *s++;
        }
        *d = '\0';
        CHECK(write_file(SAMPLE_PATH, crlf) == 0);
        err[0] = '\0';
        CHECK(config_load(&cfg2, SAMPLE_PATH, err, sizeof err) == 0);
        CHECK(cfg_equal(&cfg, &cfg2) == 1);
        remove(SAMPLE_PATH);
    }

    /* --- missing file: defaults, return 0 --- */
    err[0] = '\0';
    CHECK(config_load(&cfg, "__t5_no_such_file.toml", err, sizeof err) == 0);
    CHECK(cfg.g.bg_opacity == 40);
    CHECK(cfg.g.font_size == 11);
    CHECK(cfg.g.window_width == 640);
    CHECK(cfg.g.window_height == 480);
    CHECK(strcmp(cfg.g.history_dir, "history") == 0);
    CHECK(cfg.n_models == 0);

    /* --- save -> load round trip: every field survives --- */
    config_defaults(&cfg);
    strcpy(cfg.g.font_name, "MingLiU");
    cfg.g.font_size = 12;
    strcpy(cfg.g.history_dir, "hist dir with space");
    strcpy(cfg.g.bg_image, "bg\\path\".png");
    cfg.g.bg_opacity = 75;
    cfg.g.window_width = 800;
    cfg.g.window_height = 601;
    strcpy(cfg.p.host, "proxy.local");
    cfg.p.port = 1080;
    strcpy(cfg.p.username, "user\"x");
    strcpy(cfg.p.password, "p\\w");
    cfg.n_models = 2;
    strcpy(cfg.m[0].name, "Claude \"HQ\"");
    strcpy(cfg.m[0].api, "anthropic");
    strcpy(cfg.m[0].base_url, "https://api.anthropic.com");
    strcpy(cfg.m[0].api_key, "sk-ant-secret");
    strcpy(cfg.m[0].model_id, "claude-sonnet-4-5");
    cfg.m[0].use_proxy = 1;
    cfg.m[0].stream = 1;
    cfg.m[0].reasoning = 1;
    cfg.m[0].verify_tls = 1;
    strcpy(cfg.m[0].reasoning_effort, "high");
    cfg.m[0].thinking_budget = 32000;
    cfg.m[0].max_tokens = 8192;
    cfg.m[0].max_context_chars = 99999;
    cfg.m[0].temperature = -1.0;      /* sentinel: <0 means "do not send" */
    strcpy(cfg.m[1].name, "GPT");
    strcpy(cfg.m[1].api, "responses");
    strcpy(cfg.m[1].base_url, "https://api.openai.com");
    strcpy(cfg.m[1].api_key, "key\nwith\tescapes");
    strcpy(cfg.m[1].model_id, "o3");
    cfg.m[1].use_proxy = 0;
    cfg.m[1].stream = 0;
    cfg.m[1].reasoning = 0;
    cfg.m[1].verify_tls = 0;
    strcpy(cfg.m[1].reasoning_effort, "low");
    cfg.m[1].thinking_budget = 0;
    cfg.m[1].max_tokens = 1;
    cfg.m[1].max_context_chars = 0;
    cfg.m[1].temperature = 0.75;

    err[0] = '\0';
    CHECK(config_save(&cfg, ROUNDTRIP_PATH, err, sizeof err) == 0);
    memset(&back, 0, sizeof back);
    err[0] = '\0';
    CHECK(config_load(&back, ROUNDTRIP_PATH, err, sizeof err) == 0);
    CHECK(cfg_equal(&cfg, &back) == 1);
    /* the temperature sentinel survived the round trip */
    CHECK(back.m[0].temperature == -1.0);
    CHECK(back.m[1].temperature == 0.75);
    /* strings needing escapes survived byte-exact */
    CHECK(strcmp(back.g.bg_image, "bg\\path\".png") == 0);
    CHECK(strcmp(back.p.username, "user\"x") == 0);
    CHECK(strcmp(back.m[1].api_key, "key\nwith\tescapes") == 0);
    /* saving again over the existing file must also work (rename path) */
    err[0] = '\0';
    CHECK(config_save(&cfg, ROUNDTRIP_PATH, err, sizeof err) == 0);
    memset(&back, 0, sizeof back);
    err[0] = '\0';
    CHECK(config_load(&back, ROUNDTRIP_PATH, err, sizeof err) == 0);
    CHECK(cfg_equal(&cfg, &back) == 1);
    remove(ROUNDTRIP_PATH);

    /* --- unwritable save path: non-zero + non-empty err --- */
    err[0] = '\0';
    CHECK(config_save(&cfg, "no_such_dir_t5\\config.toml", err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    /* NULL err buffer tolerated */
    CHECK(config_save(&cfg, "no_such_dir_t5\\config.toml", NULL, 0) != 0);

    /* --- malformed TOML: error, not crash --- */
    CHECK(write_file(SAMPLE_PATH, "[general]\nfont_size = \"x\"\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) != 0);
    CHECK(err[0] != '\0');
    remove(SAMPLE_PATH);

    CHECK(write_file(SAMPLE_PATH, "[general\nfont_size = 9\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) != 0 && err[0] != '\0');
    remove(SAMPLE_PATH);

    CHECK(write_file(SAMPLE_PATH, "[general]\nfont_name = \"unterminated\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) != 0 && err[0] != '\0');
    remove(SAMPLE_PATH);

    CHECK(write_file(SAMPLE_PATH, "[general]\nfont_size\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) != 0 && err[0] != '\0');
    remove(SAMPLE_PATH);

    CHECK(write_file(SAMPLE_PATH, "[general]\nbg_opacity = 12x\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) != 0 && err[0] != '\0');
    remove(SAMPLE_PATH);

    /* unknown keys and unknown sections are skipped, not fatal */
    CHECK(write_file(SAMPLE_PATH,
        "future_key = 1\n"
        "[general]\n"
        "font_size = 11\n"
        "unknown_thing = \"zzz\"\n"
        "[future_section]\n"
        "whatever = true\n") == 0);
    err[0] = '\0';
    CHECK(config_load(&cfg, SAMPLE_PATH, err, sizeof err) == 0);
    CHECK(cfg.g.font_size == 11);
    remove(SAMPLE_PATH);

    /* NULL err buffer tolerated on load too */
    CHECK(write_file(SAMPLE_PATH, "[general\n") == 0);
    CHECK(config_load(&cfg, SAMPLE_PATH, NULL, 0) != 0);
    remove(SAMPLE_PATH);

    if (g_fail == 0) {
        printf("PASS test_config\n");
        return 0;
    }
    printf("FAIL test_config (%d failures)\n", g_fail);
    return 1;
}
