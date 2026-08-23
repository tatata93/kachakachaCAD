param(
    [string]$BuildDir = "build-product-release",
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config
}

$CadPath = Join-Path $RepoRoot "$BuildDir\$Config\kachakacha_cad.exe"
$OutputDirectory = Join-Path $RepoRoot "docs\manual-assets"
$FirstCheck = Join-Path $RepoRoot "examples\first-check.kcd"
$Acceptance = Join-Path $RepoRoot "examples\railway-nose-acceptance.kcd"
$QtBin = "C:\Qt\6.9.2\msvc2022_64\bin"
$VcpkgBin = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"

if (-not (Test-Path $CadPath)) {
    throw "CAD executable was not found: $CadPath"
}
if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$Shots = @(
    @{ Name = "overview"; Project = $Acceptance; State = "overview" },
    @{ Name = "view-controls"; Project = $Acceptance; State = "view" },
    @{ Name = "point-grid"; Project = $FirstCheck; State = "grid" },
    @{ Name = "drawing-tools"; Project = $FirstCheck; State = "drawing" },
    @{ Name = "workplane"; Project = $FirstCheck; State = "workplane" },
    @{ Name = "numeric-input"; Project = $FirstCheck; State = "numeric" },
    @{ Name = "wire-edit"; Project = $FirstCheck; State = "edit" },
    @{ Name = "machining"; Project = $FirstCheck; State = "machining" },
    @{ Name = "surface-sections"; Project = $Acceptance; State = "surface" },
    @{ Name = "plate-direction"; Project = $Acceptance; State = "direction" },
    @{ Name = "openings"; Project = $Acceptance; State = "openings" },
    @{ Name = "plate-split"; Project = $Acceptance; State = "split" },
    @{ Name = "model-output"; Project = $Acceptance; State = "output" },
    @{ Name = "selection-info"; Project = $Acceptance; State = "info" }
)

$PreviousPath = $env:Path
$PreviousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:Path = "$QtBin;$VcpkgBin;$PreviousPath"
    $env:QT_QPA_PLATFORM = "offscreen"
    foreach ($Shot in $Shots) {
        $OutputPath = Join-Path $OutputDirectory ($Shot.Name + ".png")
        $Process = Start-Process -FilePath $CadPath -ArgumentList @(
            "--project", $Shot.Project,
            "--manual-state", $Shot.State,
            "--snapshot", $OutputPath
        ) -Wait -PassThru -WindowStyle Hidden
        if ($Process.ExitCode -ne 0) {
            throw "Manual screenshot '$($Shot.Name)' failed with exit code $($Process.ExitCode)"
        }
        if (-not (Test-Path $OutputPath) -or (Get-Item -LiteralPath $OutputPath).Length -lt 1024) {
            throw "Manual screenshot '$($Shot.Name)' was not created correctly"
        }
    }
}
finally {
    $env:Path = $PreviousPath
    if ($null -eq $PreviousPlatform) {
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    }
    else {
        $env:QT_QPA_PLATFORM = $PreviousPlatform
    }
}

Write-Host "Manual screenshots: $OutputDirectory"
