param(
    [string]$BuildDir = "build-qt-release",
    [string]$Config = "Release",
    [string]$OutputDir = "out\kachakachaCAD"
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$DeployTool = "C:\Qt\6.9.2\msvc2022_64\bin\windeployqt.exe"
$ResolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))

if (-not (Test-Path $CadPath)) {
    throw "Qt application was not found: $CadPath"
}
if (-not (Test-Path $DeployTool)) {
    throw "Qt deployment tool was not found: $DeployTool"
}
if (-not (Test-Path $ResolvedOutput)) {
    New-Item -ItemType Directory -Path $ResolvedOutput | Out-Null
}

$DeployedExecutable = Join-Path $ResolvedOutput "kachakacha_cad.exe"
Copy-Item -LiteralPath $CadPath -Destination $DeployedExecutable -Force
& $DeployTool --release --no-translations --compiler-runtime --dir $ResolvedOutput $DeployedExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath (Join-Path $RepoRoot "examples\first-check.kcd") -Destination (Join-Path $ResolvedOutput "作例.kcd") -Force
Write-Host "Application: $DeployedExecutable"
