@echo off
rem Add HL2DM Portal to an existing HL2DM dedicated server (Windows, Steam app 232370).
rem   server\install_server.bat "C:\path\to\server" [port]
rem Run from the unzipped release. The normal HL2DM server keeps working: this adds
rem the hl2dm_portal folder and writes start_portal_server.bat, which runs a second
rem server (default port 27016) next to it. Run it again to update.
setlocal
set "MOD=hl2dm_portal"
set "HERE=%~dp0"
set "SRV=%~1"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=27016"

if "%SRV%"=="" goto :usage
if not exist "%SRV%\hl2mp\" goto :notserver
if not exist "%HERE%..\%MOD%\bin\x64\server.dll" goto :nopkg

rem The mod is 64-bit only.
set "EXE="
if exist "%SRV%\srcds_win64.exe" set "EXE=srcds_win64.exe"
if not defined EXE goto :no64

echo Installing the mod to %SRV%\%MOD%
rem Keep the server's own config across updates. Exit codes 0-7 mean success.
robocopy "%HERE%..\%MOD%" "%SRV%\%MOD%" /E /R:10 /W:2 /XF server.cfg mapcycle.txt motd.txt /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 goto :copyfail

rem No Steam client on a server to resolve |appid_243750|; it finds the HL2 and
rem HL2DM content in its own folder instead.
copy /Y "%HERE%gameinfo_server.txt" "%SRV%\%MOD%\gameinfo.txt" >nul
rem Some dedicated builds load the game DLL as server_srv.dll.
copy /Y "%SRV%\%MOD%\bin\x64\server.dll" "%SRV%\%MOD%\bin\x64\server_srv.dll" >nul

> "%SRV%\start_portal_server.bat" echo @echo off
>> "%SRV%\start_portal_server.bat" echo rem HL2DM Portal server. Extra arguments go to the server, e.g. +hostname "Portals"
>> "%SRV%\start_portal_server.bat" echo cd /d "%%~dp0"
>> "%SRV%\start_portal_server.bat" echo start "HL2DM Portal server" %EXE% -console -game %MOD% -port %PORT% +maxplayers 16 +map dm_lockdown %%*

echo.
echo Done. Start it with start_portal_server.bat in the server folder.
echo Players connect to this server's address on port %PORT% (allow UDP %PORT% in the firewall).
echo Server settings go in hl2dm_portal\cfg\server.cfg.
goto :end

:usage
echo usage: install_server.bat "C:\path\to\hl2dm-server" [port]
goto :end
:notserver
echo error: that folder doesn't look like an HL2DM dedicated server (no hl2mp folder).
goto :end
:nopkg
echo error: run this from the unzipped release (hl2dm_portal\bin\x64\server.dll not found next to the server folder).
goto :end
:no64
echo error: no srcds_win64.exe in the server folder. The mod is 64-bit only. Update the server:
echo   steamcmd +force_install_dir "C:\path\to\server" +login anonymous +app_update 232370 validate +quit
goto :end
:copyfail
echo error: some files couldn't be copied. Stop the portal server if it's running and try again.
goto :end
:end
if not "%HL2DM_NO_PAUSE%"=="1" pause
