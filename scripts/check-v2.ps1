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

Write-Host "== configure ($Preset) =="
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

Write-Host "== build =="
cmake --build --preset $Preset --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "== V2 tests registered? =="
$listing = & ctest --test-dir $BuildDir -C $Config -N -R '^v2_' 2>&1
$found = ([regex]::Matches(($listing -join "`n"), 'Test\s+#\d+')).Count
Write-Host "v2 tests found: $found"
if ($found -eq 0) {
    throw "no V2 test is registered. check-v2 requires at least one test named v2_*."
}

Write-Host "== V2 tests =="
ctest --test-dir $BuildDir -C $Config -R '^v2_' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "V2 tests failed" }

Write-Host "== existing tests (must not regress) =="
ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "existing tests failed" }

Write-Host "check-v2: OK"
