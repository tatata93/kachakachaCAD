# 開発手順

## 作業前

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

## Definition of Done

- 何を作ったか説明できる。
- どの製作工程に効くか説明できる。
- テストまたは確認手順がある。
- 既存の方針文書と矛盾しない。
- 未決定事項があれば文書に残っている。
