param(
    [string]$Preset = "",
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "doctor.ps1")
& (Join-Path $PSScriptRoot "configure.ps1") -Preset $Preset -BuildDir $BuildDir -Generator $Generator
& (Join-Path $PSScriptRoot "build.ps1") -Preset $Preset -BuildDir $BuildDir -Config $Config
& (Join-Path $PSScriptRoot "test.ps1") -Preset $Preset -BuildDir $BuildDir -Config $Config
