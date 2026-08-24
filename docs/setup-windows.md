# Windows 開発環境

## 必須

- Git
- CMake
- C++20 対応コンパイラ

Windows では Visual Studio 2022 Build Tools の C++ 開発環境を想定する。

`No CMAKE_CXX_COMPILER could be found` が出る場合は、以下のどちらかが必要。

- Visual Studio Build Tools に「C++ によるデスクトップ開発」を追加する。
- Developer PowerShell / Developer Command Prompt から作業する。

環境変数に `Path` と `PATH` が重複していると、MSBuild が `CL.exe` の起動に失敗する場合がある。開発スクリプトは起動時にプロセス内の `Path` を正規化する。

## 後で必要になるもの

- Qt 6
- Open CASCADE Technology
- Eigen

初期の幾何コアは、Qt と OCCT が未導入でもビルドできるようにする。

## 確認

```powershell
.\scripts\doctor.ps1
```

## 構成・ビルド・テスト

```powershell
.\scripts\configure.ps1
.\scripts\build.ps1
.\scripts\test.ps1
```

まとめて実行する場合:

```powershell
.\scripts\check.ps1
```

既定のビルド先は `build-msvc2022-x64`。Visual Studio Build Tools 2022 が入っていれば、自動で `Visual Studio 17 2022` を使う。

Qt版をビルドすると、必要なQt/Open CASCADE DLLと `platforms` プラグインが `kachakacha_cad.exe` の隣へ自動配置される。開発用EXEを直接起動しても、`TKOffset.dll` やQt platform pluginを別途探す必要はない。
