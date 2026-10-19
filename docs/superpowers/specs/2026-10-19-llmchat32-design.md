# LLMChat32 设计文档

- 日期：2026-10-19
- 状态：已获批准
- 目标平台：Windows 98（Win32），二进制亦可运行于现代 Windows
- 工具链：Open Watcom 2.0 官方测试版（`ow_portable_v2_stable/`，已存在于仓库）

## 1. 目标与范围

一个 Win32 + GDI 的 LLM 对话客户端：

- TOML 本地配置文件，可配置多套模型，每个模型可单独决定是否走代理
- 支持 Anthropic Messages、OpenAI Chat Completions、OpenAI Responses 三套协议
- 支持推理等级（reasoning_effort / Anthropic thinking budget）与上下文长度限制参数
- SSE 流式接收（可逐模型关闭，退回一次性 JSON）
- 本地保存会话（不做压缩），程序重启后恢复所有会话标签页
- iOS 短信风格气泡；思考过程默认隐藏，气泡顶部「思考过程」超链接弹窗查看；图片显示为 `[图片]` 超链接，弹窗中可查看并另存
- 聊天区背景图（stb_image 解码，锁定右下角，相对白色底可调透明度）
- 中英双语界面（简体中文版 / 英文版，按系统语言自动选择，其他语言一律英文）
- 右键自己发送的消息可撤回（删除该条及其后所有消息）
- 明确不做：markdown 渲染、MCP/tools、会话压缩、图片直接内嵌显示

## 2. 已验证的技术事实（spike 实测，wcl386 -bt=nt -bm）

- wcc386 对**无 BOM** 源码中的 UTF-8 窄字符串字面量做字节级透传，`%02X` 级 dump 验证一致，`-wx` 最高警告级别零警告
- 带 UTF-8 BOM 的源码直接编译失败（18 错误）→ 项目源文件一律无 BOM，构建脚本加 BOM 扫描兜底
- `L"中文"` 宽字面量按宿主机代码页解码，输出乱码且随开发机变化 → 禁用非 ASCII 宽字面量
- `\uXXXX` 转义不支持（原样输出字符）→ 禁用
- `wchar_t` 含 `<stddef.h>` 时为 16 位 `unsigned short`
- 工具链在中文路径（`D:\项目\...`）下工作正常
- wcl386 通过 `INCLUDE`/`LIB` 环境变量定位头文件与库（Open Watcom 惯例），`build.bat` 负责设置
- wolfSSL 2025 年起官方支持 Open Watcom（5.9.x）；WingGPT 项目已有 Win16/Win98 + wolfSSL 跑通 TLS 1.2/1.3 先例
- curl 官方早已放弃 Win9x 且不支持 Open Watcom → 不使用 curl，HTTP 自研

## 3. 编码与 i18n 规约（强制）

所有源文件：UTF-8、无 BOM、C89 风格。

**字符串进出 Win32 只有两个受控出口**：

- `ui_text_w()`：UTF-8 → `utf8_to_utf16()`（自有实现，不依赖系统代码页）→ `TextOutW/DrawTextW`
- `ui_text_a()`：UTF-8 → UTF-16 → `WideCharToMultiByte(CP_ACP)` → ANSI 控件/MessageBox
  - 简中 Win98（CP936）输出 GBK 字节；英文 Win98 输出 ASCII 字节
- 输入反向：编辑框 `GetWindowTextA` → GBK → `MultiByteToWideChar(CP_ACP)` → UTF-16 → UTF-8

**禁令**：任何位置不得把 UTF-8 字节直接传给 ANSI API（即"拿 GBK 规则读 UTF-8"是唯一致命错配，靠上述包装层从结构上杜绝）；禁止 `\u` 转义；禁止非 ASCII `L""` 字面量。

**界面语言**：启动时 `GetSystemDefaultLangID()`，主语言为中文（0x04）→ 简体字表，否则英文字表。字符串表为两份 C 指针数组（`g_strs_zh[]` / `g_strs_en[]`），源码中均为 UTF-8 窄字面量。菜单栏**不用 .rc 字符串资源**，启动时代码组装 HMENU（`LoadString` 多语言机制的代码页问题与此类风险一并避开）；.rc 中只保留图标等无编码资源。会话内容、配置值（API key、模型名）始终 UTF-8，与界面语言无关。

## 4. 总体架构

单 exe 分发，wolfSSL 静态链接。网络在**工作线程**中以**阻塞 socket** 运行（wolfSSL 默认 IO 回调零改动集成；HTTP/SSE 解析线性；crypto 不在 UI 线程），跨线程通信经 `PostMessage` + 临界区保护的消息队列。

```
LLMChat32/
├── ow_portable_v2_stable/      # 已有工具链（gitignore，不入库）
├── third_party/
│   └── wolfssl/                # v5.9.x-stable 源码 + wmake 构建
├── src/
│   ├── main.c                  # WinMain、消息循环、全局状态
│   ├── app.c/h                 # 应用层：发送队列、流式状态机、上下文截断、撤回
│   ├── core/
│   │   ├── config.c/h          # TOML 子集解析 + 配置结构 + 重写
│   │   ├── session.c/h         # 会话/消息内存模型
│   │   ├── history.c/h         # 会话落盘（每会话一个 JSON 文件）
│   │   ├── json.c/h            # 极简 JSON 解析/生成
│   │   ├── toml.c/h            # TOML 子集解析
│   │   ├── utf.c/h             # UTF-8↔UTF-16、字符遍历
│   │   ├── http.c/h            # HTTP/1.1（Winsock、chunked、SSE 回调）
│   │   ├── tls.c/h             # TLS 抽象 → wolfSSL 实现
│   │   ├── net.c/h             # DNS/连接/代理 CONNECT
│   │   ├── llm.c/h             # 三协议适配
│   │   └── image.c/h           # stb_image 解码
│   ├── ui/
│   │   ├── mainwin.c           # 主窗口、菜单、tab 栏、整体布局
│   │   ├── chatview.c          # 聊天区自绘（气泡/背景/超链接/滚动/右键菜单）
│   │   ├── inputbar.c          # 输入区 + 模型下拉 + 发送/打断
│   │   ├── dlg_config.c        # 模型配置对话框
│   │   ├── dlg_view.c          # 思考过程/图片查看对话框
│   │   └── draw.c/h            # 圆角气泡、白底透明混合、双缓冲 DIB
│   └── res/resource.rc         # 图标等无编码资源
├── build.bat                   # 设置 INCLUDE/LIB/PATH 后调 wmake
├── makefile                    # wmake 主 makefile
└── docs/superpowers/specs/     # 本文档
```

## 5. 传输层

- **http.c**：HTTP/1.1，请求头可定制；响应支持 `Content-Length` 与 `Transfer-Encoding: chunked`；超时：连接 10s / 读写 30s（可配）；错误码映射为可读字符串
- **SSE**：按行解析 `data:` 事件流逐事件回调上层；`[DONE]` 哨兵识别；每模型 `stream=false` 时走一次性 JSON
- **tls.c**：wolfSSL 静态链接，IO 走 ws2_32；SNI 开启；CA 从 exe 旁 `cacert.pem` 加载；`verify_tls` 逐模型可关（默认开）
- **net.c**：DNS（gethostbyname）、TCP 连接、代理
- **代理**：全局 `[proxy]`（host/port/可选 Basic 认证），每模型 `use_proxy` 开关；https 目标走 CONNECT 隧道，http 目标走绝对 URI 形式转发
- **打断**：closesocket → 工作线程退出 → 当前气泡标记「已打断」
- 链接库：ws2_32、comdlg32、shell32、gdi32、user32 与 wolfSSL 静态库（comctl32/advapi32 未用，如后续引入工具提示等再追加）

## 6. 协议适配（llm.c）

统一内部消息：`system/user/assistant` 角色；文本块、思考块、图片引用。会话只存「可见」内容，思考块随消息保存（用于弹窗回看），发送时按协议组装。

| | Anthropic Messages | Chat Completions | Responses |
|---|---|---|---|
| 端点 | `/v1/messages` | `/v1/chat/completions` | `/v1/responses` |
| 认证头 | `x-api-key` + `anthropic-version: 2023-06-01` | `Authorization: Bearer` | 同左 |
| 思考参数 | `thinking:{type:"enabled",budget_tokens}` | `reasoning_effort`（映射 low/medium/high；同时捕获 `reasoning_content` 增量） | `reasoning:{effort}`（同时捕获 `reasoning_summary_text` 增量） |
| 流式事件 | `content_block_delta`（`thinking_delta`/`text_delta`） | `choices[0].delta`（`content`/`reasoning_content`） | `response.output_text.delta`、`response.reasoning_summary_text.delta` |
| 非流式解析 | `content[]` 块 | `choices[0].message` | `output[]` items |

通用参数：`max_tokens`、`temperature`（<0 不传）、`stream`、`max_context_chars`。

**上下文截断**：发送前按 1 token ≈ 3.5 字符的粗估，从最旧的用户/助手轮次开始丢弃，直到满足 `max_context_chars`；system 消息永不动；截断对用户无提示（属于内部行为）。

错误处理：HTTP 非 2xx 时提取响应体中的 error 字段（三家格式不同）转可读状态栏信息。

## 7. 配置（TOML 子集）

解析器支持 `[section]`、`[[models]]` 数组表、字符串/整数/浮点/布尔、`#` 注释。

```toml
[general]
font_name = "SimSun"
font_size = 9
history_dir = "history"
bg_image = "bg.png"          # 空 = 无背景
bg_opacity = 40              # 0-100，相对白色底
window_width = 640
window_height = 480

[proxy]
host = "192.168.1.10"
port = 7890
username = ""
password = ""

[[models]]
name = "Claude"
api = "anthropic"            # anthropic | chat | responses
base_url = "https://api.anthropic.com"
api_key = "sk-ant-..."
model_id = "claude-sonnet-4-5"
use_proxy = false
stream = true
reasoning = true
reasoning_effort = "medium"  # low | medium | high
thinking_budget = 8000       # anthropic 预算
max_tokens = 4096
max_context_chars = 120000
temperature = -1             # <0 不传
verify_tls = true
```

保存语义：程序重写整个文件（未知字段解析时保留、重排；注释丢失）。写入策略：先写 `config.toml.tmp`，成功后 `rename` 覆盖；任何失败（含磁盘满）弹 MessageBox 提示且内存配置不丢。

配置对话框：左侧模型列表（添加/删除/上下移），右侧字段编辑；菜单「选项 → 模型配置」进入。

## 8. 会话与历史

- 每会话一个文件 `history/<id>.json`，自有 JSON 格式（与 API 报文共用 json.c）
- 保存时机：每条消息落定（含流式结束/打断）后全量重写该会话文件；体积按会话规模可接受
- 写失败（含磁盘满）：MessageBox 提示，内存数据保留，下次落定重试
- 启动时扫描 `history/` 载入全部会话，按文件名序恢复标签页
- **撤回**：右键自己消息 → 删除该条及其后所有消息 → 立即落盘
- **图片**：助手消息中的图片存 URL/已下载数据引用；聊天区渲染 `[图片]` 超链接；弹窗经 stb_image 解码显示（PNG/JPEG/BMP/GIF），提供「另存为…」（comdlg32），下载复用 http/tls 栈

## 9. UI（GDI 自绘）

- **主窗口**：标准 HMENU 菜单栏（代码组装，字符串来自字符串表）；客户区自上而下：tab 条、聊天区、输入区、状态栏
- **tab 条**：自绘；每 tab 显示会话标题（首条用户消息截断）+ 右侧 × 关闭钮；最右 + 按钮新建会话；点击切换、中键或 × 关闭（关闭即删历史文件，需确认）
- **聊天区**：双缓冲 offscreen DIB
  - 背景：白色底；`bg_image` 经 stb_image 解码后按 `bg_opacity` 与白色预混合，**锁定绘制在聊天区右下角**，不随滚动、不平铺；缺图为纯白底
  - 气泡：RoundRect + Polygon 尖角；用户消息绿色系右对齐，AI 浅灰左对齐；`DrawTextW` 折行；内存存 UTF-8，绘制时转 UTF-16 缓存
  - 思考过程：气泡顶部「思考过程」超链接（手型光标，点击弹 dlg_view 显示全文）
  - 图片：`[图片]` 超链接，点击弹 dlg_view（解码显示 + 另存）
  - 滚动：自绘垂直滚动条（Win98 无 UxTheme，自绘一致性更好）
  - 流式更新：工作线程 PostMessage(WM_APP) → 重绘当前气泡
- **输入区**：多行 edit（ES_WANTRETURN）+ 模型下拉（来自 [[models]]）+ 发送按钮；发送中按钮变为「打断」
- **字体**：`CreateFontW`，charset 用 `DEFAULT_CHARSET` 保证简中/英文系统都能选到匹配字形
- 状态栏：网络/磁盘/协议错误、发送进度

## 10. 错误处理

- 磁盘满：所有文件写检查返回值；失败提示且内存状态不回滚
- 网络：连接/读/写超时与 WSA 错误统一转为可读字符串进状态栏；断线不误删已收内容
- TLS：CA 加载失败/握手失败/证书校验失败 → 状态栏 + 对话框，提示可检查 cacert.pem 或关闭 verify_tls
- 内存：Win98 内存有限；消息历史动态数组增长；单条消息超阈值（如 1MB）提示

## 11. 构建与测试

- `build.bat`：设置 `WATCOM`/`INCLUDE`/`LIB`/`PATH` → `wmake`
- 编译选项：`-bt=nt -bm -wx`（NT 目标、多线程 CLIB、最高警告）
- core 模块（json/toml/utf/http 解析/llm payload 构造）带 `#ifdef LLMCORE_TEST` 独立测试 main，`wmake test` 目标，用断言覆盖边界
- 开发机（Win11）直接运行产物验证功能；真机 Win98 验证需 VM，由用户侧进行
- HTTP/SSE/协议解析以本地 mock 端点先行测试；真实 API 冒烟需用户提供 key
