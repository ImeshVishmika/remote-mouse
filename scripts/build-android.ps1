# Builds the Android APK with the Gradle wrapper.
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 [-Variant Debug|Release]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Variant = 'Debug',
    [string]$Zig = 'zig'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$android = Join-Path $root 'android'

if (-not (Test-Path (Join-Path $root 'third_party\SDL2\CMakeLists.txt'))) {
    throw "SDL2 is missing. Run scripts\fetch-sdl2.ps1 first."
}

# The phone's offline PC SETUP page serves this generated APK asset.
& (Join-Path $PSScriptRoot 'package-android-server.ps1') -Zig $Zig

Push-Location $android
try {
    & .\gradlew.bat "assemble$Variant"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Write-Host ''
Write-Host "APK: $android\app\build\outputs\apk\$($Variant.ToLower())\app-$($Variant.ToLower()).apk"
Write-Host 'Install with: adb install -r <apk>'
