# Package V2 with runtime libraries, editable samples, manual and license notices.
# Require complete self-tests and reopened sample screenshots before creating the ZIP.
# Keep this entry script ASCII: Windows PowerShell 5.1 reads BOM-less files using ACP.

param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Release",
    [string]$OutputDir = "out\kachakachaCAD-v2",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildRoot = $BuildDir
} else {
    $BuildRoot = Join-Path $RepoRoot $BuildDir
}
$ExeName = "kachakacha_cad_next.exe"
$BuiltExe = Join-Path $BuildRoot "$Config\$ExeName"
$SampleWriter = Join-Path $BuildRoot "$Config\kachakacha_v2_write_sample.exe"
$DeployTool = "C:\Qt\6.9.2\msvc2022_64\bin\windeployqt.exe"
$QtPrefix = Split-Path (Split-Path $DeployTool -Parent) -Parent

if (-not $SkipBuild) {
    cmake --build $BuildRoot --config $Config --target kachakacha_cad_next kachakacha_v2_write_sample
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not (Test-Path -LiteralPath $BuiltExe)) {
    # Ninja uses a single configuration directory; MSVC uses Release/Debug.
    $BuiltExe = Join-Path $BuildRoot $ExeName
    $SampleWriter = Join-Path $BuildRoot "kachakacha_v2_write_sample.exe"
}

foreach ($needed in @($BuiltExe, $SampleWriter, $DeployTool)) {
    if (-not (Test-Path $needed)) {
        Write-Host "Cannot package: missing dependency ($needed)"
        exit 77
    }
}

$Output = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out"))
if (-not $Output.StartsWith($OutputRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output must be inside out/: $Output"
}
if (Test-Path $Output) { Remove-Item -LiteralPath $Output -Recurse -Force }
New-Item -ItemType Directory -Path $Output | Out-Null

$Deployed = Join-Path $Output $ExeName
Copy-Item -LiteralPath $BuiltExe -Destination $Deployed -Force

& $DeployTool --release --no-translations --compiler-runtime --no-opengl-sw `
    --no-system-d3d-compiler --no-system-dxc-compiler `
    --dir $Output $Deployed
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$Offscreen = Join-Path $QtPrefix "plugins\platforms\qoffscreen.dll"
if (Test-Path $Offscreen) {
    $Platforms = Join-Path $Output "platforms"
    if (-not (Test-Path $Platforms)) { New-Item -ItemType Directory -Path $Platforms | Out-Null }
    Copy-Item -LiteralPath $Offscreen -Destination (Join-Path $Platforms "qoffscreen.dll") -Force
}

$VcpkgRuntime = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"
if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRuntime "TKernel.dll"))) {
    # CI builds OCCT directly and places its runtime directory on PATH.
    $VcpkgRuntime = $env:PATH.Split(';') | Where-Object {
        $_ -and (Test-Path -LiteralPath (Join-Path $_ "TKernel.dll"))
    } | Select-Object -First 1
}
if ($VcpkgRuntime -and (Test-Path -LiteralPath $VcpkgRuntime)) {
    $Collector = Join-Path $PSScriptRoot "collect-runtime-dependencies.cmake"
    cmake "-DKACHACAD_EXECUTABLE=$Deployed" "-DKACHACAD_OUTPUT_DIRECTORY=$Output" `
        "-DKACHACAD_VCPKG_RUNTIME=$VcpkgRuntime" `
        "-DKACHACAD_QT_RUNTIME=$(Join-Path $QtPrefix 'bin')" -P $Collector
    if ($LASTEXITCODE -ne 0) { throw "Runtime dependency collection failed" }
}

$Samples = Join-Path $Output "samples"
New-Item -ItemType Directory -Path $Samples | Out-Null
& $SampleWriter (Join-Path $Samples "v2-sample.kcd2")
if ($LASTEXITCODE -ne 0) { throw "Sample generation failed" }
& $SampleWriter --railway-nose (Join-Path $Samples "streamlined-railway-nose-1-87.kcd2")
if ($LASTEXITCODE -ne 0) { throw "Railway sample generation failed" }

Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual") -Destination (Join-Path $Output "manual") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual.html") -Destination (Join-Path $Output "manual.html") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "scripts\OPEN_MANUAL.cmd") -Destination (Join-Path $Output "OPEN_MANUAL.cmd") -Force

$Legal = Join-Path $RepoRoot "legal"
if (-not (Test-Path $Legal)) { throw "License notices are missing: $Legal" }
Copy-Item -LiteralPath $Legal -Destination $Output -Recurse -Force
foreach ($name in @("LICENSE", "COPYRIGHT")) {
    $source = Join-Path $RepoRoot $name
    if (Test-Path $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $Output $name) -Force }
}

$Previous = $env:QT_QPA_PLATFORM
Push-Location -LiteralPath $RepoRoot
try {
    $env:QT_QPA_PLATFORM = "offscreen"

    & $Deployed --version
    if ($LASTEXITCODE -ne 0) { throw "Version check failed" }

    & $Deployed --self-test
    if ($LASTEXITCODE -ne 0) { throw "Self-tests failed" }

    $Opened = Join-Path $Output "opened-sample.png"
    & $Deployed --open (Join-Path $Samples "v2-sample.kcd2") --snapshot $Opened
    if ($LASTEXITCODE -ne 0) { throw "Sample open failed" }
    if (-not (Test-Path $Opened) -or (Get-Item -LiteralPath $Opened).Length -lt 1024) {
        throw "Opened sample image is missing: $Opened"
    }
    Remove-Item -LiteralPath $Opened -Force

    $NoseOpened = Join-Path $Output "opened-railway-nose.png"
    & $Deployed --open (Join-Path $Samples "streamlined-railway-nose-1-87.kcd2") --snapshot $NoseOpened
    if ($LASTEXITCODE -ne 0) { throw "Railway sample open failed" }
    if (-not (Test-Path $NoseOpened) -or (Get-Item -LiteralPath $NoseOpened).Length -lt 1024) {
        throw "Railway sample image is missing: $NoseOpened"
    }
    Remove-Item -LiteralPath $NoseOpened -Force
}
finally {
    Pop-Location
    if ($null -eq $Previous) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $Previous }
}

$Zip = "$Output.zip"
if (Test-Path $Zip) { Remove-Item -LiteralPath $Zip -Force }
Compress-Archive -Path (Join-Path $Output "*") -DestinationPath $Zip
Write-Host "Package: $Zip"
