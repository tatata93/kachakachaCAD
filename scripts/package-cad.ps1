param(
    [string]$BuildDir = "build-qt-release",
    [string]$Config = "Release",
    [string]$OutputDir = "out\kachakachaCAD"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$DeployTool = "C:\Qt\6.9.2\msvc2022_64\bin\windeployqt.exe"
$QtPrefix = Split-Path (Split-Path $DeployTool -Parent) -Parent
$OffscreenPlugin = Join-Path $QtPrefix "plugins\platforms\qoffscreen.dll"
$ResolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))

if (-not (Test-Path $CadPath)) {
    throw "Qt application was not found: $CadPath"
}
if (-not (Test-Path $DeployTool)) {
    throw "Qt deployment tool was not found: $DeployTool"
}
if (-not (Test-Path $OffscreenPlugin)) {
    throw "Qt offscreen test plugin was not found: $OffscreenPlugin"
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

$PlatformOutput = Join-Path $ResolvedOutput "platforms"
if (-not (Test-Path $PlatformOutput)) {
    New-Item -ItemType Directory -Path $PlatformOutput | Out-Null
}
Copy-Item -LiteralPath $OffscreenPlugin -Destination (Join-Path $PlatformOutput "qoffscreen.dll") -Force

$FirstCheckProject = Join-Path $RepoRoot "examples\first-check.kcd"
Copy-Item -LiteralPath $FirstCheckProject -Destination (Join-Path $ResolvedOutput "作例.kcd") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "examples\curved-panel-light-holes.kcd") -Destination (Join-Path $ResolvedOutput "曲面とライト穴の作例.kcd") -Force

$PreviousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:QT_QPA_PLATFORM = "offscreen"
    Invoke-Checked $DeployedExecutable @("--self-test", "--project", $FirstCheckProject)
}
finally {
    if ($null -eq $PreviousPlatform) {
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    }
    else {
        $env:QT_QPA_PLATFORM = $PreviousPlatform
    }
}

Write-Host "Application: $DeployedExecutable"
