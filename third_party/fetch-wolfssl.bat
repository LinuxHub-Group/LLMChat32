@echo off
setlocal
rem ---------------------------------------------------------------------------
rem fetch-wolfssl.bat - download wolfSSL upstream source into third_party\wolfssl\
rem
rem The upstream wolfSSL source is NOT committed to this repo. Only our Open
rem Watcom port files under third_party\wolfssl\ow\ are tracked. This script
rem fetches the matching upstream tag on demand, e.g. from build.bat when the
rem source tree is missing.
rem
rem Method: git clone --depth 1 (a few seconds), then strip .git.
rem Falls back to the codeload zip via curl+tar when git is unavailable
rem (much slower). Requires network; fails with a clear message otherwise.
rem ---------------------------------------------------------------------------
set "WOLFSSL_VERSION=v5.9.4-stable"
set "TP=%~dp0"
set "DEST=%TP%wolfssl"
set "STAGE=%TP%.wolfssl-stage"
set "ZIP=%TP%wolfssl-%WOLFSSL_VERSION%.zip"
set "URL=https://codeload.github.com/wolfSSL/wolfssl/zip/refs/tags/%WOLFSSL_VERSION%"
set "REPO=https://github.com/wolfSSL/wolfssl"

if exist "%DEST%\src\ssl.c" (
    echo [fetch] wolfSSL %WOLFSSL_VERSION% already present, skipping download.
    exit /b 0
)

rem --- method 1: git clone (fast) ------------------------------------------
where git >nul 2>&1
if not errorlevel 1 (
    echo [fetch] cloning wolfSSL %WOLFSSL_VERSION% ...
    if exist "%STAGE%" rmdir /s /q "%STAGE%"
    git clone --depth 1 -b %WOLFSSL_VERSION% %REPO% "%STAGE%" 2>nul
    if not errorlevel 1 (
        rmdir /s /q "%STAGE%\.git" 2>nul
        if not exist "%DEST%" mkdir "%DEST%"
        for /d %%D in ("%STAGE%\*") do move /y "%%D" "%DEST%" >nul
        for %%F in ("%STAGE%\*.*") do move /y "%%F" "%DEST%" >nul
        rmdir /s /q "%STAGE%"
        goto verify
    )
    echo [fetch] git clone failed, falling back to zip download
)

rem --- method 2: codeload zip via curl + tar (slow) ------------------------
where curl >nul 2>&1
if errorlevel 1 (
    echo [fetch] ERROR: neither git nor curl.exe available. Fetch manually:
    echo [fetch]   %URL%
    exit /b 1
)
echo [fetch] downloading wolfSSL %WOLFSSL_VERSION% zip...
curl -fL --retry 2 -o "%ZIP%" "%URL%"
if errorlevel 1 (
    echo [fetch] ERROR: download failed - network offline or blocked?
    echo [fetch] fetch manually and unpack into "%DEST%":
    echo [fetch]   %URL%
    if exist "%ZIP%" del "%ZIP%"
    exit /b 1
)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
tar -xf "%ZIP%" -C "%STAGE%"
if errorlevel 1 (
    echo [fetch] ERROR: failed to extract "%ZIP%"
    rmdir /s /q "%STAGE%"
    del "%ZIP%"
    exit /b 1
)
if not exist "%DEST%" mkdir "%DEST%"
for /d %%D in ("%STAGE%\wolfssl-*\*") do move /y "%%D" "%DEST%" >nul
for %%F in ("%STAGE%\wolfssl-*\*.*") do move /y "%%F" "%DEST%" >nul
rmdir /s /q "%STAGE%"
del "%ZIP%"

:verify
if not exist "%DEST%\src\ssl.c" (
    echo [fetch] ERROR: fetch did not produce "%DEST%\src\ssl.c"
    exit /b 1
)
echo [fetch] OK - wolfSSL %WOLFSSL_VERSION% in "%DEST%"
exit /b 0
