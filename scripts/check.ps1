param(
    [string]$Preset = "",
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Debug",
    [string]$Generator = "",
    [switch]$LegacyPipeline
)

$ErrorActionPreference = "Stop"

# Preserve custom non-preset builds. Preset-based full checks use the tracked evidence runner.
if ($Preset -ne "" -and $BuildDir -eq "build-msvc2022-x64" -and $Generator -eq "" -and -not $LegacyPipeline) {
    $arguments = @((Join-Path $PSScriptRoot "verify.py"), "full", "--preset", $Preset)
    if ($PSBoundParameters.ContainsKey("Config")) { $arguments += @("--config", $Config) }
    & python @arguments
    exit $LASTEXITCODE
}

& (Join-Path $PSScriptRoot "doctor.ps1")
& (Join-Path $PSScriptRoot "configure.ps1") -Preset $Preset -BuildDir $BuildDir -Generator $Generator
& (Join-Path $PSScriptRoot "build.ps1") -Preset $Preset -BuildDir $BuildDir -Config $Config
& (Join-Path $PSScriptRoot "test.ps1") -Preset $Preset -BuildDir $BuildDir -Config $Config
