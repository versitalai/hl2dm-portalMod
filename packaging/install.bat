@echo off
rem Install HL2DM Portal into Steam's sourcemods folder (Windows).
rem Double-click from the unzipped release, or: install.bat "C:\Path\To\Steam"
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
echo Installing to %DEST%
xcopy "%HERE%%MOD%" "%DEST%\" /E /I /Y /Q >nul || goto :fail

if not exist "%STEAM_DIR%\steamapps\appmanifest_243750.acf" (
    echo.
    echo NOTE: "Source SDK Base 2013 Multiplayer" doesn't look installed in this library.
    echo       Install it from Steam: steam://install/243750
)

echo.
echo Done. Restart Steam, then launch "HL2DM Portal" from your Library.
pause
exit /b 0

:fail
pause
exit /b 1
