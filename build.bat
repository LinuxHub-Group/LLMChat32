@echo off
setlocal
cd /d "%~dp0"

rem ---------------------------------------------------------------------------
rem LLMChat32 build script (Open Watcom 2.0, Windows 98 compatible target)
rem
rem   build.bat        build build\llmchat32.exe
rem   build.bat test   build, then run tests (none yet)
rem
rem Environment: WATCOM defaults to ow_portable_v2_stable next to this script.
rem ---------------------------------------------------------------------------

if not defined WATCOM set "WATCOM=%~dp0ow_portable_v2_stable"
if not exist "%WATCOM%\binnt\wcl386.exe" (
    echo [build] ERROR: wcl386.exe not found under "%WATCOM%"
    echo [build] set WATCOM to a valid Open Watcom install and retry.
    exit /b 1
)

set "INCLUDE=%WATCOM%\h;%WATCOM%\h\nt"
set "LIB=%WATCOM%\lib386;%WATCOM%\lib386\nt"
set "PATH=%WATCOM%\binnt;%PATH%"

if not exist build mkdir build

rem --- UTF-8 BOM scan (batch findstr cannot match BOM bytes, so compile ---
rem --- the tiny C scanner src\tools\bomscan.c and run it over src\)      ---
wcl386 -bt=nt -bm -bc -wx -fo=build\bomscan.obj -fe=build\bomscan.exe src\tools\bomscan.c
if errorlevel 1 (
    echo [build] ERROR: failed to build bomscan tool.
    exit /b 1
)
build\bomscan.exe src
if errorlevel 1 (
    echo [build] ERROR: UTF-8 BOM found under src\ - save files without BOM.
    exit /b 1
)

if /i "%~1"=="test" goto run_tests
if not "%~1"=="" (
    echo usage: build.bat [test]
    exit /b 2
)

wmake
if errorlevel 1 exit /b 1
echo [build] OK - build\llmchat32.exe
exit /b 0

:run_tests
wmake test
if errorlevel 1 exit /b 1

set TEST_FAILED=0
set TEST_TOTAL=0
for %%E in (tests\*.exe) do (
    set /a TEST_TOTAL+=1
    echo [test] running %%~nxE
    "%%E"
    if errorlevel 1 (
        echo [test] FAIL: %%~nxE
        set TEST_FAILED=1
    ) else (
        echo [test] pass: %%~nxE
    )
)
if %TEST_FAILED%==1 exit /b 1
if %TEST_TOTAL%==0 (
    echo [test] no tests yet.
) else (
    echo [test] all %TEST_TOTAL% test executables passed.
)
exit /b 0
