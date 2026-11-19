# SPDX-License-Identifier: GPL-3.0-or-later
#
# Windows / MSVC pre-commit check. It runs what CI runs:
#   1. JSON schema validation
#   2. CMake configure (-DPROTOSIM_WERROR=ON)
#   3. Build
#   4. ctest + ProtoSim --selftest
#
# Windows + Visual Studio 17 2022 + MSVC x64 only. No Linux/WSL support.
# Recommended entry point: tools\check.bat
#
# Usage:
#   tools\check.bat
#   tools\check.bat -Fast
#   tools\check.bat -NoTests
#
# Environment:
#   PROTOSIM_QT      Qt prefix, default D:\programming\Qt\6.6.3\msvc2019_64
#   PROTOSIM_BUILD   build directory, default <repo>\build
#   PROTOSIM_CONFIG  Release (default) / Debug

param(
    [switch]$Fast,
    [switch]$NoTests,
    [string]$Qt = $env:PROTOSIM_QT,
    [string]$BuildDir = $env:PROTOSIM_BUILD,
    [string]$Config = $env:PROTOSIM_CONFIG
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message)
{
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message)
{
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Fail([string]$Message)
{
    Write-Host "[FAIL] $Message" -ForegroundColor Red
    exit 1
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if ([string]::IsNullOrWhiteSpace($Qt)) {
    $Qt = 'D:\programming\Qt\6.6.3\msvc2019_64'
}
if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = 'Release'
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot 'build'
}
elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repoRoot $BuildDir
}

# MSBuild crashes when HTTPS_PROXY / https_proxy are both present.
# Windows env names are case-insensitive, but Git Bash can inject both forms.
$proxyNames = @(
    'http_proxy', 'https_proxy', 'all_proxy', 'ftp_proxy',
    'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'FTP_PROXY'
)
foreach ($name in $proxyNames) {
    Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
}

try {
    Set-Location $repoRoot

    Write-Step 'Validating config schema'
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($null -ne $python) {
        & python tools/validate_config.py --quiet
        if ($LASTEXITCODE -ne 0) {
            Fail 'Config schema validation failed'
        }
        Write-Ok 'Config schema validation passed'
    }
    else {
        Write-Host '[SKIP] python not found'
    }

    if (-not $Fast) {
        Write-Step 'CMake configure (WERROR=ON)'
        & cmake -S . -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
            '-DPROTOSIM_WERROR=ON' "-DCMAKE_PREFIX_PATH=$Qt"
        if ($LASTEXITCODE -ne 0) {
            Fail "CMake configure failed, Qt prefix: $Qt"
        }
        Write-Ok 'Configure done'
    }
    else {
        Write-Step 'Skipping CMake configure (-Fast)'
    }

    Write-Step "Building ($Config)"
    & cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) {
        Fail 'Build failed, check the first error above'
    }
    Write-Ok 'Build passed'

    if ($NoTests) {
        Write-Host ""
        Write-Ok 'All checks passed (tests skipped)'
        exit 0
    }

    $qtBin = Join-Path $Qt 'bin'
    $env:PATH = "$qtBin;$env:PATH"

    Write-Step 'ctest'
    & ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Fail 'ctest has failing cases'
    }
    Write-Ok 'ctest passed'

    Write-Step 'ProtoSim --selftest'
    $exe = Join-Path $BuildDir 'bin\ProtoSim.exe'
    if (-not (Test-Path $exe)) {
        Fail "Executable not found: $exe"
    }
    & $exe --selftest
    if ($LASTEXITCODE -ne 0) {
        Fail 'Self-test failed'
    }
    Write-Ok 'Self-test passed'

    Write-Host ""
    Write-Ok 'Ready to commit.'
}
catch {
    Fail $_.Exception.Message
}
