@echo off
rem Install HL2DM Portal for the current user (Windows). No admin needed.
rem Double-click from the unzipped release, or pass the Steam folder (or the
rem sourcemods folder):  install.bat "D:\Steam"
rem If server.txt (one line: address:port) is next to this file, also makes a
rem "Join HL2DM Portal server" shortcut. HL2DM_NO_PAUSE=1 skips prompts (CI).
setlocal
set "MOD=hl2dm_portal"
set "HERE=%~dp0"
set "STEAM_DIR="
set "SOURCEMODS="

if not exist "%HERE%%MOD%\gameinfo.txt" goto :nopkg

rem Where Steam looks for mods. Shared and no-admin installs put Steam in odd
rem places, so try, in order: the argument, Steam's own settings (per user,
rem readable without admin), the running Steam, the usual folders, Steam
rem shortcuts, a search of all drives, then ask.
if not "%~1"=="" call :fromarg "%~1"
if not defined SOURCEMODS call :fromregistry
if not defined SOURCEMODS call :fromsearch running
if not defined SOURCEMODS call :fromcommon
if not defined SOURCEMODS call :fromsearch shortcuts
if not defined SOURCEMODS call :fromsearch drives
if not defined SOURCEMODS call :frompicker
if not defined SOURCEMODS goto :nosteam
set "SOURCEMODS=%SOURCEMODS:/=\%"
set "DEST=%SOURCEMODS%\%MOD%"

rem Steam folder for the launcher and the SDK Base check, if not known yet.
if not defined STEAM_DIR for %%I in ("%SOURCEMODS%\..\..") do if exist "%%~fI\steamapps\" set "STEAM_DIR=%%~fI"

rem Can we write there? (Steam updates itself, so its folder normally is writable.)
if not exist "%SOURCEMODS%\" mkdir "%SOURCEMODS%" 2>nul
echo test> "%SOURCEMODS%\hl2dm_write_test.tmp" 2>nul
if not exist "%SOURCEMODS%\hl2dm_write_test.tmp" goto :nowrite
del "%SOURCEMODS%\hl2dm_write_test.tmp"

rem A running game keeps bin\x64\client.dll and server.dll open, and copying
rem over them fails with "sharing violation". Ask to close it first.
:checkgame
set "RUNNING="
for %%P in (hl2.exe hl2_win64.exe srcds.exe srcds_win64.exe) do (
    tasklist /FI "IMAGENAME eq %%P" 2>nul | find /I "%%P" >nul && set "RUNNING=%%P"
)
if not defined RUNNING goto :copy
echo The game is running: %RUNNING%
echo Quit it completely, then press a key to continue.
if "%HL2DM_NO_PAUSE%"=="1" goto :fail
pause >nul
goto :checkgame

:copy
echo Installing to %DEST%
rem robocopy retries locked files: 10 tries, 2 s apart. Exit codes 0-7 mean success.
robocopy "%HERE%%MOD%" "%DEST%" /E /R:10 /W:2 /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% LSS 8 goto :copied
echo.
echo Some files couldn't be copied, most likely because they're in use.
echo Close the game and anything else using files in:
echo   %DEST%
echo then press a key to try again.
if "%HL2DM_NO_PAUSE%"=="1" goto :fail
pause >nul
goto :copy
:copied

rem Source SDK Base 2013 Multiplayer can be in any Steam library.
set "SDKBASE="
if not defined STEAM_DIR goto :sdkchecked
if exist "%STEAM_DIR%\steamapps\appmanifest_243750.acf" set "SDKBASE=%STEAM_DIR%"
if exist "%STEAM_DIR%\steamapps\libraryfolders.vdf" for /f tokens^=4^ delims^=^" %%L in ('findstr /c:"\"path\"" "%STEAM_DIR%\steamapps\libraryfolders.vdf"') do call :checklib "%%L"
:sdkchecked
if defined SDKBASE goto :sdkok
echo.
echo NOTE: "Source SDK Base 2013 Multiplayer" doesn't look installed. Install it from
echo       Steam (free): steam://install/243750
:sdkok

rem Desktop launchers: start the mod through Steam right away, no Steam
rem restart needed for it to show up in the Library.
if not defined STEAM_DIR goto :done
if not exist "%STEAM_DIR%\steam.exe" goto :done
set "DESKTOP="
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('Desktop')" 2^>nul`) do set "DESKTOP=%%D"
if not defined DESKTOP set "DESKTOP=%USERPROFILE%\Desktop"
if not exist "%DESKTOP%\" goto :done
call :launcher "%DESKTOP%\HL2DM Portal.bat" ""
set "SERVER="
if exist "%HERE%server.txt" set /p SERVER=<"%HERE%server.txt"
if defined SERVER call :launcher "%DESKTOP%\Join HL2DM Portal server.bat" "+connect %SERVER%"
echo Desktop shortcut: HL2DM Portal
if defined SERVER echo Desktop shortcut: Join HL2DM Portal server (%SERVER%)

:done
echo.
echo Done. Launch it from the desktop shortcut, or restart Steam and launch
echo "HL2DM Portal" from your Library.
if not "%HL2DM_NO_PAUSE%"=="1" pause
exit /b 0

rem ---------------------------------------------------------------------------
:fromarg
if exist "%~1\steamapps\" set "STEAM_DIR=%~1"
if exist "%~1\steamapps\" set "SOURCEMODS=%~1\steamapps\sourcemods"
if /i "%~nx1"=="sourcemods" set "SOURCEMODS=%~1"
if exist "%~1\steam.exe" set "STEAM_DIR=%~1"
if exist "%~1\steam.exe" if not defined SOURCEMODS set "SOURCEMODS=%~1\steamapps\sourcemods"
if not defined SOURCEMODS echo "%~1" isn't a Steam folder (no steamapps or steam.exe) or a sourcemods folder.
goto :eof

:fromregistry
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do set "STEAM_DIR=%%B"
if defined STEAM_DIR set "STEAM_DIR=%STEAM_DIR:/=\%"
if defined STEAM_DIR if not exist "%STEAM_DIR%\steamapps\" set "STEAM_DIR="
rem The folder Steam actually lists mods from (it can differ from SteamPath).
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SourceModInstallPath 2^>nul ^| find "SourceModInstallPath"') do set "SOURCEMODS=%%B"
if defined SOURCEMODS call :checkparent
if not defined SOURCEMODS if defined STEAM_DIR set "SOURCEMODS=%STEAM_DIR%\steamapps\sourcemods"
goto :eof

:checkparent
for %%I in ("%SOURCEMODS%\..") do if not exist "%%~fI\" set "SOURCEMODS="
goto :eof

:fromcommon
for %%D in ("%USERPROFILE%\Steam" "%LOCALAPPDATA%\Steam" "%LOCALAPPDATA%\Programs\Steam" "%APPDATA%\Steam" "%USERPROFILE%\Documents\Steam" "%USERPROFILE%\Desktop\Steam" "C:\Steam" "D:\Steam" "E:\Steam" "F:\Steam" "C:\Games\Steam" "D:\Games\Steam" "%ProgramFiles(x86)%\Steam" "%ProgramFiles%\Steam") do (
    if not defined STEAM_DIR if exist "%%~D\steamapps\" set "STEAM_DIR=%%~D"
)
if defined STEAM_DIR set "SOURCEMODS=%STEAM_DIR%\steamapps\sourcemods"
goto :eof

:fromsearch
rem PowerShell commands (not script files), so execution policy doesn't block
rem them; if PowerShell itself is blocked this just finds nothing.
rem   running:   the folder of the steam.exe that's running now
rem   shortcuts: Steam shortcuts on the desktop and in the Start Menu
rem   drives:    steam.exe up to 4 folders deep on every drive
if "%~1"=="drives" echo Searching the drives for Steam (this can take a minute)...
set "FOUND="
if "%~1"=="running" for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "Get-Process steam -ErrorAction SilentlyContinue | ForEach-Object { $_.Path } | Where-Object { $_ } | Select-Object -First 1" 2^>nul`) do set "FOUND=%%~dpP"
if "%~1"=="shortcuts" for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "$sh = New-Object -ComObject WScript.Shell; $dirs = @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('CommonDesktopDirectory'), [Environment]::GetFolderPath('StartMenu'), [Environment]::GetFolderPath('CommonStartMenu')); Get-ChildItem -Path $dirs -Filter *.lnk -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $sh.CreateShortcut($_.FullName).TargetPath } | Where-Object { $_ -like '*\steam.exe' } | Select-Object -First 1" 2^>nul`) do set "FOUND=%%~dpP"
if "%~1"=="drives" for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "Get-PSDrive -PSProvider FileSystem | ForEach-Object { Get-ChildItem -Path $_.Root -Filter steam.exe -File -Recurse -Depth 4 -ErrorAction SilentlyContinue } | Where-Object { Test-Path (Join-Path $_.DirectoryName 'steamapps') } | ForEach-Object { $_.FullName } | Select-Object -First 1" 2^>nul`) do set "FOUND=%%~dpP"
if not defined FOUND goto :eof
set "FOUND=%FOUND:~0,-1%"
if exist "%FOUND%\steamapps\" call :fromarg "%FOUND%"
goto :eof

:frompicker
if "%HL2DM_NO_PAUSE%"=="1" goto :eof
echo Steam wasn't found automatically. Pick your Steam folder (the one with
echo steam.exe in it) in the window that opens.
set "PICKED="
for /f "usebackq delims=" %%P in (`powershell -NoProfile -STA -Command "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.FolderBrowserDialog; $d.Description = 'Select your Steam folder (it has steam.exe in it)'; if ($d.ShowDialog() -eq 'OK') { $d.SelectedPath }" 2^>nul`) do set "PICKED=%%P"
if not defined PICKED set /p "PICKED=Or type the Steam folder path and press Enter: "
if defined PICKED call :fromarg "%PICKED%"
goto :eof

:checklib
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
if exist "%LIB%\steamapps\appmanifest_243750.acf" set "SDKBASE=%LIB%"
goto :eof

:launcher
> "%~1" echo @echo off
>> "%~1" echo rem Starts HL2DM Portal through Steam. Extra arguments go to the game, e.g. +connect 203.0.113.5:27016
>> "%~1" echo start "" "%STEAM_DIR%\steam.exe" -applaunch 243750 -game "%DEST%" %~2 %%*
goto :eof

rem ---------------------------------------------------------------------------
:nopkg
echo error: %HERE%%MOD% not found. Run this from the unzipped release.
goto :fail
:nosteam
echo error: Steam not found. Run:  install.bat "C:\Path\To\Steam"
goto :fail
:nowrite
echo error: can't write to %SOURCEMODS%
echo Steam lists mods from that folder, so the mod has to go there. Install Steam
echo into a folder you can write to (e.g. %USERPROFILE%\Steam), or copy the
echo hl2dm_portal folder there with an account that can.
goto :fail
:fail
if not "%HL2DM_NO_PAUSE%"=="1" pause
exit /b 1
