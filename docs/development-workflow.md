# 開発手順

## 作業前

1. **同期する。** Windows: `.\scripts\sync.ps1` / Linux・macOS: `./scripts/sync.sh`。
   作業ツリーが dirty なら新しい作業を始めず、まず `wip-<PC名>-<日付>` ブランチへ退避して push する。
2. `AGENTS.md` を確認する。
3. 触る領域の `docs/` 仕様(AGENTS.md の領域別表)を確認する。
4. 変更する範囲を小さく決め、その変更が模型製作のどの工程を助けるかを確認する。

## 実装中

- UIより先に、可能な限り幾何コアで仕様を表現する。
- 作業平面、ワイヤー、面、部材近似、板、型紙の関係を壊さない。
- 大きな設計判断は `docs/adr/` に記録する。
- 便利そうな機能を勝手に増やさない。

## 検証(環境別)

依存(Qt / vcpkg+OCCT)の場所は CMake が自動検出する(`cmake/KachakachaEnvironment.cmake`)。
標準外の場所に入れている場合の環境変数は `docs/setup-windows.md` を参照。

原則は**本番環境=Windows実機での検証**。必要な開発ツールはローカルPCへ導入してよい。

```powershell
# Windows: 構成+ビルド+テストをまとめて
.\scripts\check.ps1
# 画面確認・受入モデル
.\scripts\run-cad.ps1 -Project examples\railway-nose-acceptance.kcd
# セルフテスト
build-msvc2022-x64\Release\kachakacha_cad.exe --self-test
```

```bash
# Linux / クラウド: コアのみの最低ライン
./scripts/check.sh linux-core
```

Qt・OCCT・UIに触れる変更は、Windows実機かCI(`windows-build.yml`)が通るまで「検証済み」としない。
mainへのpushはCIを起動し `ci-latest` の配布zipを更新するため、mainは常にグリーンに保つ。

## 作業後

そのPC・環境を離れる前に、必ずコミットしてGitHubへpushする。

```powershell
git add -A
git commit -m "作業内容"
git push
```

pushできない環境(クラウド)は `docs/multi-machine-development.md` の bundle 受け渡しで必ず届ける。
未コミット・未pushのまま別環境で作業しない。

## Definition of Done

- 何を作ったか、どの製作工程に効くかを説明できる。
- 自動テストまたは再現可能な検証手順があり、環境別の検証ゲートを通っている。
- 既存の方針文書と矛盾しない。未決定事項は文書に残っている。
- **画面の目視確認はオーナーの受入作業。** エージェントは起動可能な状態まで届けて報告する。
