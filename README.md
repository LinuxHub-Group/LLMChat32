# LLMChat32

Windows 98 兼容的 LLM 聊天客户端：Win32 + 自绘 GDI 界面，C89 编写，Open Watcom 2.0 构建，wolfSSL 静态链接（TLS 1.3 实测握手通过）。

## 工具链来源（重要）

本项目的构建基于 **Open Watcom 2.0 官方每日构建的可移植版**：

- 下载：`http://openwatcom.org/ftp/source/ow_portable_v2_stable.zip`
- 解压到仓库根目录的 `ow_portable_v2_stable/`，`build.bat` 会自动定位

**注意**：这与 GitHub 上的 `https://github.com/open-watcom/open-watcom-v2` 源码仓库**不是同一来源**。后者是从源码自行构建的开放分支（含 jmalak 等人的非官方改动）；本仓库的开发与验证均针对官方 portable 快照（`lastrev.txt` 基线 Perforce change 39037），两者在个别编译器行为上可能存在差异，混用需自行验证。

## 功能

- 三套 LLM 协议：Anthropic Messages / OpenAI Chat Completions / OpenAI Responses
- SSE 流式接收（可逐模型关闭）；推理等级（`reasoning_effort` / Anthropic `thinking_budget`）与上下文长度限制
- 多会话 tab（新建/关闭/切换），JSON 历史落盘，重启恢复；损坏文件跳过并提示
- TOML 配置：多套模型、每模型独立代理开关、TLS 校验开关等
- 代理：HTTP CONNECT（含 Basic 认证）
- iOS 短信风格气泡；思考过程默认隐藏（链接弹窗查看）；图片显示为 `[图片]`（弹窗查看/另存）
- 右键气泡：复制内容（用户与 AI 消息均可；不含思考内容）/ 撤回（仅自己消息）
- 聊天区背景图（stb_image 解码，右下角锁定，透明度可调）
- 中英双语界面（按系统语言自动选择）
- 磁盘满/网络/TLS 错误的可读提示；发送全链路可选文件诊断日志

## 构建

```bat
build.bat          :: 构建 build\llmchat32.exe（含 BOM 扫描）
build.bat test     :: 构建并运行 12 个 console 测试程序
wmake clean        :: 清理
```

首次构建前需获取依赖源码（需联网，一次性）：

```bat
third_party\fetch-wolfssl.bat    :: wolfSSL v5.9.x-stable
```

`third_party/stb_image.h` 已随仓库提供。

## 运行

在 exe 同目录准备 `config.toml`（字段见 `docs/superpowers/specs/2026-10-19-llmchat32-design.md` §7），至少配置一个 `[[models]]`。`history/` 目录自动创建。

## 诊断

在 exe 旁创建空文件 `llmdiag.txt` 即开启发送全链路日志（写入该文件，含未处理异常地址）；删除则零开销关闭。

## 平台要点（Win98）

- 9x 上仅少数 W API 真实实现（`ExtTextOutW`/`TextOutW`/`GetTextExtentPointW` 等），其余为桩——UI 文本走自研引擎（`TextOutW` 单绘），编辑框按平台分 Unicode(NT)/ANSI(9x) 两种控件，9x 字体创建强制 `GB2312_CHARSET`
- 剪贴板同时提供 `CF_UNICODETEXT` 与 `CF_TEXT`（9x 不从 Unicode 合成 ANSI，记事本等纯 ANSI 程序需要后者）
- 链接栈保留显式 1MB（`-k0x100000`）——OW 默认 PE 栈保留仅 16 字节，9x 不自动增长主线程栈
- 运行时错误/日志格式化使用自研迷你格式化器（`src/core/fmt.c`），不依赖 CLIB printf 内核

## 结构

```
src/core/   utf json toml config session history net http tls llm app image fmt diag msgcopy
src/ui/     mainwin tabbar chatview inputbar dlg_view dlg_config draw ui strings
src/tests/  12 个 console 测试（wmake test）
```

编码规约：源文件一律 UTF-8 无 BOM；UTF-8 字节绝不直传 ANSI API（唯一出口 `ui_text_w` / CP_ACP 包装层）；详见 spec 文档。

## 许可说明

`third_party/wolfssl/` 由 `fetch-wolfssl.bat` 按需获取，适用 wolfSSL 的 GPLv2/商业双许可（见上游）。`third_party/stb_image.h` 为公有领域。本项目其余代码的许可由仓库所有者决定。
