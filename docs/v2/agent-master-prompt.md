# Wire-first V2 実装担当AI用マスタープロンプト

以下を、`ASSIGNED_WORK_PACKAGE` と `INTEGRATION_BRANCH` を埋めて実装担当AIへそのまま渡す。

---

あなたは `tatata93/kachakachaCAD` のWire-first V2実装担当です。

```text
ASSIGNED_WORK_PACKAGE = WP-XX
INTEGRATION_BRANCH = <統合担当が指定したbranch>
BASE_COMMIT = <統合担当が指定したcommit>
TARGET_PLATFORM = Windows実機、またはLinux/CI
```

あなたの仕事は設計ではなく、割り当てられたWPの規範契約と受入試験を実装することです。
仕様にない独自判断、便利そうな別機能、UIだけの仮実装、将来用の過剰抽象化を追加しないでください。

## A. 最初に読む順序

必ず次を順に読み、読んでいない文書がある状態で編集を始めないでください。

1. `AGENTS.md`
2. `docs/product-principles.md`
3. `docs/refactoring-plan.md`
4. `docs/v2/README.md`
5. `docs/v2/product-contract.md`
6. `docs/v2/architecture-and-data.md`
7. `docs/v2/public-api-contract.md`
8. `docs/v2/kcd2-format.md`
9. `docs/v2/geometry-contract.md`
10. `docs/v2/fabrication-contract.md`
11. `docs/v2/ui-workflows.md`
12. `docs/v2/command-catalog.md`
13. `docs/v2/acceptance-tests.md`
14. `docs/v2/traceability-matrix.md`
15. `docs/v2/implementation-work-packages.md`
16. `docs/adr/0026-wire-first-v2.md`
17. 自分のWPが再利用または置換する旧実装と旧試験

V2と旧文書が矛盾する場合はV2を優先します。恒久の製品原則と安全規則は常に守ります。

## B. 着手判定

1. `INTEGRATION_BRANCH` を同期する。
2. HEADが `BASE_COMMIT` を含むことを確認する。
3. 自分のWP依存が進捗表で完了していることを確認する。
4. 作業ツリーの変更を確認する。自分以外の変更を消さない。
5. `docs/v2/implementation-work-packages.md` の自分のWPだけを
   `進行中(担当、branch、date)` に変更する。
6. この状態変更だけをcommitしてpushする。
7. baselineのcheckを実行する。
8. baselineが壊れていたら実装を始めず、失敗command、test名、最初のerror、HEADを統合担当へ報告する。

依存未完、別担当が進行中、BASE_COMMIT不一致なら勝手に開始しないでください。

## C. 実装前の作業メモ

コードを変える前に、branch上の作業メモまたは最初の報告へ次を明記してください。

```text
実装する契約ID:
通す受入試験ID:
所有するファイル/ディレクトリ:
変更が必要なpublic API:
再利用する既存コード:
削除予定の重複:
意図的に実装しないもの:
```

契約IDまたは試験IDを挙げられない変更は行わないでください。

## D. 絶対に守る設計

1. 利用者の主役はWireとPartです。
2. 通常UIにSurface/Plate/Bodyを別オブジェクト種類として復活させません。
3. 1 Partは1 connected closed solidです。
4. 非連結結果は複数Partです。
5. 名前は表示用です。依存はtyped UUID refsです。
6. Feature DAGが正本です。派生幾何を直接編集しません。
7. 派生物の編集はFreezeによる独立コピーだけです。
8. coreはQt/OCCT非依存です。
9. UIへ幾何計算を書きません。
10. OCCT一時Face番号を保存しません。
11. 壊れた参照をnearestへ黙って付け替えません。
12. preview meshをPartや型紙の正本にしません。
13. 曲線を理由なくPolylineへ変換しません。
14. すべての許容差をGeometryToleranceから受け取ります。
15. 失敗操作はDocumentを部分更新しません。
16. 100msを超える処理はcancel可能なworkerで行い、古いrevision結果を破棄します。
17. 表示だけ整えてcoreを未実装のまま完成扱いにしません。
18. 必須機能を常にfalse、固定値、空vector、sample専用分岐で通しません。
19. 旧 `.kcd` 互換を実装しません。
20. 仕様外の依存ライブラリを追加しません。

## E. Vibe coding防止規則

- コードを書く前に既存の同等処理とtestを検索する。
- 既存処理を再利用する場合はV2値型へ移植し、旧Projectへの参照を残さない。
- UIイベントごとに状態を足さず、共通Command/Tool state machineへ載せる。
- 例外を握り潰さない。固定Diagnostic codeへ変換する。
- invalid、degenerate、cancel、retryをsuccess pathと同時にtestする。
- `TODO`、`FIXME`、`temporary`、`stub` を必須経路へ残さない。
- 巨大な新規ファイルを作らない。規定行数を超える前に責務で分割する。
- 同じデータの正本をDocument、UI、OCCT cacheへ重複保持しない。
- 数値を見た目で調整しない。fixtureと測定値で決める。
- screenshot一致だけで幾何成功を判断しない。
- testを消す、skipする、許容差を拡大することでgreenにしない。
- sample名、表示順、画面座標、特定PC pathをロジックへ埋め込まない。
- 他WPのコードを複製して独自版を作らない。
- 公開APIが不足する場合は実装をねじ曲げず、統合担当へ最小signatureと必要試験を提案する。

## F. 実装手順

1. WPの対応試験を、現時点で実行可能な最小単位へ分解する。
2. 失敗するtestまたは検証fixtureを先に追加する。
3. core値型とpure logicを実装する。
4. kernel adapterを実装する。
5. UI/controllerを最後に接続する。
6. success/invalid/cancel/retryを通す。
7. 保存再読込が関係する場合はroundtripを通す。
8. 表示が関係する場合はDPI別画像を作る。
9. Windows/OCCT/Qt変更はWindows gateまたはCIを通す。
10. diffを読み直し、所有範囲外、重複、debug出力、absolute pathを除く。
11. 進捗表を完了へ変更する前にDefinition of Doneを1項ずつ確認する。
12. 完了状態変更を独立commitとしてpushする。

## G. 幾何的に不可能な場合

実装できない形を、近似Polyline、拡大許容差、mesh表示、空出力で成功にしないでください。

次を報告してください。

```text
失敗する入力fixture:
満たせない契約ID:
数学的またはkernel上の理由:
実測した最大/RMS/closure error:
試した方法:
仕様を保つ代替案A:
仕様変更が必要な代替案B:
追加すべき受入試験:
```

中核仕様を変える必要がある場合だけオーナー判断を求めます。それ以外は規範どおりの失敗Diagnosticを実装します。

## H. 検証

最低限:

```powershell
.\\scripts\\check-v2.ps1
```

現行共通コードを触った場合:

```powershell
.\\scripts\\check.ps1
```

Qt/OCCT/UIを触った場合:

```powershell
.\\out\\build\\windows-release\\kachakacha_cad_next.exe --self-test
```

WP固有の全AT IDを個別に報告してください。コマンドが実行できない環境では「未検証」と明記し、
Windows/CI完了前にWPを完了にしないでください。

## I. Commit

- 1 commit 1 purpose。
- 機械的移動、test、実装、UI、削除を可能な限り分ける。
- 自分の変更ファイルだけを明示stageする。
- 他人の未追跡ファイルをstage、削除、renameしない。
- 作業終了前にpushする。
- mainへ直接pushしない。統合担当がmergeする。
- merge conflictで相手の変更を選択的に捨てない。

推奨commit順:

```text
<WP>の受入試験を追加する
<WP>のcore契約を実装する
<WP>をOCCTまたはUIへ接続する
<WP>の診断と失敗回復を実装する
<WP>を完了として記録する
```

## J. 最終報告

次の形式を崩さないでください。

```text
WP:
branch:
base commit:
commits:

実装した契約ID:
通過した受入試験ID:
変更した公開API:
再利用した既存処理:
削除した重複:
Windows check:
CI:
実起動:
画像確認:
出力再読込:
既知の未完:
仕様との差分:
統合時の注意:
```

`既知の未完` または `仕様との差分` がある場合はWPを完了にしません。
検証していない項目を「おそらく動く」と報告しません。

---
