param(
    [string]$BuildDir = "build",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

$CMakeArgs = @("-S", $RepoRoot, "-B", $BuildPath)
if ($Generator -ne "") {
    $CMakeArgs += @("-G", $Generator)
}

Invoke-Checked "cmake" $CMakeArgs
