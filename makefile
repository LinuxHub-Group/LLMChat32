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
APP_OBJS = build\main.obj

all: build\llmchat32.exe .SYMBOLIC

build\llmchat32.exe: $(APP_OBJS) $(WOLFSSL_LIB)
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -l=nt_win -fe=build\llmchat32.exe $(APP_OBJS) $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

build\main.obj: src\main.c src\version.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\main.obj src\main.c

# wolfSSL static library - delegates to its own makefile. Also rebuilds
# when our port files change (user_settings.h, ow makefile).
$(WOLFSSL_LIB): $(WOLFSSL_DIR)\src\ssl.c $(WOLFSSL_DIR)\ow\user_settings.h $(WOLFSSL_DIR)\ow\makefile
	cd $(WOLFSSL_DIR)\ow && wmake

# --- tests ---------------------------------------------------------------
# Console test apps (sources in src\tests, executables in tests\ for
# build.bat's `for %%E in (tests\*.exe)` runner.
TEST_EXES = tests\test_tls_smoke.exe tests\test_utf.exe tests\test_json.exe

test: all $(TEST_EXES) .SYMBOLIC

tests\test_tls_smoke.exe: src\tests\test_tls_smoke.c src\tests\certs_google.h $(WOLFSSL_LIB)
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc $(WOLFSSL_OPTS) -fe=tests\test_tls_smoke.exe src\tests\test_tls_smoke.c $(WOLFSSL_LIB) ws2_32.lib advapi32.lib

tests\test_utf.exe: src\tests\test_utf.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_utf.exe src\tests\test_utf.c src\core\utf.c

tests\test_json.exe: src\tests\test_json.c src\core\json.h src\core\json.c src\core\utf.h src\core\utf.c
	@if not exist tests mkdir tests
	$(CC) $(CFLAGS) -bc -fe=tests\test_json.exe src\tests\test_json.c src\core\json.c src\core\utf.c

clean: .SYMBOLIC
	@if exist build\bomscan.exe del build\bomscan.exe
	@if exist build\bomscan.obj del build\bomscan.obj
	@if exist build\llmchat32.exe del build\llmchat32.exe
	@if exist build\main.obj del build\main.obj
	@if exist build\wolfssl.lib del build\wolfssl.lib
	@if exist tests\test_tls_smoke.exe del tests\test_tls_smoke.exe
	@if exist tests\test_utf.exe del tests\test_utf.exe
	@if exist tests\test_json.exe del tests\test_json.exe
	@if exist $(WOLFSSL_DIR)\ow\makefile (pushd $(WOLFSSL_DIR)\ow && wmake clean && popd)
