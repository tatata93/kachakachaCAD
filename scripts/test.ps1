param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

Invoke-Checked "ctest" @("--test-dir", $BuildPath, "-C", $Config, "--output-on-failure")
