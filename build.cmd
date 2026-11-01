@echo off
setlocal
call "D:\msvc\2019\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo vcvars failed & exit /b 1)
set "PATH=D:\Qt\Tools\CMake_64\bin;%PATH%"
cd /d D:\QtWorkspace\AutoRun_Codex
cmake -S . -B build-dsh -G "NMake Makefiles" -DCMAKE_PREFIX_PATH=D:/Qt/6.6.2/msvc2019_64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 exit /b 1
cd /d build-dsh
nmake 2>&1
exit /b %errorlevel%
