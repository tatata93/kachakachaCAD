# Windows 開発環境

## 必須

- Git
- CMake
- C++20 対応コンパイラ

Windows では Visual Studio 2022 Build Tools の C++ 開発環境を想定する。

`No CMAKE_CXX_COMPILER could be found` が出る場合は、以下のどちらかが必要。

- Visual Studio Build Tools に「C++ によるデスクトップ開発」を追加する。
- Developer PowerShell / Developer Command Prompt から作業する。

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
