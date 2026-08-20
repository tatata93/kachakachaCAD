param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Generator -eq "") {
    $VsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWherePath) {
        $Vs2022Path = & $VsWherePath -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $null -ne $Vs2022Path -and $Vs2022Path.Length -gt 0) {
            $Generator = "Visual Studio 17 2022"
        }
    }
}

$CMakeArgs = @("-S", $RepoRoot, "-B", $BuildPath)
if ($Generator -ne "") {
    $CMakeArgs += @("-G", $Generator)
    if ($Generator.StartsWith("Visual Studio")) {
        $CMakeArgs += @("-A", "x64")
    }
}

Invoke-Checked "cmake" $CMakeArgs
