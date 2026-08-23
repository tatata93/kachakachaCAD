param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

Repair-PathEnvironment

function Test-Tool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        Write-Host "[missing] $Name"
        return $false
    }

    Write-Host "[ok]      $Name -> $($command.Source)"
    return $true
}

Write-Host "kachakachaCAD development environment check"
Write-Host ""

$ok = $true
$ok = (Test-Tool "git") -and $ok
$ok = (Test-Tool "cmake") -and $ok

$compilerNames = @("cl", "clang++", "g++")
$visibleCompilers = @()
foreach ($compilerName in $compilerNames) {
    $compiler = Get-Command $compilerName -ErrorAction SilentlyContinue
    if ($null -ne $compiler) {
        $visibleCompilers += "$compilerName -> $($compiler.Source)"
    }
}

$vsCppInstallations = @()
$vsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWherePath) {
    $vsCppInstallations = & $vsWherePath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

Write-Host ""
if (Get-Command "cmake" -ErrorAction SilentlyContinue) {
    cmake --version | Select-Object -First 1
}

Write-Host ""
$QtRoot = "C:\Qt\6.9.2\msvc2022_64"
$VcpkgToolchain = Join-Path $env:USERPROFILE "vcpkg\scripts\buildsystems\vcpkg.cmake"
$OcctConfig = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\share\opencascade\OpenCASCADEConfig.cmake"
Write-Host "Desktop CAD dependencies:"
foreach ($dependency in @(
    @{ Name = "Qt 6"; Path = $QtRoot },
    @{ Name = "vcpkg toolchain"; Path = $VcpkgToolchain },
    @{ Name = "Open CASCADE"; Path = $OcctConfig }
)) {
    if (Test-Path $dependency.Path) {
        Write-Host "[ok]      $($dependency.Name) -> $($dependency.Path)"
    } else {
        Write-Host "[missing] $($dependency.Name) -> $($dependency.Path)"
        $ok = $false
    }
}

Write-Host ""
Write-Host "C++ compiler visibility:"
if ($visibleCompilers.Count -eq 0) {
    Write-Host "- No C++ compiler command is visible in this shell."
    Write-Host "- This is OK when using these scripts with Visual Studio Build Tools."
} else {
    foreach ($compiler in $visibleCompilers) {
        Write-Host "- $compiler"
    }
}

Write-Host ""
Write-Host "Visual Studio C++ Build Tools:"
if ($vsCppInstallations.Count -eq 0) {
    Write-Host "- Not found by vswhere."
} else {
    foreach ($installation in $vsCppInstallations) {
        Write-Host "- $installation"
    }
}

if (-not $ok) {
    Write-Host ""
    Write-Host "Install the missing required tools before building."
    exit 1
}

Write-Host ""
Write-Host "Required tools are available."
