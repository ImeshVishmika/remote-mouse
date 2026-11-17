# Fetches SDL2 (pinned release) into third_party/SDL2.
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\fetch-sdl2.ps1
param(
    [string]$Version = 'release-2.30.9'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $root 'third_party\SDL2'

if (Test-Path (Join-Path $destination 'CMakeLists.txt')) {
    Write-Host "SDL2 is already present at $destination"
    exit 0
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null

$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    Write-Host "Cloning SDL2 $Version ..."
    & git clone --depth 1 --branch $Version https://github.com/libsdl-org/SDL.git $destination
    if ($LASTEXITCODE -eq 0) {
        Write-Host "SDL2 $Version cloned into $destination"
        exit 0
    }
    Write-Warning 'git clone failed; falling back to the release tarball.'
}

$archiveUrl = "https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/$Version"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ('sdl2-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

try {
    $archive = Join-Path $temp 'sdl2.tar.gz'
    Write-Host "Downloading $archiveUrl ..."
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archive

    # tar is bundled with Windows 10 1803+. The archive also contains an old
    # symlinked project that tar may skip on Windows; that is harmless.
    & tar -xzf $archive -C $temp
    $extracted = Get-ChildItem $temp -Directory |
        Where-Object { $_.Name -like 'SDL-*' } |
        Select-Object -First 1
    if (-not $extracted) {
        throw 'The SDL2 archive did not contain an SDL-* directory.'
    }

    Move-Item $extracted.FullName $destination
    Write-Host "SDL2 $Version extracted into $destination"
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
