# Wire-first V2 実装パッケージ

## 1. この計画の使い方

実装は下表の単位で行う。担当AIは1回に1パッケージだけを所有する。状態の正本はこの表である。

| ID | 状態 | 依存 | 担当領域 |
| --- | --- | --- | --- |
| WP-00 | 完了(`25d9982`、文書監査、Windows 18試験、self-test) | なし | 規範文書と受入基準の凍結 |
| WP-01 | 完了(`codex/v2-wp01-build-scaffold`、v2試験4本、Windows全試験) | WP-00 | V2 build scaffold、依存、ライセンス |
| WP-02 | 未着手 | WP-01 | ID、Diagnostic、Tolerance、基本値型 |
| WP-03 | 未着手 | WP-02 | Document、Feature DAG、Command、Undo |
| WP-04 | 未着手 | WP-02 | Curve/Wire/Chain、編集、数式 |
| WP-05 | 未着手 | WP-03, WP-04 | `.kcd2` JSON/ZIP保存 |
| WP-06 | 未着手 | WP-03, WP-04 | OCCT evaluator、GuideSurface |
| WP-07 | 未着手 | WP-06 | Part作成、押し出し、Boolean、Subshape |
| WP-08 | 未着手 | WP-03, WP-04 | Qt shell、作図、snap、grid、測定、view |
| WP-09 | 未着手 | WP-06, WP-07 | 製作近似コア、型紙、組立solver |
| WP-10 | 未着手 | WP-08, WP-09 | 部品/製作/出力UI接続 |
| WP-11 | 未着手 | WP-05, WP-07, WP-09 | STL/STEP/SVG/DXF/PDFと保存統合 |
| WP-12 | 未着手 | WP-01..11 | 全受入、マニュアル、配布、切替 |
| WP-13 | 未着手 | WP-12 | 旧実装削除と最終整理 |

状態は `未着手 / 進行中(担当、branch、date) / blocked(理由、試行) / 完了(commit、試験)` のいずれか。
担当開始時と完了時は、この表だけを変更する独立コミットを先にpushする。

## 2. 共通Definition of Done

各WPは次を全て満たすまで完了にしない。

1. 規範文書に対応する実装がある。
2. 対応する受入試験またはそのWPで実行可能な下位試験がある。
3. successだけでなくinvalid/degenerate/cancel/retryを試験する。
4. public APIに表示名参照、Qt型、OCCT型が漏れていない。
5. 例外でアプリまたはtest runner全体が落ちない。
6. 変更した全ファイルをformatし、警告0でbuildする。
7. `scripts/check-v2.ps1` の実行可能範囲が通る。
8. Windows/Qt/OCCT変更はWindows実機またはCIで通る。
9. 仕様との差分が0、または採用済みADRで説明されている。
10. 作業ブランチをpushし、commit hash、試験、未実施項目を報告する。

「後で接続する空ボタン」「常に固定値を返す仮実装」「見た目だけのmesh」「TODOで逃がした必須処理」は
成果物として数えない。

## 3. コーディング規則

### 3.1 新規コードの大きさ

- 新規 `.cpp` は原則1500行以下。
- 新規 public headerは500行以下。
- V2のMainWindow shellは400行以下。
- 1関数100行超は分割する。幾何アルゴリズムで必要な場合は関数冒頭に理由を1行記す。
- `Utils.cpp`、`Helpers.cpp` のような責務不明ファイルを作らない。
- 同じ分岐または変換を3回書かない。
- UI toolは共通 `ToolController` state machineへ登録し、巨大enum比較を増やさない。

### 3.2 禁止

- 表示名でEntityを検索して依存参照する。
- UIクラス内で幾何計算する。
- core public APIへQString/QColor/QPoint/TopoDS_Shapeを入れる。
- preview meshをPartの正本にする。
- 最寄り要素へ壊れた参照を自動付け替えする。
- `catch (...) { return success; }`。
- invalid inputに空Entityを返す。
- 失敗したFeatureの一部outputだけをDocumentへ残す。
- テストを通すために許容差をその場で拡大する。
- unordered containerの順序を保存結果、自動候補順、番号へ使う。
- 仕様外の機能を「ついで」に追加する。
- 他WPが所有するpublic headerを相談なしに変更する。
- 旧実装へV2分岐を追加して一時的な二重経路を恒久化する。

### 3.3 必須

- エラーcodeを安定させ、日本語文とは分離する。
- 許容差を `GeometryTolerance` から受け取る。
- ID generator、clock、filesystem failure、cancelをtestで注入可能にする。
- public API変更にはcompile testまたは直接unit testを付ける。
- expensive operationはCancellationTokenを確認する。
- source revisionとresult revisionを照合する。
- test fixtureへ実数値と由来を記す。
- 1コミット1目的。機械的移動と挙動変更を分ける。

## 4. WP-00 規範文書

### Deliverables

- `docs/v2/README.md`
- `docs/v2/product-contract.md`
- `docs/v2/architecture-and-data.md`
- `docs/v2/public-api-contract.md`
- `docs/v2/kcd2-format.md`
- `docs/v2/geometry-contract.md`
- `docs/v2/fabrication-contract.md`
- `docs/v2/ui-workflows.md`
- `docs/v2/command-catalog.md`
- `docs/v2/acceptance-tests.md`
- `docs/v2/traceability-matrix.md`
- `docs/v2/implementation-work-packages.md`
- `docs/v2/agent-master-prompt.md`
- `docs/v2/integration-lead-prompt.md`
- ADR 0026
- `AGENTS.md` と `CLAUDE.md` からV2入口へのリンク

### 完了確認

- 用語検索で利用者向け `Surface/Plate/Body` の扱いが矛盾しない。
- 全PRD必須領域にAT試験がある。
- WP依存にcycleがない。
- 旧文書よりV2が優先されることが明記される。
- オーナー未確認の重大分岐が残っていない。

## 5. WP-01 Build scaffold

### 所有

- root `CMakeLists.txt`
- 依存取得方法の決定(ADR 0027。`vcpkg.json` は追加しない — 理由はADR参照)
- `scripts/check-v2.ps1`, `scripts/check-v2.sh`
- `.github/workflows/windows-build.yml`(V2ブランチでCIを回し、`kachakacha_cad_next.exe` を配布zipへ入れる)
- V2 targetだけの空でない最小source
- V2試験の土台(`src/next/kachakacha/base/TestHarness.h`。1件失敗で残りを止めない)
- `docs/licensing-audit.md`

### 実装

- `kachakacha_v2_core`、`kachakacha_v2_occt`、`kachakacha_cad_next` を追加。
- 現行targetとtestを壊さない。
- 依存の取り方をADR 0027で決める。`vcpkg.json` を置くとvcpkgがマニフェストモードへ
  切り替わり、Qt/OCCTの解決が壊れるため、初回は追加しない。
- test-only以外の新依存を追加しない。
- `check-v2` はconfigure/build/ctestを行い、test未登録なら失敗する。

### Gate(一次)

- 現行 `check.ps1`。
- `check-v2.ps1`。
- `AT-ARC-001` 依存境界(V2 coreがQt/OCCTをincludeしない)。
- `AT-ARC-005` コード衛生(ファイル1500行・関数100行・未完了マーカー)。
- 試験土台が「1件失敗で残りを停止しない」ことを、土台自身の試験で示す。
- ライセンス表更新。
- next exeが空ウィンドウではなく、versionと「V2準備中」を出す最小shell。これは製品機能完成には数えない。

## 6. WP-02 Foundation

### 所有

- `base/Ids.*`
- `base/Diagnostic.*`
- `geometry/GeometryTolerance.*`
- `geometry/Units.*`
- `geometry/Vector*` の移植/整理
- test ID generator

### API

- strong UUID wrappers。
- parse/format/order/hash。
- Diagnostic value types。
- finite number validation。
- document unit conversion。
- global default tolerance factory。

### Gate

- UUID roundtrip、invalid parse、fixed generator、stable ordering。
- unit dimensional mismatch。
- tolerance model diagonal boundary。
- NaN/Infinity rejection。

## 7. WP-03 Document/DAG/Command

### 所有

- `domain/Entity.*`
- `domain/Feature.*`
- `domain/FabricationSettings.*`
- `document/Document.*`
- `document/FeatureGraph.*`
- `document/DocumentCommand.*`
- `document/UndoStack.*`
- `document/Group.*`

### API契約

- Entity/Featureのadd/update/removeはCommandだけ。
- typed refsとBrokenReference。
- DAG validation、affected subgraph、revision。
- transaction candidate/commit/rollback。
- active groupとderived group。
- immutable `DocumentSnapshot`。

### Gate

- AT-DOC-001から005。
- 100回Undo/Redo。
- randomized DAG property test。seed固定、cycle拒否、順序決定性。

## 8. WP-04 Wire/Chain

### 所有

- `geometry/CurveSegment.*`
- `geometry/Wire3d.*`
- `geometry/WireChain.*`
- `geometry/Intersections.*`
- `modeling/WireCommands.*`
- `io/NumericExpression.*` の移植

### 実装順

1. 値型とevaluation。
2. arc length/closest/split。
3. endpoint graph。
4. deterministic chain。
5. intersection。
6. trim/extend。
7. join/G1/G2。
8. arc constructors。
9. expression。

### Gate

- AT-WIR-001から008。
- 既存Wire testsの同等ケースを移植。
- Polyline化による正本変換0件。
- branch/gap/degenerateのcode固定。

## 9. WP-05 `.kcd2`

### 所有

- `io/KcdArchive.*`
- `io/DocumentJson.*`
- `io/SchemaV2.*`
- save/open tests

### 実装

- ZIP container。
- `document.json`。
- frozen/imported BREP asset index。
- canonical JSON writer。配列表示順は維持、object keyは安定順。
- atomic save、reopen validation、backup。
- old format detection。

### Gate

- AT-EXP-001から004。
- failure injection各段階。
- zip-slip pathを拒否。
- 1GB超、異常圧縮率、重複entryに上限と拒否。
- JSON recursion depthと配列件数に安全上限。

## 10. WP-06 OCCT/GuideSurface

### 所有

- `kernel/OcctGeometryEvaluator.*`
- `kernel/OcctCurveConversion.*`
- `kernel/OcctGuideSurface.*`
- `modeling/GuideSurfaceFeature.*`
- kernel cache

### 実装

- core曲線とOCCT edge/wireの双方向変換。
- Planar/Ruled/Loft/Guided/Gordon/BoundaryFill/OffsetGuide。
- input curve deviation validation。
- cancellation checkpoints。
- cache key = document revision + feature revision + tolerance。
- TopoDS objectをcoreへ返さない。

### Gate

- AT-GEO-001から007。
- invalid inputsでOCCT例外をDiagnostic化。
- 同じ入力のSubshapeKey順が同じ。
- leak/handle lifetime smoke。

## 11. WP-07 Part/Extrude/Boolean

### 所有

- `modeling/PartFeature.*`
- `modeling/ExtrudeFeature.*`
- `modeling/BooleanFeature.*`
- `kernel/OcctPartBuilder.*`
- `kernel/SubshapeNaming.*`
- `kernel/PartValidation.*`

### 実装順

1. profile faceとholes。
2. basic extrude。
3. semantic subshape naming。
4. wire cage planar shell。
5. GuideSurface trim patches。
6. shell sew/solid validation。
7. ToTarget。
8. add/cut。
9. disconnected result preview。

### Gate

- AT-GEO-010から013。
- AT-EXT-001から008。
- BRepCheck valid、volume、connectedness。
- invalid shapeをDocumentへcommitしない。
- STL/STEPはWP-11まで仮出力しない。

## 12. WP-08 Qt shell/作図/View

### 所有

- `src/apps/cad_next/` のshell、viewport、selection、tool controller、作図panel、測定window、theme。
- core変更は禁止。必要APIは統合担当へ依頼する。

### 構成

```text
MainShell
ModeBar
ModelTree
PropertyPanelHost
OperationGuide
ViewportWidget
SelectionController
SnapController
ViewController
ToolControllerRegistry
CursorNumericInput
DrawingPanel
MeasurementWindow
DisplaySettings
Win95Theme
```

各toolは `IToolController` を実装し、mouse/key eventを巨大switchへ集約しない。

### Gate

- AT-UIX-001から006、008から011の作図部分。
- AT-MEA-001から005のUI表示と作図点作成。
- `command-catalog.md` 8章の完全性ゲート。
- 100/125/150/200%DPI画像。
- View cube quaternion test。
- Shift snap suppression。
- app起動と通常終了。

## 13. WP-09 Fabrication core

### 所有

- `fabrication/SurfaceAnalysis.*`
- `fabrication/PanelCandidate.*`
- `fabrication/PanelOptimizer.*`
- `fabrication/ReliefCut.*`
- `fabrication/OpeningMapping.*`
- `fabrication/Develop.*`
- `fabrication/AssemblySolver.*`
- `fabrication/FoldState.*`
- `kernel/OcctFabricationGeometryProvider.*`

### 実装順

1. Planar/Cylindrical/Conical解析解。
2. B-Rep cacheからpure fabrication inputを作るprovider。
3. panel metrics。
4. deterministic candidate generation。
5. lexicographic optimizer。
6. manual roles。
7. opening clipping/junction。
8. Straight/V/CurvedV。
9. pattern mapping。
10. hinge/developable assembly。
11. closed loop solver。
12. arbitrary state Wire/Part request。

### Gate

- AT-FAB-001から012、014。
- AT-ARC-006。
- 各候補の数値metrics。
- 0/30/100%寸法不変。
- triangle interpolation実装が存在しないことをreview。
- ER1 fixtureを実行可能なところまで用意。最終判定はWP-12。

## 14. WP-10 Model/Fabrication UI

### 所有

- Part mode panel/controllers。
- GuideSurface role table。
- Fabrication panel/controllers。
- Pattern view。
- Output selection UI shell。

### Gate

- AT-UIX-002、007、009。
- 初心者シナリオ1から7を自動操作。
- operation guide全コマンド。
- invalid previewから修正して同じcommandを再実行可能。
- 部材選択と3D highlightの一致。

## 15. WP-11 出力統合

### 所有

- `kernel/PartExport.*`
- `io/PatternSvg.*`
- `io/PatternDxf.*`
- Qt PDF adapter
- output validation/controller

### 実装

- 同一B-RepからSTEP/STL。
- arbitrary fold state。
- selected Parts/Panels。
- SVG/DXF native curves。
- 1:1 tiled PDF。
- temp write/validate/replace。

### Gate

- AT-EXP-010から013。
- AT-FAB-011、012、014。
- 出力物を別readerで再読込検査。
- 0byte/partial fileを残さない。

## 16. WP-12 全受入と切替準備

### 所有

- cross-package testだけ。
- V2 manual。
- sample `.kcd2`。
- screenshot automation。
- distribution packaging。
- CI workflowのV2 gate。

### Gate

- `acceptance-tests.md` 全項目。
- AT-FAB-013 ER1/ER2。
- 画像付き全機能manual。
- 配布zipから起動。
- 実際にexeを起動してsampleを開き、30% STEPとPDFを作る。
- blocker 0。warningは一覧と理由をownerへ提示。

このWPでは必須機能を新規実装しない。欠損が見つかったら所有WPへ戻す。

## 17. WP-13 旧実装削除

### 前提

- WP-12完了。
- ownerがV2画面を比較確認。
- CI green。
- release branch/tag方針確定。

### 削除手順

1. next exeを正式名へ。
2. `.kcd2` 拡張子を同一ZIP+JSON内容の正式 `.kcd` へ切り替える。
3. 旧Project/ProjectScript専用経路を削除。
4. Surface/Plate/Body利用者UIを削除。
5. 旧PartModel/FlatPatternの重複経路を削除。
6. 旧sample/testをV2へ移植または削除。
7. CMake targetとscriptを整理。
8. docsの旧仕様に `V2で置換` bannerを追加。
9. `rg` で旧type名、always-false stub、名前参照を検査。
10. full check、self-test、配布zip、実起動。
11. mainへmerge。

旧削除とV2機能追加を同一commitにしない。

## 18. 複数AIの統合規則

### 18.1 統合担当

1人だけが次を所有する。

- この進捗表。
- root CMake/vcpkgの競合解消。
- public contract headerの最終承認。
- cross-WP API変更。
- merge順とrelease branch。

### 18.2 作業担当

- 最新integration branchから `codex/v2-<wp>-<short-name>` を作る。
- 開始ロックを先にpushする。
- 所有ファイル外を変更しない。
- 他WP APIが必要なら、必要signature、理由、試験を統合担当へ提出する。
- 他担当の未完成コードをcopyして独自版を作らない。
- 作業終了時にbranchをpushする。
- merge後に自分のbranch上だけで続けない。最新integrationへ追従する。

### 18.3 競合予防

- 公開headerは所有WPだけが編集する。
- root CMakeへ各担当が直接sourceを追記せず、WP別 `sources.cmake` をincludeする。
- Diagnostic code registryを1ファイルに集約し、重複をtestする。
- test fixture UUID registryを1ファイルに集約する。
- UI文言はtranslation/catalogへ集約し、同じ操作名を各panelへ直書きしない。
- tolerance、unit、ID、selection ref、output targetを独自定義しない。
- 一時adapterには `V2_MIGRATION_ONLY` と削除WPをコメントし、WP-13 testで残存を検出する。

## 19. 引継ぎ報告形式

```text
WP:
branch:
base commit:
commits:

実装した契約ID:
変更した公開API:
変更ファイル:
追加試験:
実行結果:
Windows/CI:
画面確認:
既知の未完:
仕様との差分:
次担当が必要な情報:
```

`既知の未完: なし` と書けない場合、WP状態を完了にしない。
