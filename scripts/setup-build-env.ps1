# kachakachaCAD ローカルビルド環境の一括セットアップ(Windows)
# 導入するもの: CMake / Visual Studio 2022 Build Tools (C++) / Python / Qt 6.9.2 / OCCT (master固定コミット)
# 実行中にユーザーアカウント制御(UAC)の確認が数回出るので「はい」を選ぶこと。
$ErrorActionPreference = 'Stop'
$occtCommit = '3d097a0328e71b826377d4814ab05ec3c3d23871'

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
        [Environment]::GetEnvironmentVariable('Path', 'User')
}

Write-Host '=== 1/4 CMake / Visual Studio Build Tools / Python (winget) ==='
winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements --silent
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { throw "CMake install failed: $LASTEXITCODE" }
winget install --id Python.Python.3.12 -e --accept-source-agreements --accept-package-agreements --silent
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { throw "Python install failed: $LASTEXITCODE" }
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements `
    --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189 -and $LASTEXITCODE -ne 3010) { throw "Build Tools install failed: $LASTEXITCODE" }
Refresh-Path

Write-Host '=== 2/4 Qt 6.9.2 (aqtinstall) ==='
if (-not (Test-Path 'C:\Qt\6.9.2\msvc2022_64\bin\Qt6Core.dll')) {
    python -m pip install --upgrade aqtinstall
    python -m aqt install-qt windows desktop 6.9.2 win64_msvc2022_64 --outputdir C:\Qt
} else {
    Write-Host 'Qt 6.9.2 は導入済み'
}

Write-Host '=== 3/4 OCCT (master) のビルド: 30分〜1時間かかります ==='
if (-not (Test-Path 'C:\occt-install\cmake\OpenCASCADEConfig.cmake') -and
    -not (Test-Path 'C:\occt-install\lib\cmake\opencascade\OpenCASCADEConfig.cmake')) {
    if (-not (Test-Path 'C:\OCCT-src')) {
        git clone https://github.com/Open-Cascade-SAS/OCCT.git C:\OCCT-src
    }
    git -C C:\OCCT-src fetch origin $occtCommit
    git -C C:\OCCT-src checkout $occtCommit
    cmake -S C:\OCCT-src -B C:\OCCT-build -G 'Visual Studio 17 2022' -A x64 `
        -DCMAKE_INSTALL_PREFIX=C:\occt-install `
        -DBUILD_MODULE_ApplicationFramework=OFF `
        -DBUILD_MODULE_DataExchange=ON `
        -DBUILD_MODULE_DETools=OFF `
        -DBUILD_MODULE_Draw=OFF `
        -DBUILD_MODULE_Visualization=OFF `
        -DBUILD_DOC_Overview=OFF `
        -DUSE_FREETYPE=OFF -DUSE_TK=OFF -DUSE_TCL=OFF -DUSE_OPENGL=OFF `
        -DUSE_FREEIMAGE=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF `
        -DUSE_TBB=OFF -DUSE_VTK=OFF
    cmake --build C:\OCCT-build --config Release
    cmake --install C:\OCCT-build --config Release
} else {
    Write-Host 'OCCT は導入済み'
}

Write-Host '=== 4/4 確認 ==='
Refresh-Path
cmake --version | Select-Object -First 1
python --version
if (Test-Path 'C:\Qt\6.9.2\msvc2022_64\bin\Qt6Core.dll') { Write-Host 'Qt: OK' } else { Write-Host 'Qt: 見つかりません' }
if ((Test-Path 'C:\occt-install\cmake\OpenCASCADEConfig.cmake') -or
    (Test-Path 'C:\occt-install\lib\cmake\opencascade\OpenCASCADEConfig.cmake')) { Write-Host 'OCCT: OK' } else { Write-Host 'OCCT: 見つかりません' }
Write-Host ''
Write-Host 'セットアップ完了。BUILD_LOCAL.cmd でローカルビルド+テスト+起動ができます。'
