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
    S_TAB_DEMO,          /* temp demo tab title prefix (Task 14; removed by 17/20) */
    /* temp demo conversation content (Task 15; removed with DEMO_TABS by 17/20) */
    S_DEMO_U_HELLO, S_DEMO_A_HELLO, S_DEMO_U_POEM, S_DEMO_A_POEM,
    S_DEMO_U_QE, S_DEMO_A_QE_THINK, S_DEMO_A_QE,
    S_DEMO_U_TCP, S_DEMO_A_TCP_INT, S_DEMO_A_TCP_BODY,
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
    /* S_TAB_DEMO */         "演示会话",
    /* S_DEMO_U_HELLO */     "你好！😀 很高兴见到你 ☺",
    /* S_DEMO_A_HELLO */     "你好！我是 LLMChat32，一个兼容 Windows 98 的 LLM 聊天客户端。试着点击上方的标签页切换会话吧。🙂",
    /* S_DEMO_U_POEM */      "帮我写一句关于秋天的话 🍂",
    /* S_DEMO_A_POEM */      "秋风起，落叶黄；晴空一鹤排云上，便引诗情到碧霄。🍁 ✨ ★",
    /* S_DEMO_U_QE */        "什么是量子纠缠？能配张图吗？",
    /* S_DEMO_A_QE_THINK */  "用户在问量子纠缠，需要一个通俗比喻和配图。",
    /* S_DEMO_A_QE */        "量子纠缠是指两个粒子形成关联后，无论相隔多远，测量其中一个会即时决定另一个的状态。🌀 可以把它想成一双分发的手套：看到一只左手，立刻知道远方那只是右手。🧤",
    /* S_DEMO_U_TCP */       "详细讲讲 TCP 的三次握手 🤝",
    /* S_DEMO_A_TCP_INT */   "好的！下面是一段比较长的讲解，用来测试折行和滚动条。📡",
    /* S_DEMO_A_TCP_BODY */  "第一次握手：客户端发送 SYN 报文（序号 seq=x），进入 SYN_SENT 状态。第二次握手：服务端回复 SYN+ACK（序号 seq=y，确认号 ack=x+1），进入 SYN_RCVD 状态。第三次握手：客户端再发送 ACK（确认号 ack=y+1），双方进入 ESTABLISHED 状态。🌐 三次握手同时确认了双方的发送与接收能力，并同步初始序号，避免历史重复连接请求造成混乱。"
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
    /* S_TAB_DEMO */         "Demo chat",
    /* S_DEMO_U_HELLO */     "Hi there! 😀 Nice to meet you ☺",
    /* S_DEMO_A_HELLO */     "Hello! I am LLMChat32, an LLM chat client compatible with Windows 98. Click the tabs above to switch sessions. 🙂",
    /* S_DEMO_U_POEM */      "Write me a line about autumn 🍂",
    /* S_DEMO_A_POEM */      "Autumn wind rises, leaves turn yellow; one crane pierces the cloudless sky. 🍁 ✨ ★",
    /* S_DEMO_U_QE */        "What is quantum entanglement? Can you attach a picture?",
    /* S_DEMO_A_QE_THINK */  "The user asks about entanglement; a plain analogy plus an image.",
    /* S_DEMO_A_QE */        "Entanglement: once two particles correlate, measuring one instantly fixes the other's state, however far apart. 🌀 Think of a pair of gloves mailed to two cities: seeing a left-hand one tells you the far one is right-handed. 🧤",
    /* S_DEMO_U_TCP */       "Explain the TCP three-way handshake in detail 🤝",
    /* S_DEMO_A_TCP_INT */   "Sure! The long explanation below exercises line wrapping and the scrollbar. 📡",
    /* S_DEMO_A_TCP_BODY */  "First handshake: the client sends a SYN (seq=x) and enters SYN_SENT. Second: the server replies SYN+ACK (seq=y, ack=x+1) and enters SYN_RCVD. Third: the client sends ACK (ack=y+1) and both enter ESTABLISHED. 🌐 The handshake verifies both directions and synchronizes initial sequence numbers, so stale duplicate connection requests cause no confusion."
};

#else

extern const char *const g_strs_zh[S__COUNT];
extern const char *const g_strs_en[S__COUNT];

#endif /* STRINGS_DEFINE_TABLES */

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_UI_STRINGS_H */
