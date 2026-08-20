param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug",
    [string]$Project = ""
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ViewerPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_viewer.exe"

if (-not (Test-Path $ViewerPath)) {
    throw "Viewer executable was not found: $ViewerPath"
}

$Arguments = @()
if ($Project -ne "") {
    $ProjectPath = Resolve-Path $Project
    $Arguments += @("--project", $ProjectPath)
}

Start-Process -FilePath $ViewerPath -ArgumentList $Arguments
