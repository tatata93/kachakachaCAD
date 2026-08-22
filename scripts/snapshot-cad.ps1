param(
    [string]$BuildDir = "build-qt",
    [string]$Config = "Debug",
    [string]$Project = "examples\first-check.kcd",
    [string]$OutputPath = "out\cad-preview.png",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$ProjectPath = Resolve-Path $Project
$ResolvedOutput = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputPath))
$OutputDirectory = Split-Path -Parent $ResolvedOutput
$QtBin = "C:\Qt\6.9.2\msvc2022_64\bin"

if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$PreviousPath = $env:Path
$PreviousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:Path = "$QtBin;$PreviousPath"
    $env:QT_QPA_PLATFORM = "offscreen"
    $SnapshotArguments = @("--project", $ProjectPath, "--snapshot", $ResolvedOutput)
    if ($SelfTest) {
        $SnapshotArguments += "--self-test"
    }
    $SnapshotProcess = Start-Process -FilePath $CadPath -ArgumentList $SnapshotArguments -Wait -PassThru
    if ($SnapshotProcess.ExitCode -ne 0) {
        throw "kachakacha_cad failed with exit code $($SnapshotProcess.ExitCode)"
    }
} finally {
    $env:Path = $PreviousPath
    $env:QT_QPA_PLATFORM = $PreviousPlatform
}

Write-Host "Snapshot: $ResolvedOutput"
