/*
 * strings.h - UI string table (i18n base layer).
 *
 * Every piece of user-visible text is stored as a narrow UTF-8 literal in
 * one of the two tables below and fetched through LS(). The UI layer may
 * only ever hand these bytes to the two conversion outlets in ui.c
 * (ui_text_w/ui_drawtext_w via utf8_to_utf16, ui_set_a/ui_msg_a/menu
 * helpers via CP_ACP); never to an ANSI API directly.
 *
 * Language: ui_lang_init() maps GetSystemDefaultLangID() to a table -
 * primary language 0x04 (Chinese) selects g_strs_zh, anything else
 * g_strs_en.
 *
 * The tables are DEFINED in this file, exactly once: ui.c includes it
 * with STRINGS_DEFINE_TABLES set; everywhere else they are extern
 * declarations. All strings are C89 narrow literals, UTF-8, no BOM.
 */
#ifndef LLMCHAT32_UI_STRINGS_H
#define LLMCHAT32_UI_STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    S_MENU_FILE, S_MENU_NEW_CHAT, S_MENU_CLOSE_CHAT, S_MENU_EXIT,
    S_MENU_EDIT, S_MENU_RECALL, S_MENU_CLEAR, S_MENU_OPTIONS, S_MENU_MODEL_CFG,
    S_MENU_HELP, S_MENU_ABOUT,
    S_STATUS_READY, S_STATUS_SENDING, S_STATUS_ABORTED,
    S_BTN_SEND, S_BTN_ABORT, S_THINK_LINK, S_IMAGE_LINK,
    S_CONFIRM_DEL_SESSION, S_SAVE_FAILED, S_ABOUT_TEXT,
    S_SEND_FAILED,       /* app_send could not start (OOM / too many tabs) */
    S_THINK_TITLE,       /* Task 18 view dialogs */
    S_IMAGE_TITLE, S_DLG_OK, S_DOWNLOADING, S_DOWNLOAD_FAILED, S_SAVE_AS,
    S_IMG_FILTER,        /* GetSaveFileNameA filter description */
    S_ALL_FILES,         /* GetSaveFileNameA "all files" entry */
    S_DECODE_FAILED,     /* fetch ok but the bytes are not an image */
    S_CFG_TITLE,         /* Task 19 model config dialog */
    S_CFG_ADD, S_CFG_DEL, S_CFG_UP, S_CFG_DOWN,
    S_CFG_NAME, S_CFG_API, S_CFG_BASEURL, S_CFG_APIKEY, S_CFG_MODELID,
    S_CFG_USEPROXY, S_CFG_STREAM, S_CFG_REASONING, S_CFG_VERIFYTLS,
    S_CFG_EFFORT, S_CFG_BUDGET, S_CFG_MAXTOKENS, S_CFG_MAXCTX, S_CFG_TEMP,
    S_CFG_NEWMODEL,      /* default name for an added model */
    S_CFG_OK, S_CFG_CANCEL,
    S_API_ANTHROPIC, S_API_CHAT, S_API_RESPONSES,  /* api dropdown display */
    S_EFF_LOW, S_EFF_MEDIUM, S_EFF_HIGH,           /* effort dropdown display */
    S_CFG_ERR_NAME, S_CFG_ERR_API, S_CFG_ERR_NUM, S_CFG_ERR_MAXTOK,
    S_CFG_ERR_FULL, S_CFG_ERR_NOMODEL,
    S_CONFIG_SAVED,      /* status-strip note after a successful save */
    S_RECALL_DONE,       /* status-strip note after a successful recall */
    S_RECALL_FAILED,     /* MessageBox title when a recall fails */
    S_CORRUPT_TITLE,     /* startup notice: session files skipped */
    S_CORRUPT_BODY,      /* startup notice: listing header (file names appended) */
    S__COUNT
} StrID;

/* Current-language lookup. Out-of-range ids yield "" (never NULL). */
const char *LS(StrID);

/* GetSystemDefaultLangID(): primary language 0x04 -> zh, else en. */
void ui_lang_init(void);

#ifdef STRINGS_DEFINE_TABLES

const char *const g_strs_zh[S__COUNT] = {
    /* S_MENU_FILE */        "文件(&F)",
    /* S_MENU_NEW_CHAT */    "新建会话",
    /* S_MENU_CLOSE_CHAT */  "关闭会话",
    /* S_MENU_EXIT */        "退出(&X)",
    /* S_MENU_EDIT */        "编辑(&E)",
    /* S_MENU_RECALL */      "撤回上一条(&R)",
    /* S_MENU_CLEAR */       "清空对话(&C)",
    /* S_MENU_OPTIONS */     "选项(&O)",
    /* S_MENU_MODEL_CFG */   "模型设置(&M)...",
    /* S_MENU_HELP */        "帮助(&H)",
    /* S_MENU_ABOUT */       "关于 LLMChat32(&A)...",
    /* S_STATUS_READY */     "就绪",
    /* S_STATUS_SENDING */   "发送中...",
    /* S_STATUS_ABORTED */   "已中止",
    /* S_BTN_SEND */         "发送",
    /* S_BTN_ABORT */        "中止",
    /* S_THINK_LINK */       "思考过程",
    /* S_IMAGE_LINK */       "[图片]",
    /* S_CONFIRM_DEL_SESSION */ "确定要删除该会话吗？",
    /* S_SAVE_FAILED */      "保存失败：",
    /* S_ABOUT_TEXT */       "LLMChat32 —— Windows 98 兼容的 LLM 聊天客户端。\nUTF-8 内核，极简依赖。",
    /* S_SEND_FAILED */      "发送失败",
    /* S_THINK_TITLE */      "思考过程",
    /* S_IMAGE_TITLE */      "图片",
    /* S_DLG_OK */           "确定",
    /* S_DOWNLOADING */      "下载中...",
    /* S_DOWNLOAD_FAILED */  "下载失败：",
    /* S_SAVE_AS */          "另存为(&S)...",
    /* S_IMG_FILTER */       "图片文件 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)",
    /* S_ALL_FILES */        "所有文件 (*.*)",
    /* S_DECODE_FAILED */    "无法解码图片",
    /* S_CFG_TITLE */        "模型设置",
    /* S_CFG_ADD */          "添加(&A)",
    /* S_CFG_DEL */          "删除(&D)",
    /* S_CFG_UP */           "上移(&U)",
    /* S_CFG_DOWN */         "下移(&O)",
    /* S_CFG_NAME */         "名称(&N):",
    /* S_CFG_API */          "API(&P):",
    /* S_CFG_BASEURL */      "Base URL(&B):",
    /* S_CFG_APIKEY */       "API Key(&K):",
    /* S_CFG_MODELID */      "模型 ID(&I):",
    /* S_CFG_USEPROXY */     "使用代理(&Y)",
    /* S_CFG_STREAM */       "流式输出(&S)",
    /* S_CFG_REASONING */    "推理(&R)",
    /* S_CFG_VERIFYTLS */    "验证 TLS 证书(&V)",
    /* S_CFG_EFFORT */       "推理强度(&E):",
    /* S_CFG_BUDGET */       "思考预算(&T):",
    /* S_CFG_MAXTOKENS */    "最大 tokens(&M):",
    /* S_CFG_MAXCTX */       "上下文上限(&C):",
    /* S_CFG_TEMP */         "温度(&D):",
    /* S_CFG_NEWMODEL */     "新模型",
    /* S_CFG_OK */           "确定",
    /* S_CFG_CANCEL */       "取消",
    /* S_API_ANTHROPIC */    "Anthropic",
    /* S_API_CHAT */         "Chat 补全",
    /* S_API_RESPONSES */    "Responses",
    /* S_EFF_LOW */          "低",
    /* S_EFF_MEDIUM */       "中",
    /* S_EFF_HIGH */         "高",
    /* S_CFG_ERR_NAME */     "名称不能为空。",
    /* S_CFG_ERR_API */      "存在未选择 API 类型的模型。",
    /* S_CFG_ERR_NUM */      "数字字段包含无效值。",
    /* S_CFG_ERR_MAXTOK */   "max_tokens 必须大于 0。",
    /* S_CFG_ERR_FULL */     "模型数量已达上限(32)。",
    /* S_CFG_ERR_NOMODEL */  "至少保留一个模型。",
    /* S_CONFIG_SAVED */     "配置已保存",
    /* S_RECALL_DONE */      "已撤回",
    /* S_RECALL_FAILED */    "撤回失败",
    /* S_CORRUPT_TITLE */    "会话加载失败",
    /* S_CORRUPT_BODY */     "以下会话文件已损坏，启动时已跳过：\n"
};

const char *const g_strs_en[S__COUNT] = {
    /* S_MENU_FILE */        "&File",
    /* S_MENU_NEW_CHAT */    "&New chat",
    /* S_MENU_CLOSE_CHAT */  "&Close chat",
    /* S_MENU_EXIT */        "E&xit",
    /* S_MENU_EDIT */        "&Edit",
    /* S_MENU_RECALL */      "&Recall last message",
    /* S_MENU_CLEAR */       "&Clear conversation",
    /* S_MENU_OPTIONS */     "&Options",
    /* S_MENU_MODEL_CFG */   "Model &settings...",
    /* S_MENU_HELP */        "&Help",
    /* S_MENU_ABOUT */       "&About LLMChat32...",
    /* S_STATUS_READY */     "Ready",
    /* S_STATUS_SENDING */   "Sending...",
    /* S_STATUS_ABORTED */   "Aborted",
    /* S_BTN_SEND */         "&Send",
    /* S_BTN_ABORT */        "A&bort",
    /* S_THINK_LINK */       "Thinking process",
    /* S_IMAGE_LINK */       "[image]",
    /* S_CONFIRM_DEL_SESSION */ "Delete this session?",
    /* S_SAVE_FAILED */      "Save failed: ",
    /* S_ABOUT_TEXT */       "LLMChat32 - a Windows 98 compatible LLM chat client.\nUTF-8 core, minimal dependencies.",
    /* S_SEND_FAILED */      "Send failed",
    /* S_THINK_TITLE */      "Thinking process",
    /* S_IMAGE_TITLE */      "Image",
    /* S_DLG_OK */           "OK",
    /* S_DOWNLOADING */      "Downloading...",
    /* S_DOWNLOAD_FAILED */  "Download failed: ",
    /* S_SAVE_AS */          "Save &As...",
    /* S_IMG_FILTER */       "Image files (*.png;*.jpg;*.jpeg;*.bmp;*.gif)",
    /* S_ALL_FILES */        "All Files (*.*)",
    /* S_DECODE_FAILED */    "Cannot decode image",
    /* S_CFG_TITLE */        "Model settings",
    /* S_CFG_ADD */          "&Add",
    /* S_CFG_DEL */          "&Delete",
    /* S_CFG_UP */           "Move &Up",
    /* S_CFG_DOWN */         "Move D&own",
    /* S_CFG_NAME */         "&Name:",
    /* S_CFG_API */          "A&PI:",
    /* S_CFG_BASEURL */      "&Base URL:",
    /* S_CFG_APIKEY */       "API &Key:",
    /* S_CFG_MODELID */      "&Model ID:",
    /* S_CFG_USEPROXY */     "Use pro&xy",
    /* S_CFG_STREAM */       "&Stream",
    /* S_CFG_REASONING */    "&Reasoning",
    /* S_CFG_VERIFYTLS */    "&Verify TLS certificate",
    /* S_CFG_EFFORT */       "Reasoning &effort:",
    /* S_CFG_BUDGET */       "Thinking &budget:",
    /* S_CFG_MAXTOKENS */    "Ma&x tokens:",
    /* S_CFG_MAXCTX */       "Ma&x context chars:",
    /* S_CFG_TEMP */         "Tempe&rature:",
    /* S_CFG_NEWMODEL */     "New model",
    /* S_CFG_OK */           "OK",
    /* S_CFG_CANCEL */       "Cancel",
    /* S_API_ANTHROPIC */    "Anthropic",
    /* S_API_CHAT */         "Chat Completions",
    /* S_API_RESPONSES */    "Responses",
    /* S_EFF_LOW */          "Low",
    /* S_EFF_MEDIUM */       "Medium",
    /* S_EFF_HIGH */         "High",
    /* S_CFG_ERR_NAME */     "Name must not be empty.",
    /* S_CFG_ERR_API */      "A model has no API type selected.",
    /* S_CFG_ERR_NUM */      "A number field has an invalid value.",
    /* S_CFG_ERR_MAXTOK */   "max_tokens must be greater than 0.",
    /* S_CFG_ERR_FULL */     "Model list is full (32).",
    /* S_CFG_ERR_NOMODEL */  "Keep at least one model.",
    /* S_CONFIG_SAVED */     "Configuration saved",
    /* S_RECALL_DONE */      "Message recalled",
    /* S_RECALL_FAILED */    "Recall failed",
    /* S_CORRUPT_TITLE */    "Failed to load sessions",
    /* S_CORRUPT_BODY */     "The following session files were corrupt and were skipped:\n"
};

#else

extern const char *const g_strs_zh[S__COUNT];
extern const char *const g_strs_en[S__COUNT];

#endif /* STRINGS_DEFINE_TABLES */

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_STRINGS_H */
