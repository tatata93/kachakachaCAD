param(
    [string]$Preset = "windows-msvc",
    [string]$BuildDir = "",
    [string]$Config = "Release"
)

# Wire-first V2 の構成・ビルド・試験をまとめて回す。
# V2の受入試験(名前が v2_ で始まるもの)が1件も登録されていなければ失敗にする。

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    switch ($Preset) {
        "windows-core" { $BuildDir = "build-core" }
        default        { $BuildDir = "build-msvc2022-x64" }
    }
}

Write-Host "== full tests (including all V2 tests, once) =="
& python (Join-Path $PSScriptRoot "verify.py") full --preset $Preset --config $Config
if ($LASTEXITCODE -ne 0) { throw "full verification failed" }

Write-Host "check-v2: OK"
