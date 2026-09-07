# Wire-first V2 アーキテクチャとデータ契約

## 1. 目的

この文書は、V2実装のモジュール境界、正本データ、依存再計算、保存形式を固定する。
クラス名の細部は変更できるが、依存方向と不変条件は変更してはならない。

## 2. 最終モジュール構成

V2の正式切替後は次のライブラリ境界にする。移行中は同名の `v2` 名前空間または
`src/next` 配下を使ってよいが、切替時にこの構成へ整理する。

```text
src/core/kachakacha/base/
  UUID、Diagnostic、Result、Cancellation、共通の有限値検査。Qt/OCCT非依存。

src/core/kachakacha/geometry/
  数値、曲線、ワイヤー鎖、交差、許容差。baseだけに依存。

src/core/kachakacha/domain/
  Entity/Featureの純データ定義、参照、製作設定。base/geometryだけに依存。

src/core/kachakacha/document/
  Document、Feature DAG、Command、Undo、検証。domainまでにだけ依存。

src/core/kachakacha/modeling/
  輪郭認識、押し出し要求、形状ガイド要求、ソリッド化要求。Qt/OCCT非依存。

src/core/kachakacha/fabrication/
  製作近似、パネル、折り線、切れ目、型紙、誤差。Qt/OCCT非依存。

src/core/kachakacha/io/
  .kcd文書JSON、SVG、DXF、型紙データ。Qt/OCCT非依存。

src/occt/kachakacha/kernel/
  IGeometryEvaluator実装、B-Rep生成・検査、STEP/STL、形状由来のSubshape対応。

src/apps/cad/
  Qt UI、Viewport、Controller、パネル。幾何計算を持たない。
```

依存方向は次だけを許可する。

```text
base <- geometry <- domain <- document
domain/document <- modeling
domain/document/modeling <- fabrication
domain/document/modeling/fabrication <- io
domain/document/modeling/fabrication <- occt
all public layers <- Qt application
```

- `DOC-001`: coreはQtヘッダとOCCTヘッダをincludeしてはならない。
- `DOC-002`: UIは座標計算、交差、輪郭接続、B-Rep生成を実装してはならない。
- `DOC-003`: OCCT層はUI状態、QWidget、表示名による検索を参照してはならない。
- `DOC-004`: 旧 `Project` をV2の正本として再利用してはならない。
- `DOC-005`: domainは純データだけを持ち、modeling/fabrication serviceまたはOCCTへ依存してはならない。
- `DOC-016`: 製作近似は表示用三角形を入力にせず、OCCT B-Rep cacheを参照する
  `IFabricationGeometryProvider` から境界、開口、UV、曲率標本を受け取る。近似最適化自体はOCCT非依存とする。

## 3. ビルドターゲット

移行中は次を並行してビルドする。

```text
kachakacha_cad          現行版。受入完了まで保守のみ。
kachakacha_v2_core      V2 geometry/document/modeling/fabrication/io。
kachakacha_v2_occt      V2 OCCTアダプタ。
kachakacha_cad_next     V2 Qtアプリ。
kachakacha_v2_tests     V2単体・統合試験群。
```

切替コミットで `kachakacha_cad_next` を `kachakacha_cad` へ改名し、旧ターゲットと旧専用テストを削除する。
移行用アダプタを正式版へ残してはならない。

## 4. IDと名前

### 4.1 ID

次の型を文字列と暗黙変換できない強い型として定義する。

```cpp
struct DocumentId;
struct EntityId;
struct FeatureId;
struct GroupId;
struct SegmentId;
struct PanelId;
struct FoldId;
struct CutId;
struct AssetId;
```

- 保存表現は小文字ハイフン付きUUID v4とする。
- 新規作成時だけ乱数UUIDを生成する。
- 読込、Undo、再計算でIDを作り直してはならない。
- 自動試験は固定UUIDを注入できる `IdGenerator` を使う。
- 空ID、重複ID、型の異なる参照先は読込エラーにする。

### 4.2 表示名

- 表示名は重複可能なUTF-8文字列である。
- 参照、保存、選択、依存関係に表示名を使ってはならない。
- UIでは同名がある場合 `前面 [a1b2]` のようにID末尾4桁を補助表示する。
- 自動命名は日本語の種類名+連番とし、利用者が変更できる。

## 5. Entity

全Entityは共通して次を持つ。

```text
id: EntityId
kind: Point | WorkPlane | Wire | GuideSurface | Part | FabricationModel | Pattern
displayName: string
groupId: GroupId | null
visibility: Visible | Reference | Hidden
editPolicy: Source | Derived | Frozen
createdBy: FeatureId
revision: uint64
```

Entityはidentityと表示metadataだけを正本として持つ。下記の種類別フィールドはFeatureを評価した有効状態であり、
同じ幾何定義をEntityとFeatureへ二重保存してはならない。全ての幾何Entityは必ず1つのFeature outputである。

### 5.1 PointEntity

```text
positionMm: Vector3
sourcePlaneId: EntityId | null
planePolicy: Free3D | ReferenceOnly | LockedToPlane
```

### 5.2 WorkPlaneEntity

```text
originMm: Vector3
uAxis: UnitVector3
vAxis: UnitVector3
normal: UnitVector3
definition: WorkPlaneDefinition
```

基底は右手系の正規直交基底でなければならない。

### 5.3 WireEntity

1つ以上の順序付き曲線Segmentを持つ。連続ポリラインや曲線の組合せを1つのワイヤーとして保持できる。

```text
segments: [CurveSegment]
sourcePlaneId: EntityId | null
planePolicy: Free3D | ReferenceOnly | LockedToPlane
construction: bool
```

`CurveSegment` は `Line`、`CircularArc`、`Circle`、`CubicBezier`、`CubicBSpline` を持つ。
各Segmentは不変 `SegmentId` を持つ。トリムや分割で置換された場合は由来IDを記録する。

### 5.4 GuideSurfaceEntity

```text
definitionFeatureId: FeatureId
parameterDomain: [uMin,uMax,vMin,vMax]
diagnostics: curvature/developability summary
```

B-Rep面そのものを文書の正本として持たない。通常UIでは半透明の `形状ガイド` として扱う。

### 5.5 PartEntity

```text
definitionFeatureId: FeatureId
role: FinishedModel | FabricationPart | Fixture | Imported
manufacturing: ManufacturingProperties | null
evaluation: Valid | Warning | Error | Stale
```

`ManufacturingProperties` は材料名、色、製法、基準板厚、縮尺、注記を持つ。板厚0は未指定を意味せず、
`null` を未指定に使う。

### 5.6 FabricationModelEntity

```text
sourcePartIds: [EntityId]
panels: [FabricationPanel]
folds: [FoldDefinition]
cuts: [CutDefinition]
openings: [OpeningDefinition]
settings: FabricationSettings
errorMetrics: max/rms
assemblyState: masterPercent + perFoldOverrides
```

### 5.7 PatternEntity

```text
sourceFabricationModelId: EntityId
selectedPanelIds: [PanelId]
placements2d: [PanelPlacement]
annotations: [PatternAnnotation]
paperSettings: PaperSettings
```

## 6. 参照の表現

Feature入力は文字列名ではなく次のいずれかで表す。

```text
EntityRef       = EntityId
SegmentRef      = EntityId + SegmentId + parameter range
SubshapeRef     = Part EntityId + SubshapeKey
WireChainRef    = one or more SegmentRef + orientation
```

`SubshapeKey` はFeature由来の意味的キーを使う。

```text
extrude/cap/start
extrude/cap/end
extrude/side/<source SegmentId>
loft/span/<sectionA SegmentId>/<sectionB SegmentId>
boolean/provenance/<source PartId>/<source SubshapeKey>
```

- OCCTの一時的なFace番号を保存してはならない。
- 再計算で意味的キーが消えた参照は `BrokenReference` とする。
- 最寄り面や最寄り点へ無言で付け替えてはならない。
- 修復UIは候補を見せ、利用者が選んだときだけ参照を書き換える。

## 7. Feature DAG

Featureは次の共通形式を持つ。

```text
id: FeatureId
type: string
displayName: string
enabled: bool
definition: type別のcompile-time variant
outputs: [{key, EntityId, EntityKind}]
revision: uint64
```

初回切替までに必要なFeature type:

```text
CreatePoint
CreateWorkPlane
CreateWire
TransformWire
ProjectWire
CreateGuideSurface
Extrude
CreatePartFromWireCage
Boolean
CreateFabricationModel
CreatePattern
FreezeDerived
```

TransformWireのmethodで編集、結合、trim、extend、fillet、chamfer等を区別し、Booleanのoperationで
add/cutを区別する。methodごとに別Feature typeを増やさない。

- `DOC-010`: DAGに循環を許可しない。
- `DOC-011`: Feature出力は作成時にIDを確保し、再計算で同じIDを維持する。
- `DOC-012`: 入力変更時は影響を受ける下流だけをトポロジカル順に再計算する。
- `DOC-013`: 同一入力、同一設定、同一許容差からは同一の幾何結果と順序を返す。
- `DOC-014`: 自動分割数や候補順に乱数、unordered containerの反復順、画面表示順を使わない。
- `DOC-015`: 無効化したFeatureの下流は `SuppressedInput` とし、別入力へ自動接続しない。

## 8. CommandとUndo

文書変更はすべて `DocumentCommand` を通す。

```cpp
struct CommandResult {
    bool committed;
    DocumentDelta delta;
    std::vector<Diagnostic> diagnostics;
};
```

- 直接 `entities` や `features` の配列を書き換えてはならない。
- Commandは候補スナップショットへ変更し、構造検証と必須評価が成功してからcommitする。
- 失敗時は元のDocumentをバイト同等に維持する。
- Undo/Redoはcommit済み `DocumentDelta` を使う。UI状態は別履歴とする。
- 連続ドラッグは開始から終了まで1つのUndo単位にまとめる。
- ファイルを開く、新規作成、保存済み点の更新はUndo履歴の境界として明示する。

## 9. 評価サービス

core側にOCCT非依存のインターフェースを置く。

```cpp
class IGeometryEvaluator {
public:
    virtual EvaluationResult Evaluate(
        const DocumentSnapshot& document,
        FeatureId root,
        const CancellationToken& cancel) = 0;
};
```

`EvaluationResult` は診断、表示用三角形、境界ワイヤー、質量特性、意味的Subshape一覧を返す。
OCCTの `TopoDS_Shape` はOCCT層の世代付きキャッシュだけが所有し、coreへ露出しない。

製作近似では表示用三角形を再利用しない。OCCT層の `IFabricationGeometryProvider` 実装が同じB-Rep cacheから
source patch、厳密な3D境界曲線、面上UV、開口、隣接、適応曲率標本を純データへ変換する。
`fabrication` 層はその純データだけを受けて候補生成、誤差評価、展開、組立を行う。表示tessellation設定を
変えても製作結果のpanel topologyと測定値が変わってはならない。

- 文書変更はUIスレッドだけで行う。
- 100msを超える可能性がある評価は不変Snapshotをworkerへ渡す。
- 新しいrevisionが発生したら古い評価をcancelし、遅れて返った結果を採用しない。
- workerからQWidgetを触らない。
- 例外をUIイベントループ外へ漏らさない。Diagnosticへ変換する。

## 10. Diagnostic

```text
code: 安定した英数字ID
severity: Info | Warning | Error
summaryJa: 1行の理由
detailsJa: 数値と対象を含む説明
entityIds/featureIds/subshapeKeys: 問題箇所
recoveryActions: 実行可能な修正候補
```

同じエラーはどの画面から実行しても同じcodeを返す。日本語文を試験の識別子にしてはならない。
必須codeは `geometry-contract.md` と `fabrication-contract.md` で定義する。

## 11. グループ

- グループは親を1つ持てる木構造とする。
- Entityは0または1グループへ属する。
- Documentは `activeGroupId` を持ち、新規の利用者作成Entityへ自動設定する。
- 派生EntityはFeatureが指定する派生グループへ置き、active groupを使わない。
- グループ状態は `Visible`、`Reference`、`Hidden`。
- `Reference` は表示、スナップ、測定が可能で、選択編集は不可。
- グループ削除時は、中身を親へ移すか同時削除するかを確認し、暗黙削除しない。

## 12. .kcd2保存形式

移行中のV2アプリは拡張子 `.kcd2` を使う。WP-13の正式切替時に、同じZIP+JSON内容のまま拡張子を
`.kcd` へ変更する。旧テキスト `.kcd` との互換や自動読込は行わない。

### 12.1 コンテナ

`.kcd2` は標準ZIPコンテナとする。

```text
document.json                 必須。正本。
geometry/<EntityId>.brep      Imported/Frozen Partだけ。OCCT BREP。
preview/<EntityId>.glb        任意キャッシュ。読込結果へ影響させない。
meta/thumbnail.png            任意。
```

ZIP実装は `libzip` をvcpkgから使用する。BSD-3-Clauseとして `docs/licensing-audit.md` と配布NOTICEへ追加する。
別のZIP依存へ変える場合はADRとライセンス調査が必要である。

### 12.2 JSONルート

```json
{
  "format": "kachakachaCAD",
  "schemaVersion": 2,
  "documentId": "00000000-0000-4000-8000-000000000001",
  "units": {"length": "mm", "angle": "deg"},
  "tolerances": {"linearMm": 0.000001, "angularRad": 0.000000001},
  "metadata": {"title": "", "author": ""},
  "activeGroupId": null,
  "groups": [],
  "entities": [],
  "features": [],
  "rootOrder": [],
  "uiState": {}
}
```

必須規則:

- 数値は有限値だけを許可し、NaNとInfinityを保存しない。
- JSON objectのキー順に意味を持たせない。
- EntityとFeatureの配列順は表示順だけに使い、依存順と解釈しない。
- 未知の必須typeは読込拒否、未知の任意UIフィールドは保持して再保存してよい。
- `schemaVersion` が2より大きい場合は読込拒否し、ファイルを変更しない。
- 旧テキスト `.kcd` を検出した場合は `KCDV2-F001` を表示し、部分読込しない。

### 12.3 保存の安全性

1. 同じディレクトリへ一時ファイルを作る。
2. ZIPを閉じる。
3. その一時ファイルを再度開き、JSON schema、参照整合、必須BREPを検証する。
4. 既存ファイルがある場合は `.bak` へ退避する。
5. OSの置換操作で本ファイルへ切り替える。
6. 失敗時は本ファイルを残し、一時ファイルを削除できなくても警告に留める。

## 13. 依存追加

V2仕様で新たに許可する依存は `libzip` のみとする。JSONは `nlohmann/json` MITを使う。
両方をvcpkg manifestへ固定し、正確な版、リンク形態、ライセンス、対応ソースを
`docs/licensing-audit.md` へ記録する。別の幾何、メッシュ、UIライブラリを追加してはならない。

## 14. 削除対象

V2切替コミットでは、参照がなくなった次を削除する。

- 旧 `Project` と名前参照フィールド
- 旧 `ProjectScript`
- 利用者向け `Surface`、`Plate`、`Body` オブジェクトと専用パネル
- `UsesFacetedPapercraft` など常にfalseを返す暫定スタブ
- 旧展開方式と重複するUI経路
- V2へ移植済みの処理を持つ旧コピー
- 旧形式専用のサンプルとテスト。ただし価値のある幾何ケースはV2 fixtureへ移植する

削除前に `rg` で参照0を確認し、削除と新機能を同一コミットへ混ぜない。
