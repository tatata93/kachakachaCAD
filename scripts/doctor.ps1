param()

$ErrorActionPreference = "Stop"

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

Write-Host ""
if (Get-Command "cmake" -ErrorAction SilentlyContinue) {
    cmake --version | Select-Object -First 1
}

Write-Host ""
Write-Host "Optional later dependencies:"
Write-Host "- Qt 6"
Write-Host "- Open CASCADE Technology"
Write-Host "- Eigen"

Write-Host ""
Write-Host "C++ compiler visibility:"
if ($visibleCompilers.Count -eq 0) {
    Write-Host "- No C++ compiler command is visible in this shell."
    Write-Host "- CMake may still find Visual Studio C++ tools if the C++ workload is installed."
} else {
    foreach ($compiler in $visibleCompilers) {
        Write-Host "- $compiler"
    }
}

if (-not $ok) {
    Write-Host ""
    Write-Host "Install the missing required tools before building."
    exit 1
}

Write-Host ""
Write-Host "Required tools are available."
