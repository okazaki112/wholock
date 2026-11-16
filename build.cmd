@echo off
REM WhoLock build: probe VS -> vcvars -> configure -> Release build -> ctest. (ASCII only: cmd parses UTF-8 as GBK)
setlocal enableextensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [build] vswhere not found, install VS or Build Tools & exit /b 1 )
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT ( echo [build] no VS with VC tools & exit /b 1 )
echo [build] VS=%VSROOT%
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

where cmake >nul 2>&1 || set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

cmake -S . -B build -DCMAKE_CONFIGURATION_TYPES=Release || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
ctest --test-dir build -C Release --output-on-failure || exit /b 1
echo [build] OK. see build\out\wholock.exe
