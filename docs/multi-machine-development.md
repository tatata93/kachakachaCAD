# 複数PCでの開発手順

このプロジェクトは複数のPC(自宅PC・別PC・クラウドのLinux検証環境)で開発する。
食い違いの原因はほぼ全て「pushし忘れ」と「PC固有の絶対パス」なので、その2点を仕組みで防ぐ。

## 鉄則

1. **作業を始める前に必ず同期する。**
   - Windows: `.\scripts\sync.ps1`
   - Linux / macOS: `./scripts/sync.sh`
2. **そのPCを離れる前に必ずコミットしてpushする。** 未コミットのまま別PCで作業すると、
   後から手作業のマージが必要になる(実際に2026-08-29に発生した)。
3. **PC固有のパスをリポジトリに書かない。** Qtやvcpkgの場所は環境変数か `local.cmake` で渡す。
4. **ビルドディレクトリは共有しない。** `build*/` と `out/` は `.gitignore` 済み。

## 環境の違いをどう吸収しているか

| 対象 | 解決方法 |
| --- | --- |
| vcpkg (Open CASCADE) | `VCPKG_ROOT` → `%USERPROFILE%\vcpkg` → `$HOME/vcpkg` → `C:\vcpkg` → `/opt/vcpkg` の順に自動検出 |
| Qt 6 | `KACHACAD_QT_ROOT` → `QT_ROOT_DIR` → `C:\Qt\6.*\msvc*_64` / `/opt/qt6` などを新しい順に自動検出 |
| 生成器・ビルド先 | `CMakePresets.json` のプリセット(`windows-msvc` / `linux` / `*-core`) |
| 改行コード | `.gitattributes` でLFに統一。`.ps1` `.cmd` `.bat` だけCRLF |
| そのPCだけの設定 | リポジトリ直下の `local.cmake`(gitignore済み) |

自動検出が外れる場合だけ、そのPCで環境変数を設定する。

```powershell
# Windows (そのPCで1回)
setx KACHACAD_QT_ROOT "C:\Qt\6.9.2\msvc2022_64"
setx VCPKG_ROOT "%USERPROFILE%\vcpkg"
```

```bash
# Linux / macOS (~/.bashrc など)
export KACHACAD_QT_ROOT=/opt/qt6
export VCPKG_ROOT="$HOME/vcpkg"
```

それでも足りない場合は `local.cmake` を作る(gitには入らない)。

```cmake
# local.cmake の例
set(KACHACAD_QT_ROOT "D:/Qt/6.9.2/msvc2022_64")
set(KACHACAD_VCPKG_ROOT "D:/vcpkg")
```

## PCの種類ごとの使い方

### 開発ツールが揃っているPC(Qt + OCCT + MSVC)

```powershell
.\scripts\sync.ps1
.\scripts\check.ps1          # 構成→ビルド→テスト
.\scripts\run-cad.ps1        # 起動して動作確認
```

### 開発ツールが無いPC

ローカルビルドはしない。GitHub Actions(`.github/workflows/windows-build.yml`)が
`main` へのpushごとにWindows版をビルドし、`ci-latest` タグに
`kachakachaCAD-windows-x64.zip` を公開する。これを展開して起動する。

### Linux / クラウド検証環境

```bash
./scripts/doctor.sh
./scripts/check.sh linux-core   # 依存を入れずコアとテストだけ(数分)
./scripts/check.sh linux        # Qt/OCCTがある場合はフルビルド
```

Qt / OCCT のソースビルド手順は `docs/linux-build.md`。

## 作業が分岐してしまった場合

未コミットの作業を抱えたまま別PCの変更を取り込むときは、**先にブランチとして保存する**。

```bash
git checkout -B wip-<PC名>-<日付>
git add -A
git commit -m "WIP: 作業内容"
git push -u origin wip-<PC名>-<日付>
```

そのうえで `main` を取り込み、競合を解消してから `main` へマージする。
いきなり `git pull` や `git checkout .` をしないこと。
