# makefile for LLMChat32 - Open Watcom wmake
#
# Targets:
#   all   (default) build build\llmchat32.exe (builds wolfssl.lib first)
#   test            build app + tests, then tests are run by build.bat
#   clean           remove build artifacts

CC       = wcl386
CFLAGS   = -bt=nt -bm -wx

WOLFSSL_DIR  = third_party\wolfssl
WOLFSSL_LIB  = build\wolfssl.lib
# wolfSSL gets its own wmake (third_party\wolfssl\ow\makefile); -DWOLFSSL_USER_SETTINGS
# and the include paths must match that makefile so lib and app agree on
# struct layouts (see wolfSSL AGENTS.md: "same feature defines" rule).
# -wcd=202 silences the vendored headers' "defined but not referenced"
# static-function noise so project TUs that include wolfssl (tls.c and the
# test drivers below) stay warning-clean under -wx; the wolfSSL library
# build itself keeps its own pre-existing warning profile.
WOLFSSL_OPTS = -DWOLFSSL_USER_SETTINGS -i=$(WOLFSSL_DIR) -i=$(WOLFSSL_DIR)\ow -wcd=202

# application objects, listed for later growth
APP_OBJS = build\main.obj build\ui.obj build\mainwin.obj build\tabbar.obj build\chatview.obj build\inputbar.obj build\dlg_view.obj build\dlg_config.obj build\draw.obj build\session.obj build\utf.obj build\config.obj build\toml.obj build\image.obj build\app.obj build\history.obj build\json.obj build\net.obj build\tls.obj build\http.obj build\llm.obj build\diag.obj build\fmt.obj build\msgcopy.obj
APP_RES  = build\app.res

all: build\llmchat32.exe .SYMBOLIC

build\llmchat32.exe: $(APP_OBJS) $(APP_RES) $(WOLFSSL_LIB)
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -l=nt_win -k0x100000 -fe=build\llmchat32.exe $(APP_OBJS) $(APP_RES) $(WOLFSSL_LIB) ws2_32.lib advapi32.lib comdlg32.lib

build\main.obj: src\main.c src\version.h src\core\config.h src\ui\mainwin.h src\ui\strings.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\main.obj src\main.c

build\ui.obj: src\ui\ui.c src\ui\ui.h src\ui\strings.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\ui.obj src\ui\ui.c

build\mainwin.obj: src\ui\mainwin.c src\ui\mainwin.h src\ui\ui.h src\ui\strings.h src\ui\commands.h src\ui\tabbar.h src\ui\chatview.h src\ui\inputbar.h src\ui\dlg_view.h src\ui\dlg_config.h src\core\config.h src\core\session.h src\core\app.h src\core\history.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\mainwin.obj src\ui\mainwin.c

build\inputbar.obj: src\ui\inputbar.c src\ui\inputbar.h src\ui\ui.h src\ui\strings.h src\ui\draw.h src\core\config.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\inputbar.obj src\ui\inputbar.c

build\tabbar.obj: src\ui\tabbar.c src\ui\tabbar.h src\ui\mainwin.h src\ui\ui.h src\ui\strings.h src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\tabbar.obj src\ui\tabbar.c

build\chatview.obj: src\ui\chatview.c src\ui\chatview.h src\ui\draw.h src\ui\ui.h src\ui\strings.h src\ui\mainwin.h src\core\session.h src\core\utf.h src\core\image.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\chatview.obj src\ui\chatview.c

# Task 18 view dialogs (think text / image viewer). dlg_view.c pulls in
# image.h (fetch+decode), strings.h, ui.h; GetSaveFileNameA comes from
# comdlg32 (added to the exe link line above).
build\dlg_view.obj: src\ui\dlg_view.c src\ui\dlg_view.h src\core\image.h src\core\config.h src\core\utf.h src\ui\strings.h src\ui\ui.h src\ui\draw.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\dlg_view.obj src\ui\dlg_view.c

# Task 19 model configuration dialog. dlg_config.c edits the model list
# on a local Config copy and rewrites config.toml on OK; it pulls the
# same ui/draw/utf headers as dlg_view plus core/config.h.
build\dlg_config.obj: src\ui\dlg_config.c src\ui\dlg_config.h src\core\config.h src\core\utf.h src\ui\strings.h src\ui\ui.h src\ui\draw.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\dlg_config.obj src\ui\dlg_config.c

build\draw.obj: src\ui\draw.c src\ui\draw.h src\ui\ui.h src\core\config.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\draw.obj src\ui\draw.c

build\session.obj: src\core\session.c src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\session.obj src\core\session.c

build\utf.obj: src\core\utf.c src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\utf.obj src\core\utf.c

build\config.obj: src\core\config.c src\core\config.h src\core\toml.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\config.obj src\core\config.c

build\toml.obj: src\core\toml.c src\core\toml.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\toml.obj src\core\toml.c

# image decoding: stb_image implementation TU (see image.c header for
# the STBI_NO_* configuration, which is what keeps it -wx clean - no
# per-file warning suppressions needed, unlike third_party\wolfssl).
# Task 18 adds image_fetch() on top of the net/tls/http stack, so the
# rule lists those headers too (the app already links them).
build\image.obj: src\core\image.c src\core\image.h src\core\net.h src\core\tls.h src\core\http.h src\core\config.h third_party\stb_image.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\image.obj src\core\image.c

# --- core orchestration stack, wired into the exe by Task 17 -----------
build\app.obj: src\core\app.c src\core\app.h src\core\config.h src\core\llm.h src\core\session.h src\core\history.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\app.obj src\core\app.c

build\diag.obj: src\core\diag.c src\core\diag.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\diag.obj src\core\diag.c

build\fmt.obj: src\core\fmt.c src\core\fmt.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\fmt.obj src\core\fmt.c

build\msgcopy.obj: src\core\msgcopy.c src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\msgcopy.obj src\core\msgcopy.c

build\history.obj: src\core\history.c src\core\history.h src\core\session.h src\core\json.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\history.obj src\core\history.c

build\json.obj: src\core\json.c src\core\json.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\json.obj src\core\json.c

build\net.obj: src\core\net.c src\core\net.h src\core\config.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\net.obj src\core\net.c

# tls.c includes wolfssl headers: same feature defines/include paths as
# the wolfSSL build (the "same feature defines" rule, see WOLFSSL_OPTS).
build\tls.obj: src\core\tls.c src\core\tls.h src\core\net.h src\core\certs_roots.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) $(WOLFSSL_OPTS) -c -fo=build\tls.obj src\core\tls.c

build\http.obj: src\core\http.c src\core\http.h src\core\net.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\http.obj src\core\http.c

build\llm.obj: src\core\llm.c src\core\llm.h src\core\json.h src\core\http.h src\core\net.h src\core\tls.h src\core\config.h src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\llm.obj src\core\llm.c

# icon-only resource; the menu is built from LS() strings in code
build\app.res: src\res\app.rc src\res\app.ico
	@if not exist build mkdir build
	wrc -q -r -i=src\res -fo=build\app.res src\res\app.rc

# wolfSSL static library - delegates to its own makefile. Also rebuilds
# when our port files change (user_settings.h, ow makefile).
$(WOLFSSL_LIB): $(WOLFSSL_DIR)\src\ssl.c $(WOLFSSL_DIR)\ow\user_settings.h $(WOLFSSL_DIR)\ow\makefile
	cd $(WOLFSSL_DIR)\ow && wmake

# --- tests ---------------------------------------------------------------
# Console test apps (sources in src\tests, executables in tests\ for
# build.bat's `for %%E in (tests\*.exe)` runner.
TEST_EXES = tests\test_tls_smoke.exe tests\test_utf.exe tests\test_json.exe tests\test_config.exe tests\test_history.exe tests\test_net.exe tests\test_http.exe tests\test_tls.exe tests\test_llm_req.exe tests\test_llm_stream.exe tests\test_app.exe tests\test_image.exe

test: all $(TEST_EXES) .SYMBOLIC

tests\test_tls_smoke.exe: src\tests\test_tls_smoke.c src\tests\certs_google.h src\core\certs_roots.h $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_tls_smoke.exe src\tests\test_tls_smoke.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

tests\test_utf.exe: src\tests\test_utf.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_utf.exe src\tests\test_utf.c src\core\utf.c

tests\test_json.exe: src\tests\test_json.c src\core\json.h src\core\json.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_json.exe src\tests\test_json.c src\core\json.c src\core\utf.c

tests\test_config.exe: src\tests\test_config.c src\core\toml.h src\core\toml.c src\core\config.h src\core\config.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_config.exe src\tests\test_config.c src\core\toml.c src\core\config.c

tests\test_history.exe: src\tests\test_history.c src\core\session.h src\core\session.c src\core\history.h src\core\history.c src\core\json.h src\core\json.c src\core\utf.h src\core\utf.c src\core\msgcopy.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_history.exe src\tests\test_history.c src\core\session.c src\core\history.c src\core\json.c src\core\utf.c src\core\msgcopy.c

# net 连接层: plain sockets + ws2_32 (no wolfssl needed at this layer).
tests\test_net.exe: src\tests\test_net.c src\core\net.h src\core\net.c src\core\config.h src\core\fmt.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_net.exe src\tests\test_net.c src\core\net.c ws2_32.lib src\core\fmt.c

# http 层: 请求/响应/chunked 解码，架在 net.c 之上 (no wolfssl needed).
tests\test_http.exe: src\tests\test_http.c src\core\http.h src\core\http.c src\core\net.h src\core\net.c src\core\config.h src\core\fmt.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 -fe=tests\test_http.exe src\tests\test_http.c src\core\http.c src\core\net.c ws2_32.lib src\core\fmt.c

# tls 层: wolfSSL 封装于 net.c 之上。tls.c 与 wolfssl.lib 必须用相同的
# feature defines 编译（见 WOLFSSL_OPTS 注释），并链接 ws2_32/advapi32
# （与 test_tls_smoke 一致）。
tests\test_tls.exe: src\tests\test_tls.c src\core\tls.h src\core\tls.c src\core\net.h src\core\net.c src\core\certs_roots.h src\core\config.h src\core\fmt.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_tls.exe src\tests\test_tls.c src\core\tls.c src\core\net.c src\core\fmt.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# llm 请求构造层: 三协议 JSON 组装 + 上下文截断 (纯函数, 无网络).
# llm.c 现引用 net/tls/http 符号 (llm_send 全管线), 故链接全栈 + wolfssl
# (与 test_llm_stream 一致); 源文件自身不触网。
tests\test_llm_req.exe: src\tests\test_llm_req.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c src\core\diag.h src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_llm_req.exe src\tests\test_llm_req.c src\core\llm.c src\core\json.c src\core\session.c src\core\utf.c src\core\net.c src\core\tls.c src\core\http.c src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# llm 解析/发送层: SSE 行解析 + 事件分发 + 非流式解析 + 错误提取 + llm_send
# 全管线。解析层经 llm_feed_sse/llm_parse_nonstream/llm_extract_error 直接
# 喂字节测试 (不经网络); 全链路链接 net/tls/http/json/config/session/utf +
# wolfssl + ws2_32/advapi32 (与 test_tls_smoke 一致)。
tests\test_llm_stream.exe: src\tests\test_llm_stream.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\config.c src\core\toml.h src\core\toml.c src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c src\core\diag.h src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_llm_stream.exe src\tests\test_llm_stream.c src\core\llm.c src\core\net.c src\core\tls.c src\core\http.c src\core\json.c src\core\config.c src\core\toml.c src\core\session.c src\core\utf.c src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# 应用编排层: 工作线程跑 llm_send (经 app_set_transport_hook 可被测试替换),
# 锁队列 + app_pump 移交碎片/终态事件。链接全栈 + wolfssl (与
# test_llm_stream 一致)。
tests\test_app.exe: src\tests\test_app.c src\core\app.h src\core\app.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\config.c src\core\toml.h src\core\toml.c src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c src\core\history.h src\core\history.c src\core\diag.h src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_app.exe src\tests\test_app.c src\core\app.c src\core\llm.c src\core\net.c src\core\tls.c src\core\http.c src\core\json.c src\core\config.c src\core\toml.c src\core\session.c src\core\utf.c src\core\history.c src\core\diag.c src\core\fmt.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# image 解码层: stb_image 封装 (PNG/JPEG/BMP/GIF/TGA/PSD -> RGBA8)。
# 混合断言在 draw.c 的 draw_blend_white（纯整数，无 GDI），但 draw.obj
# 里的 ui_font/ui_make_font/draw_bubble 引用 g_cfg、g_win9x、ui_acp_a 与
# GDI，故链接 config/toml/utf/ui + gdi32 (与 app 一致). Task 18 起 image.c
# 还引用 net/tls/http 符号 (image_fetch)，故测试链接全栈 + wolfssl +
# ws2_32/advapi32。
tests\test_image.exe: src\tests\test_image.c src\core\image.h src\core\image.c src\ui\draw.h src\ui\draw.c src\ui\ui.h src\ui\ui.c src\core\diag.c src\core\config.h src\core\config.c src\core\toml.h src\core\toml.c src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c src\core\fmt.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -k0x100000 $(WOLFSSL_OPTS) -fe=tests\test_image.exe src\tests\test_image.c src\core\image.c src\ui\draw.c src\ui\ui.c src\core\diag.c src\core\config.c src\core\toml.c src\core\utf.c src\core\net.c src\core\tls.c src\core\http.c src\core\fmt.c $(WOLFSSL_LIB) gdi32.lib ws2_32.lib advapi32.lib

clean: .SYMBOLIC
	@if exist build\bomscan.exe del build\bomscan.exe
	@if exist build\bomscan.obj del build\bomscan.obj
	@if exist build\llmchat32.exe del build\llmchat32.exe
	@if exist build\main.obj del build\main.obj
	@if exist build\ui.obj del build\ui.obj
	@if exist build\mainwin.obj del build\mainwin.obj
	@if exist build\tabbar.obj del build\tabbar.obj
	@if exist build\chatview.obj del build\chatview.obj
	@if exist build\draw.obj del build\draw.obj
	@if exist build\session.obj del build\session.obj
	@if exist build\utf.obj del build\utf.obj
	@if exist build\config.obj del build\config.obj
	@if exist build\toml.obj del build\toml.obj
	@if exist build\image.obj del build\image.obj
	@if exist build\app.obj del build\app.obj
	@if exist build\history.obj del build\history.obj
	@if exist build\json.obj del build\json.obj
	@if exist build\net.obj del build\net.obj
	@if exist build\tls.obj del build\tls.obj
	@if exist build\http.obj del build\http.obj
	@if exist build\llm.obj del build\llm.obj
	@if exist build\inputbar.obj del build\inputbar.obj
	@if exist build\dlg_view.obj del build\dlg_view.obj
	@if exist build\dlg_config.obj del build\dlg_config.obj
	@if exist build\app.res del build\app.res
	@if exist build\wolfssl.lib del build\wolfssl.lib
	@if exist tests\test_tls_smoke.exe del tests\test_tls_smoke.exe
	@if exist tests\test_utf.exe del tests\test_utf.exe
	@if exist tests\test_json.exe del tests\test_json.exe
	@if exist tests\test_config.exe del tests\test_config.exe
	@if exist tests\test_history.exe del tests\test_history.exe
	@if exist tests\test_net.exe del tests\test_net.exe
	@if exist tests\test_http.exe del tests\test_http.exe
	@if exist tests\test_tls.exe del tests\test_tls.exe
	@if exist tests\test_llm_req.exe del tests\test_llm_req.exe
	@if exist tests\test_llm_stream.exe del tests\test_llm_stream.exe
	@if exist tests\test_app.exe del tests\test_app.exe
	@if exist tests\test_image.exe del tests\test_image.exe
	@if exist $(WOLFSSL_DIR)\ow\makefile (pushd $(WOLFSSL_DIR)\ow && wmake clean && popd)
