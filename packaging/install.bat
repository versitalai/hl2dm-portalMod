@echo off
rem Install HL2DM Portal into Steam's sourcemods folder (Windows).
rem Double-click from the unzipped release, or: install.bat "C:\Path\To\Steam"
rem Set HL2DM_NO_PAUSE=1 to skip the prompts (used by CI).
setlocal
set "MOD=hl2dm_portal"
set "HERE=%~dp0"
set "STEAM_DIR=%~1"

if not exist "%HERE%%MOD%\gameinfo.txt" (
    echo error: %HERE%%MOD% not found. Run this from the unzipped release.
    goto :fail
)

if "%STEAM_DIR%"=="" (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do set "STEAM_DIR=%%B"
)
if "%STEAM_DIR%"=="" (
    echo error: Steam not found. Run: install.bat "C:\Path\To\Steam"
    goto :fail
)
set "STEAM_DIR=%STEAM_DIR:/=\%"
set "DEST=%STEAM_DIR%\steamapps\sourcemods\%MOD%"

rem A running game keeps bin\x64\client.dll and server.dll open, and copying
rem over them fails with "sharing violation". Ask to close it first.
:checkgame
set "RUNNING="
for %%P in (hl2.exe hl2_win64.exe srcds.exe srcds_win64.exe) do (
    tasklist /FI "IMAGENAME eq %%P" 2>nul | find /I "%%P" >nul && set "RUNNING=%%P"
)
if defined RUNNING (
    echo The game is running: %RUNNING%
    echo Quit it completely, then press a key to continue.
    if "%HL2DM_NO_PAUSE%"=="1" goto :fail
    pause >nul
    goto :checkgame
)

echo Installing to %DEST%

:copy
rem robocopy retries locked files: 10 tries, 2 s apart. Exit codes 0-7 mean success.
robocopy "%HERE%%MOD%" "%DEST%" /E /R:10 /W:2 /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 (
    echo.
    echo Some files couldn't be copied, most likely because they're in use.
    echo Close the game and anything else using files in:
    echo   %DEST%
    echo then press a key to try again.
    if "%HL2DM_NO_PAUSE%"=="1" goto :fail
    pause >nul
    goto :copy
)

if not exist "%STEAM_DIR%\steamapps\appmanifest_243750.acf" (
    echo.
    echo NOTE: "Source SDK Base 2013 Multiplayer" doesn't look installed in this library.
    echo       Install it from Steam: steam://install/243750
)

echo.
echo Done. Restart Steam, then launch "HL2DM Portal" from your Library.
if not "%HL2DM_NO_PAUSE%"=="1" pause
exit /b 0

:fail
if not "%HL2DM_NO_PAUSE%"=="1" pause
exit /b 1
