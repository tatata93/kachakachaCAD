param(
    [string]$BuildDir = "build-product-release",
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config
}

$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$ProjectPath = Join-Path $RepoRoot "examples\er1-er2-round-cab-1-87.kcd"
$OutputDirectory = Join-Path $RepoRoot "docs\test-models\er1-er2-round-cab"
$QtBin = "C:\Qt\6.9.2\msvc2022_64\bin"
$QtPlugins = "C:\Qt\6.9.2\msvc2022_64\plugins"
$VcpkgBin = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"

if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$Shots = @(
    @{ Name = "front"; State = "er2-front" },
    @{ Name = "top"; State = "er2-top" },
    @{ Name = "side"; State = "er2-side" },
    @{ Name = "front-45"; State = "er2-front-45" },
    @{ Name = "wireframe"; State = "er2-wireframe" },
    @{ Name = "horizontal-sections"; State = "er2-sections" },
    @{ Name = "center-section"; State = "er2-center-section" },
    @{ Name = "curvature"; State = "er2-curvature" },
    @{ Name = "flat-pattern"; State = "er2-flat-pattern" },
    @{ Name = "assembly-30"; State = "er2-assembly-30" },
    @{ Name = "assembly-100"; State = "er2-assembly-100" }
)

$PreviousPath = $env:Path
$PreviousPlatform = $env:QT_QPA_PLATFORM
$PreviousPluginPath = $env:QT_PLUGIN_PATH
try {
    Repair-PathEnvironment
    $env:Path = "$QtBin;$VcpkgBin;$env:Path"
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:QT_PLUGIN_PATH = $QtPlugins
    foreach ($Shot in $Shots) {
        $OutputPath = Join-Path $OutputDirectory ($Shot.Name + ".png")
        $Process = Start-Process -FilePath $CadPath -ArgumentList @(
            "--project", $ProjectPath,
            "--manual-state", $Shot.State,
            "--snapshot", $OutputPath
        ) -Wait -PassThru -WindowStyle Hidden
        if ($Process.ExitCode -ne 0) {
            throw "ER1 / ER2 test screenshot '$($Shot.Name)' failed."
        }
    }
}
finally {
    $env:Path = $PreviousPath
    $env:QT_QPA_PLATFORM = $PreviousPlatform
    $env:QT_PLUGIN_PATH = $PreviousPluginPath
}

Write-Host "ER1 / ER2 test images: $OutputDirectory"
