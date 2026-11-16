# check-deps.ps1 - R8-a dependency direction machine check. ASCII-only: Windows PowerShell 5.1 decodes UTF-8-no-BOM .ps1 as ANSI (known parser corruption).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$fails = @()

$hits1 = Get-ChildItem src/domain, src/application -Recurse -Include *.h, *.cpp |
    Select-String -Pattern '#include\s*[<"](windows\.h|winsock2\.h|ws2tcpip\.h|fstream|RestartManager\.h|winternl\.h)'
if ($hits1) { $hits1 | ForEach-Object { $fails += "DOMAIN-LEAK: $($_.Path):$($_.LineNumber) $($_.Line.Trim())" } }

$hits2 = Get-ChildItem src/infrastructure -Recurse -Include *.h, *.cpp |
    Select-String -Pattern '#include\s*"(.*ui|.*application).*"|\.\./ui|\.\./application'
if ($hits2) { $hits2 | ForEach-Object { $fails += "DIRECTION: $($_.Path):$($_.LineNumber) $($_.Line.Trim())" } }

if ($fails) { $fails | ForEach-Object { Write-Output ("FAIL " + $_) }; exit 1 }
Write-Output "PASS dependency single-direction: no Win32/IO header in domain+application; infra never includes ui/app"
