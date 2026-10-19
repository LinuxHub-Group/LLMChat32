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
WOLFSSL_OPTS = -DWOLFSSL_USER_SETTINGS -i=$(WOLFSSL_DIR) -i=$(WOLFSSL_DIR)\ow

# application objects, listed for later growth
APP_OBJS = build\main.obj build\ui.obj build\mainwin.obj build\tabbar.obj build\session.obj build\utf.obj build\config.obj build\toml.obj
APP_RES  = build\app.res

all: build\llmchat32.exe .SYMBOLIC

build\llmchat32.exe: $(APP_OBJS) $(APP_RES) $(WOLFSSL_LIB)
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -l=nt_win -fe=build\llmchat32.exe $(APP_OBJS) $(APP_RES) $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

build\main.obj: src\main.c src\version.h src\core\config.h src\ui\mainwin.h src\ui\strings.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\main.obj src\main.c

build\ui.obj: src\ui\ui.c src\ui\ui.h src\ui\strings.h src\core\utf.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\ui.obj src\ui\ui.c

build\mainwin.obj: src\ui\mainwin.c src\ui\mainwin.h src\ui\ui.h src\ui\strings.h src\ui\commands.h src\ui\tabbar.h src\core\config.h src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\mainwin.obj src\ui\mainwin.c

build\tabbar.obj: src\ui\tabbar.c src\ui\tabbar.h src\ui\mainwin.h src\ui\ui.h src\ui\strings.h src\core\session.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\tabbar.obj src\ui\tabbar.c

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
TEST_EXES = tests\test_tls_smoke.exe tests\test_utf.exe tests\test_json.exe tests\test_config.exe tests\test_history.exe tests\test_net.exe tests\test_http.exe tests\test_tls.exe tests\test_llm_req.exe tests\test_llm_stream.exe tests\test_app.exe

test: all $(TEST_EXES) .SYMBOLIC

tests\test_tls_smoke.exe: src\tests\test_tls_smoke.c src\tests\certs_google.h src\core\certs_roots.h $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_tls_smoke.exe src\tests\test_tls_smoke.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

tests\test_utf.exe: src\tests\test_utf.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_utf.exe src\tests\test_utf.c src\core\utf.c

tests\test_json.exe: src\tests\test_json.c src\core\json.h src\core\json.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_json.exe src\tests\test_json.c src\core\json.c src\core\utf.c

tests\test_config.exe: src\tests\test_config.c src\core\toml.h src\core\toml.c src\core\config.h src\core\config.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_config.exe src\tests\test_config.c src\core\toml.c src\core\config.c

tests\test_history.exe: src\tests\test_history.c src\core\session.h src\core\session.c src\core\history.h src\core\history.c src\core\json.h src\core\json.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_history.exe src\tests\test_history.c src\core\session.c src\core\history.c src\core\json.c src\core\utf.c

# net 连接层: plain sockets + ws2_32 (no wolfssl needed at this layer).
tests\test_net.exe: src\tests\test_net.c src\core\net.h src\core\net.c src\core\config.h
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_net.exe src\tests\test_net.c src\core\net.c ws2_32.lib

# http 层: 请求/响应/chunked 解码，架在 net.c 之上 (no wolfssl needed).
tests\test_http.exe: src\tests\test_http.c src\core\http.h src\core\http.c src\core\net.h src\core\net.c src\core\config.h
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_http.exe src\tests\test_http.c src\core\http.c src\core\net.c ws2_32.lib

# tls 层: wolfSSL 封装于 net.c 之上。tls.c 与 wolfssl.lib 必须用相同的
# feature defines 编译（见 WOLFSSL_OPTS 注释），并链接 ws2_32/advapi32
# （与 test_tls_smoke 一致）。
tests\test_tls.exe: src\tests\test_tls.c src\core\tls.h src\core\tls.c src\core\net.h src\core\net.c src\core\certs_roots.h src\core\config.h $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_tls.exe src\tests\test_tls.c src\core\tls.c src\core\net.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# llm 请求构造层: 三协议 JSON 组装 + 上下文截断 (纯函数, 无网络).
# llm.c 现引用 net/tls/http 符号 (llm_send 全管线), 故链接全栈 + wolfssl
# (与 test_llm_stream 一致); 源文件自身不触网。
tests\test_llm_req.exe: src\tests\test_llm_req.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_llm_req.exe src\tests\test_llm_req.c src\core\llm.c src\core\json.c src\core\session.c src\core\utf.c src\core\net.c src\core\tls.c src\core\http.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# llm 解析/发送层: SSE 行解析 + 事件分发 + 非流式解析 + 错误提取 + llm_send
# 全管线。解析层经 llm_feed_sse/llm_parse_nonstream/llm_extract_error 直接
# 喂字节测试 (不经网络); 全链路链接 net/tls/http/json/config/session/utf +
# wolfssl + ws2_32/advapi32 (与 test_tls_smoke 一致)。
tests\test_llm_stream.exe: src\tests\test_llm_stream.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\config.c src\core\toml.h src\core\toml.c src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_llm_stream.exe src\tests\test_llm_stream.c src\core\llm.c src\core\net.c src\core\tls.c src\core\http.c src\core\json.c src\core\config.c src\core\toml.c src\core\session.c src\core\utf.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

# 应用编排层: 工作线程跑 llm_send (经 app_set_transport_hook 可被测试替换),
# 锁队列 + app_pump 移交碎片/终态事件。链接全栈 + wolfssl (与
# test_llm_stream 一致)。
tests\test_app.exe: src\tests\test_app.c src\core\app.h src\core\app.c src\core\llm.h src\core\llm.c src\core\json.h src\core\json.c src\core\session.h src\core\session.c src\core\config.h src\core\config.c src\core\toml.h src\core\toml.c src\core\utf.h src\core\utf.c src\core\net.h src\core\net.c src\core\tls.h src\core\tls.c src\core\http.h src\core\http.c src\core\history.h src\core\history.c $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_app.exe src\tests\test_app.c src\core\app.c src\core\llm.c src\core\net.c src\core\tls.c src\core\http.c src\core\json.c src\core\config.c src\core\toml.c src\core\session.c src\core\utf.c src\core\history.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

clean: .SYMBOLIC
	@if exist build\bomscan.exe del build\bomscan.exe
	@if exist build\bomscan.obj del build\bomscan.obj
	@if exist build\llmchat32.exe del build\llmchat32.exe
	@if exist build\main.obj del build\main.obj
	@if exist build\ui.obj del build\ui.obj
	@if exist build\mainwin.obj del build\mainwin.obj
	@if exist build\tabbar.obj del build\tabbar.obj
	@if exist build\session.obj del build\session.obj
	@if exist build\utf.obj del build\utf.obj
	@if exist build\config.obj del build\config.obj
	@if exist build\toml.obj del build\toml.obj
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
	@if exist $(WOLFSSL_DIR)\ow\makefile (pushd $(WOLFSSL_DIR)\ow && wmake clean && popd)
