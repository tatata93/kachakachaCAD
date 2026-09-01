param(
    [string]$Preset = "",
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Preset -eq "" -and $env:KACHACAD_PRESET) { $Preset = $env:KACHACAD_PRESET }

if ($Preset -ne "") {
    Push-Location $RepoRoot
    try { Invoke-Checked "ctest" @("--preset", $Preset) } finally { Pop-Location }
    return
}

Invoke-Checked "ctest" @("--test-dir", $BuildPath, "-C", $Config, "--output-on-failure")
