@echo off
setlocal
cd /d %~dp0
if not exist .env (
  echo Please copy .env.example to .env and edit your settings first.
  pause
  exit /b 1
)
authserver.exe
