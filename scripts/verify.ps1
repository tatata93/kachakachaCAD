param(
    [ValidateSet("plan", "preflight", "focused", "full", "publish")][string]$Mode = "focused",
    [string]$Preset = "windows-msvc",
    [int]$Parallel = 2,
    [string]$Base = "",
    [string]$Evidence = ""
)
$ErrorActionPreference = "Stop"
$arguments = @((Join-Path $PSScriptRoot "verify.py"), $Mode, "--preset", $Preset, "--parallel", $Parallel)
if ($Base) { $arguments += @("--base", $Base) }
if ($Evidence) { $arguments += @("--evidence", $Evidence) }
& python @arguments
exit $LASTEXITCODE
