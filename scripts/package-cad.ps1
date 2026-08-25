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
& $DeployTool `
    --release `
    --no-translations `
    --compiler-runtime `
    --no-opengl-sw `
    --no-system-d3d-compiler `
    --no-system-dxc-compiler `
    --skip-plugin-types "generic,iconengines,imageformats,networkinformation,tls" `
    --dir $ResolvedOutput `
    $DeployedExecutable
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
Copy-Item -LiteralPath (Join-Path $RepoRoot "examples\panorama-light-case.kcd") -Destination (Join-Path $ResolvedOutput "panorama-light-case.kcd") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "START_REVIEW.cmd") -Destination (Join-Path $ResolvedOutput "START_REVIEW.cmd") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "OPEN_MANUAL.cmd") -Destination (Join-Path $ResolvedOutput "OPEN_MANUAL.cmd") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual.html") -Destination (Join-Path $ResolvedOutput "manual.html") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\portable-readme-ja.txt") -Destination (Join-Path $ResolvedOutput "最初にお読みください.txt") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual-assets") -Destination (Join-Path $ResolvedOutput "manual-assets") -Recurse -Force

$LegalSource = Join-Path $RepoRoot "legal"
$LegalOutput = Join-Path $ResolvedOutput "legal"
if (-not (Test-Path $LegalSource)) {
    throw "Legal notices directory was not found: $LegalSource"
}
Copy-Item -LiteralPath $LegalSource -Destination $ResolvedOutput -Recurse -Force

$ApplicationLicense = Join-Path $RepoRoot "LICENSE"
if (Test-Path $ApplicationLicense) {
    Copy-Item -LiteralPath $ApplicationLicense -Destination (Join-Path $ResolvedOutput "LICENSE") -Force
}
else {
    Write-Warning "The kachakachaCAD application license has not been selected. This package is not ready for public distribution."
}

$QtSbomSource = Join-Path $QtPrefix "sbom"
$QtSbomOutput = Join-Path $LegalOutput "sbom\qt"
New-Item -ItemType Directory -Path $QtSbomOutput -Force | Out-Null
foreach ($name in @(
    "qtbase-6.9.2.spdx",
    "qtbase-6.9.2.spdx.json",
    "qtbase-6.9.2.source.spdx"
)) {
    $source = Join-Path $QtSbomSource $name
    if (-not (Test-Path $source)) {
        throw "Qt license inventory was not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $QtSbomOutput $name) -Force
}

$VcpkgShare = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\share"
foreach ($packageName in @(
    "opencascade", "freetype", "brotli", "bzip2", "libpng", "zlib",
    "egl-registry", "opengl-registry", "opengl"
)) {
    $source = Join-Path $VcpkgShare $packageName
    $noticeOutput = Join-Path $LegalOutput "third-party\$packageName"
    $sbomOutput = Join-Path $LegalOutput "sbom\vcpkg\$packageName"
    if (-not (Test-Path (Join-Path $source "copyright")) -or
        -not (Test-Path (Join-Path $source "vcpkg.spdx.json"))) {
        throw "vcpkg license material was not found for $packageName"
    }
    New-Item -ItemType Directory -Path $noticeOutput -Force | Out-Null
    New-Item -ItemType Directory -Path $sbomOutput -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source "copyright") -Destination (Join-Path $noticeOutput "copyright") -Force
    Copy-Item -LiteralPath (Join-Path $source "vcpkg.spdx.json") -Destination (Join-Path $sbomOutput "vcpkg.spdx.json") -Force
    $resources = Join-Path $source "vcpkg-spdx-resources.json"
    if (Test-Path $resources) {
        Copy-Item -LiteralPath $resources -Destination (Join-Path $sbomOutput "vcpkg-spdx-resources.json") -Force
    }
}

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
    foreach ($legalArtifact in @(
        (Join-Path $LegalOutput "README-JA.txt"),
        (Join-Path $LegalOutput "THIRD_PARTY_NOTICES.txt"),
        (Join-Path $LegalOutput "SOURCE-CODE-JA.txt"),
        (Join-Path $LegalOutput "license-texts\LGPL-3.0.txt"),
        (Join-Path $LegalOutput "license-texts\GPL-3.0.txt"),
        (Join-Path $LegalOutput "qt-license-texts\MIT.txt"),
        (Join-Path $LegalOutput "third-party\opencascade\copyright"),
        (Join-Path $LegalOutput "sbom\qt\qtbase-6.9.2.spdx.json")
    )) {
        if (-not (Test-Path $legalArtifact) -or (Get-Item -LiteralPath $legalArtifact).Length -lt 256) {
            throw "Packaged legal material was not created correctly: $legalArtifact"
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
