param(
    [string]$BuildDir = "build-product-debug",
    [string]$Config = "Debug",
    [string]$Project = ""
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$QtBin = "C:\Qt\6.9.2\msvc2022_64\bin"
$OcctBin = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"
$OcctDebugBin = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\debug\bin"

if (-not (Test-Path $CadPath)) {
    throw "Qt application was not found: $CadPath"
}
if (-not (Test-Path $QtBin)) {
    throw "Qt runtime was not found: $QtBin"
}
if (-not (Test-Path $OcctBin)) {
    throw "Open CASCADE runtime was not found: $OcctBin"
}

$Arguments = @()
if ($Project -ne "") {
    $ProjectPath = Resolve-Path $Project
    $Arguments += @("--project", $ProjectPath)
}

$PreviousPath = $env:Path
try {
    $env:Path = "$QtBin;$OcctDebugBin;$OcctBin;$PreviousPath"
    Start-Process -FilePath $CadPath -ArgumentList $Arguments
} finally {
    $env:Path = $PreviousPath
}
