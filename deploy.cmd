@echo off
setlocal
if not exist "D:\QtWorkspace\AutoRun_Codex\build-dsh\AutoRun_Codex.exe" (
  call "D:\QtWorkspace\AutoRun_Codex\build.cmd"
  if errorlevel 1 exit /b 1
)
call "D:\msvc\2019\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=D:\Qt\6.6.2\msvc2019_64\bin;%PATH%"
cd /d D:\QtWorkspace\AutoRun_Codex
if exist dist rmdir /s /q dist
mkdir dist
copy /y build-dsh\AutoRun_Codex.exe dist\ >nul
windeployqt.exe --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist\AutoRun_Codex.exe
if errorlevel 1 exit /b 1
echo.
echo package done: D:\QtWorkspace\AutoRun_Codex\dist\AutoRun_Codex.exe
