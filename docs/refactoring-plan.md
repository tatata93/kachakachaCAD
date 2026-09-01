# 構造改修計画

## 背景

2026-08-31、外部調査により本リポジトリの実装状況を確認した。結論は次の通り。

- `src/core` の幾何・モデルコアは健全であり、作り直しは不要。
- UI層(`src/apps/cad/MainWindow.cpp`)が肥大しており、可読性・変更容易性を損ねている。
  行数などの数値は文書内の値を信じず、必ず計測してから使う(例: `wc -l src/apps/cad/MainWindow.cpp`。
  参考: 調査時 11,758行 → セルフテスト分離と統合を経た 2026-09-02 時点で 10,449行)。

したがって、コアの設計を変更する大規模リライトではなく、UI層を中心とした段階的な分割・整理を行う。この文書は、その作業項目をチェックリストとして管理し、今後どのAIエージェントが作業しても同じ計画の下で続きを進められるようにするためのものである。

## 作業の原則

- オーナーは早期の実用開始を最優先している。(2)以降の項目は、機能追加を止めて一括実施するのではなく、該当箇所に触れる機能追加・修正の直前に併せて少しずつ実施する。実用を妨げる大規模な一括改修を勝手に始めない。
- 挙動を変えるリファクタと機能追加を同一コミットにしない。1コミット1目的を守る。
- コードの移動は逐語的に行う(切って貼るか、確実な自動化で移す)。手で書き写して再入力しない。書き写しは差分確認を不可能にし、微妙な挙動変化を混入させる。
- 変更後の検証は環境別(AGENTS.md「検証ゲート」参照): Windowsは `scripts\check.ps1` と `kachakacha_cad.exe --self-test`、Linux/クラウドは `./scripts/check.sh linux-core` が最低ライン。Qt/OCCT/UIに触れる変更はWindows実機かCIが通るまでmainへ入れない。
- 各チェックリスト項目は独立して着手・完了できるように分割してある。1項目=最低1コミットを目安にする。
- 状態は着手した作業者が更新する。**着手時は、他の変更より先に状態を「進行中」へ書き換えてコミット・pushする**(複数エージェント・複数PCの同時着手を防ぐロック代わり)。完了したら「完了」に書き換え、簡単な備考(コミットハッシュ等)を添える。

## チェックリスト

状態の凡例: 未着手 / 進行中 / 完了

### (1) セルフテストとマニュアル用スクリーンショットの分離

状態: 完了(2026-08-31、コミット c488ea6。`RunCreationSelfTest` と `PrepareManualScreenshot` の計2,617行を `MainWindowSelfTest.cpp` へ逐語移動。Linuxフルビルド(docs/linux-build.md)で警告ゼロのコンパイル、16テスト、`--self-test` の通過を確認済み)

`MainWindow.cpp` 内の `RunCreationSelfTest`(約1,972行)と `PrepareManualScreenshot`(約647行)を、それぞれ独立した翻訳単位(例: `MainWindowSelfTest.cpp`、`MainWindowManualScreenshot.cpp` など)へ切り出す。

- なぜ: この2関数だけで `MainWindow.cpp` の約22%を占める。通常のUI編集作業でこれらを読み飛ばす必要が常に発生しており、ファイル肥大の最大の要因のひとつになっている。
- 検証方法: 分離後も `--self-test` が同じ内容で成功し、マニュアル用スクリーンショット生成コマンドが同じ画像を出力することを確認する。
- 運用上の注意(2026-09-02追記): 分離後の `MainWindow.cpp` と `MainWindowSelfTest.cpp` は `#include`・`using` 宣言を常に一致させること。片方だけ更新するとWindowsビルドが壊れる(統合作業時に2回発生)。

### (2) 板材Add/Remove関数のパラメータ化とSuggested*Name統合

状態: 完了(2026-08-31、コミット 78a1bd8 / 6402db9。共通メソッド `ModifySelectedPlateWires` と `SuggestedName` へ統合し純減220行。Linuxフルビルドで警告ゼロ・16テスト・`--self-test` 通過を確認済み)

`AddSelectedPlateOpenings` / `RemoveSelectedPlateOpenings` / `AddSelectedPlateReliefCuts` / `RemoveSelectedPlateReliefCuts` / `AddSelectedPlateSplitLines` / `RemoveSelectedPlateSplitLines` の6関数はほぼ同一の処理を開口・切れ目・分割線の3種類に対して繰り返しているだけなので、共通処理をパラメータ化した1組のAdd/Remove関数へ統合する。同様に `SuggestedPlaneName` / `SuggestedWireName` / `SuggestedDirectGroupName` / `SuggestedChamferName` / `SuggestedFilletName` / `SuggestedSurfaceName` / `SuggestedPlateName` / `SuggestedBodyName` / `SuggestedDimensionName` の名前提案系関数群も、接頭辞と既存名の集合を引数に取る共通関数へ統合する。

- なぜ: コピペ実装はロジック修正時の追従漏れを生む。すでに「ほぼ同一の関数を3つ以上コピペしない」というガードレールに違反している既存コードであり、最優先で解消する対象。
- 検証方法: 開口・切れ目・分割線それぞれの追加/削除を手動または既存テストで確認し、名前の重複回避ロジックが全対象で同じ結果になることを確認する。

### (3) 9枚のUIパネルをQWidgetサブクラスへ抽出

状態: 未着手

`BuildDrawingPanel` / `BuildPlanePanel` / `BuildWirePanel` / `BuildEditPanel` / `BuildMachiningPanel` / `BuildSurfacePanel` / `BuildOutputPanel` / `BuildDisplayPanel` / `BuildInfoPanel` の9関数を、それぞれ独立した `QWidget` サブクラス(ファイル)へ抽出する。1回の変更で1パネルずつ進め、`MainWindow` 側はパネルの生成と配線のみを残す。

- なぜ: 9パネル分のUI構築コードが `MainWindow.cpp` の大部分を占めており、1パネルの変更が他パネルと無関係にレビュー・テストできるようにする必要がある。
- 検証方法: 1パネル抽出するごとに `check.ps1` と `--self-test` を通し、該当パネルの画面操作(作図・入力・ボタン)が抽出前と同じ挙動であることを目視確認する。

### (4) ViewportToolのStrategy/Command化

状態: 未着手

`CadViewport.cpp` 内に散在する `tool_ ==` 比較(現在128箇所)を、ツールごとのStrategyまたはCommandオブジェクトへ置き換える。

- なぜ: 新しいツールを1つ追加するたびに128箇所前後の分岐に手を入れる状態は、追加漏れとデグレの温床になる。ガードレールの「既存の分岐箇所を数えて増加を最小化する」を実現するには、分岐そのものの構造を変える必要がある。
- 検証方法: 各ツールの挙動(クリック・ドラッグ・スナップ・確定・キャンセル)を移行前後で比較し、`--self-test` と手動での主要ツール一巡確認を行う。128箇所という基準数を `docs/refactoring-plan.md`(本項目)で追跡し、着手前後で件数の推移を記録する。

### (5) 名前参照更新の一元化(リネーム機能の前提)

状態: 未着手

オブジェクトは文字列名で相互参照される設計であり、将来のリネーム機能実装時に更新漏れが起きないよう、名前参照を保持するフィールドをここに一覧化し、以後フィールドを追加した作業者はこの一覧も更新する。

`src/core/kachakacha/model/Project.h` 時点での名前参照フィールド一覧:

- `WireEndpointReference::wireName` — 端点一致・接線拘束(`WireCoincidentConstraint` / `WireTangentConstraint`)が参照するワイヤー名。
- `WireMetadata::sourcePlaneName`(省略可) — ワイヤーの作図元となった作業平面名。
- `NamedPoint::sourcePlaneName`(省略可) — 作図点の作図元となった作業平面名。
- `NamedWire::Projection::sourceWireName` — 投影元の平面図ワイヤー名。
- `NamedWire::Projection::targetSurfaceName` — 投影先の面名。
- `NamedWire::PlateOffset::sourceWireName` — 板厚オフセット表示の元ワイヤー名。
- `NamedWire::PlateOffset::plateName` — 板厚オフセット表示が属する板材名。
- `NamedSurface::sourceWireNames` — 面(平面/曲面/ロフト)を構成する断面ワイヤー名の並び。
- `NamedPlate::sourceSurfaceName` — 板材の元になった面名。
- `NamedPlate::openingWireNames` — 板材の開口として登録されたワイヤー名の並び。
- `NamedPlate::reliefCutWireNames` — 板材の切れ目として登録されたワイヤー名の並び。
- `NamedPlate::splitWireNames` — 板材の分割線として登録されたワイヤー名の並び。
- `NamedBody::sourceSurfaceName` — 治具ボディの元になった面名。
- `DimensionReference::objectName` — 参照寸法(`ReferenceDimension`)がワイヤーまたは作業平面を指す名前。

- なぜ: リネーム機能を安全に実装するには、名前を書き換える際に更新すべき参照箇所を漏れなく把握できる必要がある。フィールド追加のたびにここへ追記するルールをAGENTS.mdのガードレールとして併設した。
- 検証方法: この一覧と `Project.h` の構造体定義を突き合わせ、差分がないことを確認する。リネーム機能実装時は、この一覧の全項目を更新対象としてテストする。

### (6) tests/ のテストフレームワーク移行

状態: 未着手

`tests/` 配下の各 `*_tests.cpp` は個別の `int main` を持つ自前の軽量チェック方式で、1個のアサート失敗がそのバイナリの残り全部を止める。Catch2など既存フレームワークへ移行し、個々のテストケース単位で成功/失敗を報告できるようにする。

- なぜ: 現状では1件の失敗が原因不明の後続失敗の山に見えてしまい、CI・ローカル双方で問題の切り分けに時間がかかる。
- 検証方法: 移行後も既存の全チェック内容が同数以上のテストケースとして存在し、意図的に1件だけ失敗させても他のケースの結果が個別に報告されることを確認する。

### (7) .kcdの前方互換

状態: 取り下げ(2026-09-02、オーナー判断で前方互換対応は不要と決定。旧ビルドで新ファイルが開けないのは許容する。以下は記録として残す)

`.kcd`(プロジェクトスクリプト)読み込み時に未知コマンドをエラーではなく警告として読み飛ばすモードの追加を検討する。

- なぜ: 新しいコマンドを追加した新バージョンで保存したファイルを、古いビルドや実験ブランチで開こうとした際に全体が読み込み不能になるのを避けたい。ただし「警告読み飛ばし」は形状が意図と異なるまま保存され得るため、まずは検討(要否・UI上の警告表示方法)から始める。
- 検証方法: 未知コマンドを含む `.kcd` を用意し、通常モードでは従来通り拒否されること、前方互換モードでは警告を出しつつ既知部分だけで読み込めることを確認する。

### (8) 将来項目

状態: 未着手

以下は優先度が低い、または判断材料が不足しているため「将来」枠として残す。

- Undoの差分化: 現状のUndoが状態の全体コピー的な実装であれば、操作ごとの差分(コマンドパターン)へ置き換える。大規模プロジェクトでのUndoのメモリ・速度改善が目的。着手前に現状のUndo実装を計測してから要否を判断する。
- `paintEvent`(`CadViewport.cpp`、1,376行)のシーン記述と描画の分離: 「何を描くか」の決定と「どう描くか」の描画処理が1関数に混在しているため、シーン記述(描くべき要素のリスト化)と描画(そのリストをQPainterへ流し込む)を分離する。差分描画やテスト容易性の向上が目的。
- `src/apps/viewer`(Win32 GDIによる並行実装)の廃止判断: Qt版(`src/apps/cad`)が本命として実用段階にある現状で、GDI版を維持し続ける価値があるか判断する。存続させる場合は維持コストを明記し、廃止する場合は削除コミットを別途起こす。

- なぜ: いずれも実装コストや判断材料の確認が先に必要で、他項目より優先度を下げている。
- 検証方法: 着手する場合は、この文書の該当項目に具体的な検証方法を書き足してから着手する。

## 既知の問題(未着手)

- .kcd 保存→再読込でワイヤの並び順が変わる(平面ワイヤが先、投影ワイヤが後にまとめられる)。モデル一覧の順序が保存前後で一致せず、セルフテスト内の固定インデックス前提も脆くしている。保存時に作成順を保持する書き出し順序(依存が満たせる範囲で)への変更を検討する。

## 追加済みの主要機能

- Gordon面(2026-08-31、コミット a1a501b / e32e7b3 / f391e7a): 断面2本以上+外形ガイド線1本以上を厳密に通る面。面タブの「断面と外形で面を作成」。ガイドが断面に0.01mm以内で交差していることを検証し、交差しない場合は距離付きでエラーにする。.kcdでは surface_gordon コマンド。
- ペーパークラフト3系統+ガイド付きロフト(2026-08-27〜29の作業を 2026-09-02 に main へ統合、マージ 38b26f5): `FacetedPapercraft`(多面体)、`BentSheetPapercraft`(連続曲げ板)、`FabricationPanelPapercraft`(製作用大部材)、`Surface::GuidedLoft`、面入力のグループ化(`NamedSurface::sourceWireGroups`、.kcd の surface_grouped)。ADR 0014〜0017。`docs/test-models/er1-er2-round-cab` に検証モデル一式。
- 複数環境対応(2026-09-02、809667f〜): Qt/vcpkg自動検出(`cmake/KachakachaEnvironment.cmake`)、`CMakePresets.json`、`scripts/*.sh`、MSVC `/utf-8` `/MP`、テストのタイムアウト。詳細は `docs/multi-machine-development.md`。
- 今後の大きな機能開発の軸は面展開の新パイプライン(`docs/surface-unfolding-spec.md`)。既存の展開4系統はそこへ統合・整理していく。

