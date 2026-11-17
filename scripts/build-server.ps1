# Configures and builds the Windows server + tools.
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build-server.ps1
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build-$($Config.ToLower())"

cmake -S $root -B $build -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host "Built: $build"
Write-Host "Server : $build\desktop\$Config\remote_mouse_server.exe (Visual Studio)"
Write-Host "         $build\desktop\remote_mouse_server.exe (Ninja/MinGW)"
