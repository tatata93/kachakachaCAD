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
$LightCase = Join-Path $RepoRoot "examples\panorama-light-case.kcd"
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
    @{ Name = "arc-endpoints-radius"; Project = $FirstCheck; State = "arc-endpoints" },
    @{ Name = "arc-start-direction"; Project = $FirstCheck; State = "arc-tangent" },
    @{ Name = "weak-snapping"; Project = $FirstCheck; State = "snap" },
    @{ Name = "workplane"; Project = $FirstCheck; State = "workplane" },
    @{ Name = "numeric-input"; Project = $FirstCheck; State = "numeric" },
    @{ Name = "wire-edit"; Project = $FirstCheck; State = "edit" },
    @{ Name = "direct-transforms"; Project = $FirstCheck; State = "transforms" },
    @{ Name = "curve-trim"; Project = $FirstCheck; State = "trim" },
    @{ Name = "curve-extend"; Project = $FirstCheck; State = "extend" },
    @{ Name = "machining"; Project = $FirstCheck; State = "machining" },
    @{ Name = "surface-sections"; Project = $Acceptance; State = "surface" },
    @{ Name = "composite-planar-surface"; Project = $FirstCheck; State = "composite-surface" },
    @{ Name = "surface-projection"; Project = $Acceptance; State = "projection" },
    @{ Name = "protruding-light-case"; Project = $LightCase; State = "lightcase" },
    @{ Name = "plate-create"; Project = $Acceptance; State = "plate-create" },
    @{ Name = "plate-direction"; Project = $Acceptance; State = "direction" },
    @{ Name = "forming-jig"; Project = $Acceptance; State = "jig" },
    @{ Name = "openings"; Project = $Acceptance; State = "openings" },
    @{ Name = "relief-cuts"; Project = $Acceptance; State = "relief" },
    @{ Name = "plate-split"; Project = $Acceptance; State = "split" },
    @{ Name = "planar-output"; Project = $FirstCheck; State = "planar-output" },
    @{ Name = "flat-pattern"; Project = $Acceptance; State = "flat-pattern" },
    @{ Name = "assembly-output"; Project = $Acceptance; State = "assembly-output" },
    @{ Name = "model-output"; Project = $Acceptance; State = "output" },
    @{ Name = "model-inspection"; Project = $Acceptance; State = "inspection" },
    @{ Name = "display-settings"; Project = $Acceptance; State = "display" },
    @{ Name = "display-background-grid"; Project = $FirstCheck; State = "display-grid" },
    @{ Name = "measure-3d-angle"; Project = $FirstCheck; State = "measure-3d" },
    @{ Name = "measure-curve-normal"; Project = $FirstCheck; State = "measure-normal" },
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
