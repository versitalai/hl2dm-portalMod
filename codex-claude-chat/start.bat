@echo off
setlocal
title Codex-Claude Chat
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo.
  echo  Node.js is not installed. Opening https://nodejs.org - install the LTS version, then run this again.
  start "" https://nodejs.org
  pause
  exit /b 1
)
node doctor.js --first-run
node server.js --open %*
echo.
echo  Chat server stopped.
pause
