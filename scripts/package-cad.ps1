param(
    [string]$BuildDir = "build-product-release",
    [string]$Config = "Release",
    [string]$OutputDir = "out\kachakachaCAD"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config
& (Join-Path $PSScriptRoot "test.ps1") -BuildDir $BuildDir -Config $Config
& (Join-Path $PSScriptRoot "build-manual-assets.ps1") -BuildDir $BuildDir -Config $Config -SkipBuild

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$DeployTool = "C:\Qt\6.9.2\msvc2022_64\bin\windeployqt.exe"
$QtPrefix = Split-Path (Split-Path $DeployTool -Parent) -Parent
$OffscreenPlugin = Join-Path $QtPrefix "plugins\platforms\qoffscreen.dll"
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out"))
$ResolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))
$OutputPrefix = $OutputRoot.TrimEnd('\') + '\'

if (-not $ResolvedOutput.StartsWith($OutputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output must be a child directory of $OutputRoot"
}

if (-not (Test-Path $CadPath)) {
    throw "Qt application was not found: $CadPath"
}
if (-not (Test-Path $DeployTool)) {
    throw "Qt deployment tool was not found: $DeployTool"
}
if (-not (Test-Path $OffscreenPlugin)) {
    throw "Qt offscreen test plugin was not found: $OffscreenPlugin"
}
if (Test-Path $ResolvedOutput) {
    Remove-Item -LiteralPath $ResolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedOutput | Out-Null

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

$VcpkgRuntime = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"
if (-not (Test-Path $VcpkgRuntime)) {
    throw "Open CASCADE runtime directory was not found: $VcpkgRuntime"
}
$RuntimeCollector = Join-Path $PSScriptRoot "collect-runtime-dependencies.cmake"
Invoke-Checked "cmake" @(
    "-DKACHACAD_EXECUTABLE=$DeployedExecutable",
    "-DKACHACAD_OUTPUT_DIRECTORY=$ResolvedOutput",
    "-DKACHACAD_VCPKG_RUNTIME=$VcpkgRuntime",
    "-DKACHACAD_QT_RUNTIME=$(Join-Path $QtPrefix 'bin')",
    "-P", $RuntimeCollector
)

$FirstCheckProject = Join-Path $RepoRoot "examples\first-check.kcd"
$AcceptanceProject = Join-Path $ResolvedOutput "review-model.kcd"
Copy-Item -LiteralPath $FirstCheckProject -Destination (Join-Path $ResolvedOutput "first-check.kcd") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "examples\curved-panel-light-holes.kcd") -Destination (Join-Path $ResolvedOutput "curved-panel-light-holes.kcd") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "examples\railway-nose-acceptance.kcd") -Destination $AcceptanceProject -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "START_REVIEW.cmd") -Destination (Join-Path $ResolvedOutput "START_REVIEW.cmd") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "OPEN_MANUAL.cmd") -Destination (Join-Path $ResolvedOutput "OPEN_MANUAL.cmd") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual.html") -Destination (Join-Path $ResolvedOutput "manual.html") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual-assets") -Destination (Join-Path $ResolvedOutput "manual-assets") -Recurse -Force

$PreviousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:QT_QPA_PLATFORM = "offscreen"
    Invoke-Checked $DeployedExecutable @("--self-test", "--project", $FirstCheckProject)
    $ReviewStl = Join-Path $ResolvedOutput "review-forming-jig.stl"
    $ReviewStep = Join-Path $ResolvedOutput "review-forming-jig.step"
    $ReviewSnapshot = Join-Path $ResolvedOutput "review-screenshot.png"
    Invoke-Checked $DeployedExecutable @(
        "--project", $AcceptanceProject,
        "--export-first-body-stl", $ReviewStl,
        "--export-first-body-step", $ReviewStep,
        "--snapshot", $ReviewSnapshot
    )
    foreach ($artifact in @($ReviewStl, $ReviewStep, $ReviewSnapshot)) {
        if (-not (Test-Path $artifact) -or (Get-Item -LiteralPath $artifact).Length -lt 1024) {
            throw "Packaged acceptance artifact was not created correctly: $artifact"
        }
    }
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
