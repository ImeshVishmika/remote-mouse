# Cross-compiles the Windows server and bundles it as an Android asset.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package-android-server.ps1 [-Zig C:\path\to\zig.exe]
param(
    [string]$Zig = 'zig'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root 'android\app\src\main\assets'
$out = Join-Path $outDir 'remote_mouse_server.exe'
$buildDir = Join-Path $root 'build-zig\android-server'
$built = Join-Path $buildDir 'remote_mouse_server.exe'

# Zig can also emit a PDB and import library next to -o. Keep those byproducts
# out of Android assets so the APK contains only the downloadable executable.
New-Item -ItemType Directory -Force $outDir, $buildDir | Out-Null

# PowerShell does not expand native-command wildcards, so pass full source paths.
$sources = Get-ChildItem -Path (Join-Path $root 'desktop\src') -Filter '*.cpp' -File |
    Sort-Object Name |
    ForEach-Object { $_.FullName }
if ($sources.Count -eq 0) {
    throw 'No Windows server sources were found.'
}

$arguments = @(
    'c++',
    '-target', 'x86_64-windows-gnu',
    '-std=c++17',
    '-O2',
    '-Wall', '-Wextra', '-Wpedantic',
    '-I', (Join-Path $root 'shared'),
    '-I', (Join-Path $root 'desktop\src')
)
$arguments += $sources
$arguments += @(
    '-lws2_32',
    '-liphlpapi',
    '-luser32',
    '-lcomctl32',
    '-o', $built
)

& $Zig @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Zig failed to build the bundled Windows server (exit $LASTEXITCODE)."
}

Copy-Item $built $out -Force
$asset = Get-Item $out
if ($asset.Length -eq 0) {
    throw 'The bundled Windows server is empty.'
}
Write-Host "Bundled Windows server: $out ($($asset.Length) bytes)"
