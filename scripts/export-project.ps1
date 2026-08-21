param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug",
    [string]$Project = "examples\first-check.kcd",
    [string]$OutputPath = "out\kachakacha-export.kcd"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ViewerPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_viewer.exe"
$ProjectPath = Resolve-Path $Project
$ResolvedOutputPath = Join-Path $RepoRoot $OutputPath
$OutputDir = Split-Path $ResolvedOutputPath -Parent

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Invoke-Checked $ViewerPath @("--project", $ProjectPath, "--export", $ResolvedOutputPath)
Write-Host $ResolvedOutputPath
