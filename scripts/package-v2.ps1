# V2 の配布ひとまとめを作る(WP-12)。
#
# 中身:
#   kachakacha_cad_next.exe と Qt / OpenCASCADE の実行時ファイル
#   samples\v2-sample.kcd2(配る見本)
#   manual\(図つき取扱説明書)
#   legal\(第三者のライセンス表示。無いと配れない)
#
# 作ったあと、そのひとまとめの exe を実際に動かして確かめる。
#   1. --self-test が全部通る
#   2. 見本を --open で開ける
#   3. 開いた絵が撮れる
# ここを通らなければ zip にしない。動かないものを配らないためである。

param(
    [string]$BuildDir = "build-msvc2022-x64",
    [string]$Config = "Release",
    [string]$OutputDir = "out\kachakachaCAD-v2",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# 組み立て先は、相対でも絶対でも受ける。ctest からは絶対で渡ってくる。
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
    if ($LASTEXITCODE -ne 0) { throw "組み立てに失敗しました" }
}

# 道具がそろっていないときは「やっていない」と言って抜ける。
# 失敗と区別しないと、Qt を入れていない機械でいつも赤くなり、
# 本当の失敗に気づけなくなる。ctest はこの 77 を「飛ばした」と読む。
foreach ($needed in @($BuiltExe, $SampleWriter, $DeployTool)) {
    if (-not (Test-Path $needed)) {
        Write-Host "配布ひとまとめは作れません(見つかりません: $needed)"
        exit 77
    }
}

$Output = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDir))
$OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out"))
if (-not $Output.StartsWith($OutputRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "出す先は out\ の下でなければなりません: $Output"
}
if (Test-Path $Output) { Remove-Item -LiteralPath $Output -Recurse -Force }
New-Item -ItemType Directory -Path $Output | Out-Null

$Deployed = Join-Path $Output $ExeName
Copy-Item -LiteralPath $BuiltExe -Destination $Deployed -Force

& $DeployTool --release --no-translations --compiler-runtime --no-opengl-sw `
    --no-system-d3d-compiler --no-system-dxc-compiler `
    --dir $Output $Deployed
if ($LASTEXITCODE -ne 0) { throw "windeployqt に失敗しました" }

# 画面を出さずに確かめるための差し込み。無いと --self-test が動かない。
$Offscreen = Join-Path $QtPrefix "plugins\platforms\qoffscreen.dll"
if (Test-Path $Offscreen) {
    $Platforms = Join-Path $Output "platforms"
    if (-not (Test-Path $Platforms)) { New-Item -ItemType Directory -Path $Platforms | Out-Null }
    Copy-Item -LiteralPath $Offscreen -Destination (Join-Path $Platforms "qoffscreen.dll") -Force
}

# OpenCASCADE の実行時ファイル。
$VcpkgRuntime = Join-Path $env:USERPROFILE "vcpkg\installed\x64-windows\bin"
if (Test-Path $VcpkgRuntime) {
    $Collector = Join-Path $PSScriptRoot "collect-runtime-dependencies.cmake"
    cmake "-DKACHACAD_EXECUTABLE=$Deployed" "-DKACHACAD_OUTPUT_DIRECTORY=$Output" `
        "-DKACHACAD_VCPKG_RUNTIME=$VcpkgRuntime" `
        "-DKACHACAD_QT_RUNTIME=$(Join-Path $QtPrefix 'bin')" -P $Collector
    if ($LASTEXITCODE -ne 0) { throw "実行時ファイルを集められませんでした" }
}

# 見本を作り直してから入れる。古いものを配らないためである。
$Samples = Join-Path $Output "samples"
New-Item -ItemType Directory -Path $Samples | Out-Null
& $SampleWriter (Join-Path $Samples "v2-sample.kcd2")
if ($LASTEXITCODE -ne 0) { throw "見本を作れませんでした" }

# 取扱説明書。図ごと入れる。
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs\manual") -Destination (Join-Path $Output "manual") -Recurse -Force

# 第三者のライセンス表示。無いものは配らない。
$Legal = Join-Path $RepoRoot "legal"
if (-not (Test-Path $Legal)) { throw "ライセンス表示がありません: $Legal" }
Copy-Item -LiteralPath $Legal -Destination $Output -Recurse -Force
foreach ($name in @("LICENSE", "COPYRIGHT")) {
    $source = Join-Path $RepoRoot $name
    if (Test-Path $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $Output $name) -Force }
}

# ここから、作ったひとまとめを実際に動かして確かめる。
$Previous = $env:QT_QPA_PLATFORM
try {
    $env:QT_QPA_PLATFORM = "offscreen"

    & $Deployed --version
    if ($LASTEXITCODE -ne 0) { throw "版を出せませんでした" }

    & $Deployed --self-test
    if ($LASTEXITCODE -ne 0) { throw "自己試験が通りませんでした" }

    $Opened = Join-Path $Output "opened-sample.png"
    & $Deployed --open (Join-Path $Samples "v2-sample.kcd2") --snapshot $Opened
    if ($LASTEXITCODE -ne 0) { throw "見本を開けませんでした" }
    if (-not (Test-Path $Opened) -or (Get-Item -LiteralPath $Opened).Length -lt 1024) {
        throw "開いた絵が作られませんでした: $Opened"
    }
    Remove-Item -LiteralPath $Opened -Force
}
finally {
    if ($null -eq $Previous) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $Previous }
}

$Zip = "$Output.zip"
if (Test-Path $Zip) { Remove-Item -LiteralPath $Zip -Force }
Compress-Archive -Path (Join-Path $Output "*") -DestinationPath $Zip
Write-Host "配布ひとまとめ: $Zip"
