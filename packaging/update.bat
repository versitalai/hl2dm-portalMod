@echo off
rem Update HL2DM Portal to the latest release on GitHub. No admin needed.
rem The desktop shortcuts run "update.bat quiet" before starting the game: one
rem small request, nothing shown when it's up to date or there's no internet.
rem   update.bat         check now and update if a newer release is out
rem   update.bat quiet   the same, silent unless it updates
rem A file named no_auto_update.txt next to this turns the quiet check off.
rem HL2DM_GITHUB_TOKEN (optional, CI) authenticates the release lookup.

rem The installer replaces this file while it runs, and cmd reads a batch file
rem from disk line by line: run a copy from %TEMP% instead (one line, so the
rem original is never read again).
if not defined HL2DM_UPDATER_MODDIR ( set "HL2DM_UPDATER_MODDIR=%~dp0" & copy /y "%~f0" "%TEMP%\hl2dm_portal_update.bat" >nul & call "%TEMP%\hl2dm_portal_update.bat" %* & set "HL2DM_UPDATER_MODDIR=" & exit /b )

setlocal
set "MODDIR=%HL2DM_UPDATER_MODDIR:~0,-1%"
set "QUIET="
if /i "%~1"=="quiet" set "QUIET=1"
if defined QUIET if exist "%MODDIR%\no_auto_update.txt" exit /b 0

set "HAVE=dev"
if exist "%MODDIR%\version.txt" set /p HAVE=<"%MODDIR%\version.txt"
rem A build that isn't a release (dev, a commit) is someone's test copy: leave it.
if defined QUIET if /i not "%HAVE:~0,1%"=="v" exit /b 0

set "WORK=%TEMP%\hl2dm_portal_update"
set "REPO=versitalai/hl2dm-portalmod"
rem Exit code: 0 up to date, 1 downloaded to %WORK%\new (tag in %WORK%\tag.txt), 2 no answer.
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $h = @{ 'User-Agent' = 'hl2dm-portal-updater' }; $api = $h.Clone(); if ($env:HL2DM_GITHUB_TOKEN) { $api['Authorization'] = 'Bearer ' + $env:HL2DM_GITHUB_TOKEN }; try { $r = Invoke-RestMethod -Uri ('https://api.github.com/repos/' + $env:REPO + '/releases/latest') -Headers $api -TimeoutSec 5 } catch { exit 2 }; if ($r.tag_name -eq $env:HAVE) { exit 0 }; $a = $r.assets | Where-Object { $_.name -like 'hl2dm_portal-*.zip' } | Select-Object -First 1; if (-not $a) { exit 2 }; Write-Host ('Updating HL2DM Portal ' + $env:HAVE + ' -> ' + $r.tag_name + ' (' + [math]::Round($a.size / 1MB) + ' MB)...'); try { if (Test-Path $env:WORK) { Remove-Item -Recurse -Force $env:WORK }; New-Item -ItemType Directory -Path $env:WORK | Out-Null; $zip = Join-Path $env:WORK 'update.zip'; Invoke-WebRequest -Uri $a.browser_download_url -OutFile $zip -Headers $h -UseBasicParsing; Expand-Archive -Path $zip -DestinationPath (Join-Path $env:WORK 'new') -Force; Set-Content -Path (Join-Path $env:WORK 'tag.txt') -Value $r.tag_name -NoNewline } catch { Write-Host ('Download failed: ' + $_.Exception.Message); exit 2 }; exit 1"
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" (
    if not defined QUIET echo HL2DM Portal %HAVE% is the latest.
    exit /b 0
)
if not "%RESULT%"=="1" (
    if not defined QUIET echo Couldn't reach GitHub to check for updates.
    exit /b 0
)

rem Install over this copy with the new release's own installer.
for %%I in ("%MODDIR%\..") do set "SOURCEMODS=%%~fI"
if not exist "%WORK%\new\install.bat" (
    echo The download has no install.bat; skipping the update.
    exit /b 0
)
set "CALLER_NO_PAUSE=%HL2DM_NO_PAUSE%"
set "HL2DM_SOURCEMODS=%SOURCEMODS%"
set "HL2DM_NO_PAUSE=1"
call "%WORK%\new\install.bat" "%SOURCEMODS%"
if errorlevel 1 (
    echo The update didn't install. Close the game and run this again:
    echo   %MODDIR%\update.bat
    if not "%CALLER_NO_PAUSE%"=="1" pause
    exit /b 1
)
rem Older releases have no version.txt: record what was installed.
copy /y "%WORK%\tag.txt" "%MODDIR%\version.txt" >nul
set /p NOW=<"%MODDIR%\version.txt"
echo HL2DM Portal updated to %NOW%.
rd /s /q "%WORK%" 2>nul
exit /b 0
