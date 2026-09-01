# 開発手順

## 作業前

0. **同期する。** Windows は `.\scripts\sync.ps1`、Linux/macOS は `./scripts/sync.sh`。
   複数PCで開発しているため、これを飛ばすと未コミット作業と衝突する。
   詳細は `docs/multi-machine-development.md`。
1. `AGENTS.md` を確認する。
2. 関連する `docs/` の仕様を確認する。
3. 変更する範囲を小さく決める。
4. その変更が模型製作のどの工程を助けるかを確認する。

## 実装中

- UIより先に、可能な限り幾何コアで仕様を表現する。
- 作業平面、ワイヤー、面、板の関係を壊さない。
- 大きな設計判断は `docs/adr/` に記録する。
- 便利そうな機能を勝手に増やさない。

## 作業後

初回だけ、`%USERPROFILE%\vcpkg` にvcpkgを用意し、Open CASCADEを導入する。

```powershell
& "$env:USERPROFILE\vcpkg\vcpkg.exe" install opencascade:x64-windows
```

`configure.ps1` はこのvcpkg toolchainを自動検出する。Qtは `C:\Qt\6.9.2\msvc2022_64` を使う。

PowerShell で以下を実行する。

```powershell
.\scripts\check.ps1
```

最低限、構成、ビルド、テストが通ることを確認する。

画面表示を確認する場合:

```powershell
.\scripts\run-viewer.ps1
```

数値入力プロジェクトを確認する場合:

```powershell
.\scripts\run-viewer.ps1 -Project examples\first-check.kcd
```

完成受入モデルをQt版で確認する場合:

```powershell
.\scripts\run-cad.ps1 -Project examples\railway-nose-acceptance.kcd
```

配布可能なReleaseフォルダを作り、同梱DLLを含めて自己検査する場合:

```powershell
.\scripts\package-cad.ps1
```

## Definition of Done

- 何を作ったか説明できる。
- どの製作工程に効くか説明できる。
- テストまたは確認手順がある。
- 既存の方針文書と矛盾しない。
- 未決定事項があれば文書に残っている。

## 作業を終えるとき

そのPCを離れる前に、必ずコミットしてGitHubへpushする。

```powershell
git add -A
git commit -m "作業内容"
git push
```

未コミットのまま別PCで作業しないこと。
