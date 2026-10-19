/*
 * config.h - application configuration structs and load/save.
 *
 * (ruling) config_save rewrites the entire file from the known fields
 * only: keys that config_load does not recognize are NOT preserved,
 * and comments are lost on save. Accepted trade-off (plan ruling,
 * spec §7): user-edited comments and unknown keys vanish when the
 * program saves the file. This keeps toml.c a minimal subset parser
 * (YAGNI) - no preserve-area machinery.
 *
 * Save strategy: write "<path>.tmp" first, then rename over <path>;
 * any failure (including a full disk) returns non-zero with a message
 * in err and leaves the in-memory Config untouched (it is const here).
 *
 * Load semantics: a missing file is not an error - config_defaults()
 * values stay in place and config_load() returns 0. Malformed TOML
 * returns non-zero with a readable err message.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_CONFIG_H
#define LLMCHAT32_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char font_name[64]; int font_size; char history_dir[128];
  char bg_image[256]; int bg_opacity; int window_width; int window_height;
} GeneralCfg;
typedef struct { char host[128]; int port; char username[64]; char password[64]; } ProxyCfg;
typedef struct {
  char name[64]; char api[16];        /* "anthropic"|"chat"|"responses" */
  char base_url[256]; char api_key[256]; char model_id[128];
  int use_proxy, stream, reasoning, verify_tls;
  char reasoning_effort[16];          /* low|medium|high */
  long thinking_budget, max_tokens, max_context_chars;
  double temperature;                 /* <0 不传 */
} ModelCfg;
typedef struct { GeneralCfg g; ProxyCfg p; ModelCfg m[32]; int n_models; } Config;

/* Fill cfg with defaults; any pre-existing content is discarded. */
void config_defaults(Config*);

/* Parse the TOML file at path into *cfg (defaults first, then overlay).
 * Missing file: defaults stay, returns 0.
 * Malformed file: non-zero, err set, *cfg holds whatever parsed so far. */
int config_load(Config*, const char *path, char *err, int errcap); /* 文件缺失→默认值，返回0 */

/* Rewrite the whole file from *cfg (known fields only; see ruling above).
 * tmp+rename; non-zero + err on any failure. */
int config_save(const Config*, const char *path, char *err, int errcap);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_CONFIG_H */
