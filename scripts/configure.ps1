# 例: .\scripts\configure.ps1                    (プリセット windows-msvc)
#     .\scripts\configure.ps1 -Preset windows-core
#     .\scripts\configure.ps1 -BuildDir build-試験用   (プリセットを使わない旧来の方法)
param(
    [string]$Preset = "",
    [string]$BuildDir = "",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

# 既定は従来どおりのビルド先(build-msvc2022-x64)。
# プリセットを使いたい場合だけ -Preset か KACHACAD_PRESET を指定する。
if ($Preset -eq "" -and $env:KACHACAD_PRESET) { $Preset = $env:KACHACAD_PRESET }

if ($Preset -ne "") {
    Push-Location $RepoRoot
    try {
        Invoke-Checked "cmake" @("--preset", $Preset)
    }
    finally {
        Pop-Location
    }
    return
}

# --- プリセットを使わない場合(ビルド先を明示したいとき) ---
if ($BuildDir -eq "") { $BuildDir = "build-msvc2022-x64" }
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Generator -eq "") {
    $VsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWherePath) {
        $Vs2022Path = & $VsWherePath -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $null -ne $Vs2022Path -and $Vs2022Path.Length -gt 0) {
            $Generator = "Visual Studio 17 2022"
        }
    }
}

$CMakeArgs = @("-S", $RepoRoot, "-B", $BuildPath)
if ($Generator -ne "") {
    $CMakeArgs += @("-G", $Generator)
    if ($Generator.StartsWith("Visual Studio")) {
        $CMakeArgs += @("-A", "x64")
    }
}

# vcpkg と Qt の場所は CMake 側 (cmake/KachakachaEnvironment.cmake) が自動検出する。
# ここで絶対パスを渡さないこと。
Invoke-Checked "cmake" $CMakeArgs
