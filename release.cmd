@echo off
setlocal
set "VERSION=%~1"
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\make-release.ps1" -Version "%VERSION%"
if errorlevel 1 (
  echo release FAILED
  exit /b 1
)
echo release OK
