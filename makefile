# makefile for LLMChat32 - Open Watcom wmake
#
# Targets:
#   all   (default) build build\llmchat32.exe
#   test            build app, then build/run tests (none yet)
#   clean           remove build artifacts

CC       = wcl386
CFLAGS   = -bt=nt -bm -wx
# application objects, listed for later growth
APP_OBJS = build\main.obj

all: build\llmchat32.exe .SYMBOLIC

build\llmchat32.exe: $(APP_OBJS)
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -l=nt_win -fe=build\llmchat32.exe $(APP_OBJS)

build\main.obj: src\main.c src\version.h
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -c -fo=build\main.obj src\main.c

test: all .SYMBOLIC
	@echo No tests yet.

clean: .SYMBOLIC
	@if exist build\bomscan.exe del build\bomscan.exe
	@if exist build\bomscan.obj del build\bomscan.obj
	@if exist build\llmchat32.exe del build\llmchat32.exe
	@if exist build\main.obj del build\main.obj
