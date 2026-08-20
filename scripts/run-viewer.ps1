param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ViewerPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_viewer.exe"

if (-not (Test-Path $ViewerPath)) {
    throw "Viewer executable was not found: $ViewerPath"
}

Start-Process -FilePath $ViewerPath

