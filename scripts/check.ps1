# check.ps1 - delivery gate (one command: build/test/warnings/dependence/exception-swallow/sentinel). ASCII-only (PS5.1 ANSI pitfall).
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$results = @()

cmd /c "build.cmd > build_check.log 2>&1"
$buildOk = ($LASTEXITCODE -eq 0)
$results += "[1] build+ctest exit=$LASTEXITCODE -> $(if ($buildOk) {'PASS'} else {'FAIL see build_check.log'})"

$warns = Select-String -Path build_check.log -Pattern "warning C"
$results += "[2] compile warnings (W4) = $(@($warns).Count) $(if ((@($warns)).Count -eq 0) {'PASS'} else {'FAIL'})"
if ($warns) { $warns | Select-Object -First 5 | ForEach-Object { Write-Output ("    " + $_.Line.Trim()) } }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -prerelease -property installationPath
$dumpbin = Get-ChildItem "$vsRoot\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
$exePath = "build\out\wholock.exe"
$deps = & $dumpbin.FullName /dependents $exePath | Select-String -Pattern "\.dll" | ForEach-Object { $_.Line.Trim() }
$vc = $deps | Where-Object { $_ -match '^(vcruntime|msvcp|vccorlib)' }
$known = '^(KERNEL32\.dll|USER32\.dll|GDI32\.dll|COMCTL32\.dll|advapi32\.dll|ole32\.dll|shell32\.dll|rstrtmgr\.dll|ntdll\.dll|msvcrt\.dll|ucrtbase\.dll|api-ms-win-.*|IMM32\.dll|COMDLG32\.dll|UXTHEME\.dll|OLEAUT32\.dll|WINOLE\s?)$'
$unknown = $deps | Where-Object { $_ -notmatch $known }
$vcPart = if (-not $vc) { 'no-VC-runtime=PASS' } else { 'VC-RUNTIME=FAIL: ' + ($vc -join ',') }
$unPart = if ($unknown) { " unknown: " + ($unknown -join ',') } else { '' }
$results += "[3] dumpbin [1]$vcPart$unPart deps: $($deps -join ', ')"

# runner quirk: PS5.1 in sandboxed ConPTY exits with 0xc0000409 regardless of script result
# -> evidence-based: parse deps.log head instead of exit code (also a fake-success guard)
$depsRunner = if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh" } else { "powershell" }
& $depsRunner -NoProfile -ExecutionPolicy Bypass -File scripts\check-deps.ps1 *> deps.log
$depsLine = ((Get-Content deps.log -TotalCount 1) -join '')
if (-not $depsLine) { $depsLine = ((Get-Content deps.log -ErrorAction SilentlyContinue | Select-Object -First 1) -join '') }
$depsOk = if ($depsLine -match '^PASS ') { 'PASS' } else { 'FAIL see deps.log' }
$results += "[4] check-deps [$depsRunner] $depsOk :: $depsLine"

$catchHits = Get-ChildItem src -Recurse -Include *.cpp, *.h |
    Select-String -Pattern 'catch\s*\(.*\)\s*\{\s*\}|catch\s*\{\s*\}'
$results += "[5] empty-catch hits = $(@($catchHits).Count) $(if ((@($catchHits)).Count -eq 0) {'PASS'} else {'FAIL'})"

$readme = Test-Path "README.md"; $qa = Test-Path "docs/manual-qa.md"
$results += "[6] README=$(if($readme){'ok'}else{'MISSING'}) manual-qa=$(if($qa){'ok'}else{'MISSING'}) $(if ($readme -and $qa) {'PASS'} else {'FAIL'})"

# [7] v1.0 F9 icon machine check: flat ico on disk + embedded icon in exe
$icoOk = (Test-Path "src\app.ico") -and ((Get-Item "src\app.ico").Length -gt 500)
try {
    Add-Type -AssemblyName System.Drawing -EA Stop
    $iconExe = [System.Drawing.Icon]::ExtractAssociatedIcon((Resolve-Path $exePath).Path)
} catch { $iconExe = $null }
$results += "[7] icon: file=$(if($icoOk){'ok'}else{'MISSING'}) embedded=$(if($iconExe){'ok'}else{'MISSING'}) $(if($icoOk -and $iconExe){'PASS'}else{'FAIL'})"

# [8] v1.0 G8 version consistency: CMakeLists == generated version.h == README
$cmVer = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'WhoLock VERSION ([0-9.]+)').Groups[1].Value
$hdrVer = [regex]::Match((Get-Content build\generated\version.h -Raw), '"([0-9.]+)"').Groups[1].Value
$rmVer = [regex]::Match((Get-Content README.md -Raw), 'v(\d+\.\d+\.\d+)').Groups[1].Value
$allSame = ($cmVer -eq $hdrVer) -and ($cmVer -eq $rmVer) -and ($cmVer -ne '')
$results += "[8] G8 version: cmake=$cmVer header=$hdrVer readme=$rmVer $(if($allSame){'PASS'}else{'FAIL'})"

$results | ForEach-Object { Write-Output $_ }
$bad = $results | Where-Object { $_ -match 'FAIL' }
if ($bad) { Write-Output "== GATE NOT PASSED =="; exit 1 }
Write-Output "== ALL GATES PASSED: $exePath is deliverable =="; exit 0
