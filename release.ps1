# ==============================================================
# WhoLock 发布版构建脚本（PowerShell 5.1+，无输入参数）
# 流程：解析版本号 -> 构建（优先 cmake，缺失时回退 cl.exe）-> 复制产物到 发布版\<版本>\
# ==============================================================
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Log([string]$Msg)    { Write-Host "[release] $Msg" }
function LogErr([string]$Msg) { Write-Host "[release:ERROR] $Msg" -ForegroundColor Red }

# ---- 1. 版本号（单一事实源：CMakeLists.txt 的 project(... VERSION x.y.z)）----
$verLine = Select-String -Path (Join-Path $Root "CMakeLists.txt") -Pattern 'project\(\w+\s+VERSION\s+(\d+)\.(\d+)\.(\d+)' | Select-Object -First 1
if (-not $verLine) { LogErr "无法从 CMakeLists.txt 解析版本号"; exit 1 }
$VMajor = $verLine.Matches[0].Groups[1].Value
$VMinor = $verLine.Matches[0].Groups[2].Value
$VPatch = $verLine.Matches[0].Groups[3].Value
$Version = "$VMajor.$VMinor.$VPatch"
Log "项目版本号: $Version"

$publishDir = Join-Path $Root ("发布版\" + $Version)
if (-not (Test-Path $publishDir)) {
    New-Item -ItemType Directory -Path $publishDir -Force | Out-Null
    Log "已创建发布目录: $publishDir"
}

$exeOut = Join-Path $Root "build\out\wholock.exe"
Log "开始构建（Release）..."

$cmakeExe = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeExe) {
    # ---- 2a. 标准 CMake 构建（缓存指向旧源码目录时清理重配一次）----
    & $cmakeExe.Source -S . -B build -DCMAKE_CONFIGURATION_TYPES=Release
    if ($LASTEXITCODE -ne 0) {
        Log "CMake configure 失败，清理 build 缓存后重试..."
        Remove-Item (Join-Path $Root "build") -Recurse -Force -ErrorAction SilentlyContinue
        & $cmakeExe.Source -S . -B build -DCMAKE_CONFIGURATION_TYPES=Release
        if ($LASTEXITCODE -ne 0) { LogErr "CMake configure 两次失败，停止"; exit 1 }
    }
    & $cmakeExe.Source --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { LogErr "CMake 构建失败"; exit 1 }
}
else {
    # ---- 2b. 回退：vswhere 定位 VS -> vcvars64 -> rc/cl/link 直接编译 ----
    Log "未找到 cmake.exe，回退 cl.exe 直接编译..."
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { LogErr "找不到 vswhere.exe，无法定位 Visual Studio"; exit 1 }
    $vsRoot = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { LogErr "找不到 vcvars64.bat: $vcvars"; exit 1 }

    # version.h（configure_file 的手工等价）
    $genDir = Join-Path $Root "build\generated"
    New-Item -ItemType Directory -Path $genDir -Force | Out-Null
    $verH = Get-Content (Join-Path $Root "cmake\version.h.in") -Raw
    $verH = $verH.Replace("@PROJECT_VERSION_MAJOR@", $VMajor).Replace("@PROJECT_VERSION_MINOR@", $VMinor).Replace("@PROJECT_VERSION_PATCH@", $VPatch).Replace("@PROJECT_VERSION@", $Version)
    Set-Content -Path (Join-Path $genDir "version.h") -Value $verH -Encoding ASCII
    Log "已生成 build\generated\version.h"

    # 源码清单（与 CMakeLists.txt 的 who_app/who_infra/wholock 目标一致）
    $sources = @(
        "src\ui_main.cpp", "src\ui\Theme.cpp", "src\cli\CliMain.cpp",
        "src\application\ScanCoordinator.cpp",
        "src\domain\Normalize.cpp", "src\domain\Merge.cpp",
        "src\domain\RiskFlags.cpp", "src\domain\CliArgs.cpp",
        "src\infrastructure\win32\RmScanner.cpp", "src\infrastructure\win32\HandleTableScanner.cpp",
        "src\infrastructure\win32\WindowsPathMap.cpp", "src\infrastructure\win32\Privileges.cpp",
        "src\infrastructure\win32\ProcessOps.cpp", "src\infrastructure\win32\ProcessQuery.cpp",
        "src\infrastructure\win32\PrepareTargets.cpp", "src\infrastructure\win32\DeepScanRunner.cpp",
        "src\infrastructure\win32\DeepScanner.cpp", "src\infrastructure\win32\Settings.cpp",
        "src\infrastructure\win32\ShellIntegration.cpp", "src\infrastructure\win32\Engines.cpp"
    )
    $clArgs = "/nologo /c /std:c++latest /EHsc /W4 /permissive- /utf-8 /Zc:__cplusplus /O2 /DNDEBUG /MT /DUNICODE /D_UNICODE /DNOMINMAX /Isrc /Ibuild\generated /Fobuild\manual_objs\ " + ($sources -join " ")
    $linkArgs = "link /nologo /SUBSYSTEM:WINDOWS /OUT:build\out\wholock.exe build\manual_objs\*.obj build\out\app.res user32.lib gdi32.lib comctl32.lib shell32.lib comdlg32.lib advapi32.lib rstrtmgr.lib /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest"

    # 编译批处理：内容纯 ASCII，中文工程路径经 %1/%2 传入避免代码页乱码
    $bat = Join-Path $env:TEMP "wholock_release_build.cmd"
    $batLines = @(
        "@echo off",
        'call "%~2" >nul || exit /b 1',
        'cd /d "%~1" || exit /b 1',
        "if not exist build\out mkdir build\out",
        "if not exist build\manual_objs mkdir build\manual_objs",
        "rc /nologo /fo build\out\app.res src\app.rc || exit /b 1",
        ("cl " + $clArgs + " || exit /b 1"),
        ($linkArgs + " || exit /b 1"),
        "exit /b 0"
    )
    Set-Content -Path $bat -Value $batLines -Encoding ASCII
    & cmd.exe /d /c $bat $Root $vcvars
    if (($LASTEXITCODE -ne 0) -or -not (Test-Path $exeOut)) { LogErr "cl.exe 编译/链接失败，停止"; exit 1 }
}

Log "构建成功"
if (-not (Test-Path $exeOut)) { LogErr "构建产物不存在: $exeOut"; exit 1 }

# ---- 3. 复制发布产物 ----
Copy-Item $exeOut (Join-Path $publishDir "wholock.exe") -Force
$sizeKB = [math]::Round((Get-Item (Join-Path $publishDir "wholock.exe")).Length / 1KB, 1)
Log "复制完成: 发布版\$Version\wholock.exe（$sizeKB KB）"
Log "全部完成"
