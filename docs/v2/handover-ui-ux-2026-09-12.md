# WP-14 UI/UX統合改訂 引継ぎ書

## 1. 作業識別

- WP: WP-14
- branch: `codex/v2-ui-ux-overhaul`
- 開始日: 2026-09-12
- 基点: `3b4fdd3`
- 作業ロック: `1ea7c5b`
- 担当: Codex。利用限界時はClaudeへこの文書を渡す。

## 2. オーナーが確定した仕様

次は質問し直さない。

1. 固定の `作図 / 部品 / 製作 / 出力` 4モードは、分かりやすくなるなら廃止してよい。
   WP-14では廃止して統合shellへ移す。
2. 利用者向け `板材` 概念は廃止済みであり、復活させない。
3. ワイヤーをSketchまたは作業平面の所有物にしてはならない。
4. スイープなど一般ソリッドCAD機能は将来課題であり、今回実装しない。
5. 標準操作順は `ツール -> 対象選択`。有効な事前選択からの開始も許す。
6. V1は削除しない。必要機能と実証済みの挙動を調査する資料として使う。

## 3. 規範文書

必ず次の順に読む。

1. `AGENTS.md`
2. `docs/v2/README.md`
3. `docs/v2/pre-implementation-fixes.md`
4. `docs/v2/ui-ux-integrated-spec.md`
5. `docs/adr/0028-integrated-ui-ux-and-tool-session.md`
6. `docs/v2/product-contract.md`
7. `docs/v2/public-api-contract.md` §16
8. `docs/v2/ui-workflows.md`
9. `docs/v2/acceptance-tests.md` §10
10. `docs/v2/acceptance-coverage.md` のAT-UIX行

## 4. なぜ全面改訂するか

現行V2は機能の存在より操作体系が問題になっている。

- モードを変えても以前のツール状態が残る実装があった。
- 選択がEntityId中心で、同じワイヤーの複数線分や端点を保てない。
- 事前選択がないと開始できないコマンドがあり、ツール先行になっていない。
- 上部、右棚、手順棚へ同じコマンドが重複している。
- Shift/Ctrl/Altの意味が選択とスナップで競合している。
- 右パネルが選択プロパティとツール入力に統合されていない。
- mode、tool、pending command、viewportが別々に操作状態を持っている。

見た目だけを直さず、SelectionRefとToolSessionを先に直す。

## 5. 実装境界

### WP-14が変更してよい

- `src/next/kachakacha/app/Selection.*`
- 新規 `src/next/kachakacha/app/ToolSession.*`
- 新規 `src/next/kachakacha/app/InteractionState.*`
- `src/apps/cad_next/V2Viewport.*`
- `src/apps/cad_next/V2MainWindow.*`。ただし膨張させず新しいdock/widget/controllerへ分ける。
- `src/apps/cad_next/V2PendingCommand.*` はToolSession adapterへ置換する。
- UIXに直接対応する `tests_v2/*` とself-test。
- UI規範、受入台帳、HTMLマニュアル。

### WP-14が独断で変更しない

- 製作近似、展開、曲げsolverの数学。
- STEP/STL/SVG/DXF/PDF writer。
- `.kcd2` のFeature保存形式。
- V1 `src/apps/cad` と `src/core`。
- WP-10/12の進捗状態。

必要な公開API変更は規範文書とcompile testを先にcommitする。

## 6. Phase 1の具体設計

### 6.1 SelectionRef

`SelectionSet` をEntityIdの集合から順序付き `SelectionRef` の集合へ移す。

```text
SelectionRef
  entityId
  kind = Object | Vertex | Edge | Face | ControlPoint | WorkPlane
  segmentId?
  subshapeKey?
  curveParameter?
  hitPoint
  screenDistancePx
```

同一Entityでもサブ要素が異なれば別項目として保持する。等価判定は
`entityId + kind + segmentId/subshapeKey` で行い、hitPointは等価判定に使わない。

### 6.2 選択操作

- plain click: replace
- Ctrl+click: toggle
- box left-to-right: contained
- box right-to-left: crossing
- Tab: candidate cycle
- Alt+click: deeper candidate
- Shift: selection変更ではなく作図拘束

既存試験の `Shiftで追加、Altで削除` は新仕様に更新する。

### 6.3 ToolSession

1つのactive sessionだけを持つ。sessionは必要役割、取り込み済みSelectionRef、パラメータ、
Preview世代、診断、確定可否、1段戻れるかを返す。

V2PendingCommandは最初から削除せずadapterにする。各コマンドを順に移し、最後にadapterを削除する。
Document変更は確定時のDocumentCommandだけで行う。

### 6.4 意味状態

Viewportへ `Hovered / Selected / Input / Preview / Warning / Error` を別々に渡す。
Inputを通常Selectionへ混ぜない。PreviewをDocumentへ仮登録しない。

## 7. Phase 2の具体設計

- `UiMode` は保存形式やcoreから先に消さず、画面配置だけをカテゴリへ置換する。
- メニューバーは `ファイル/編集/表示/作図/3D/製作用近似/出力/ツール/ウィンドウ/ヘルプ`。
- 上部は1段。狭い画面では低優先コマンドをoverflowへ移す。
- 右dockは `SelectionProperties` と `ToolProperties` をstackで切り替える。
- 左下の常設操作ガイドとモード別コマンド棚を削除する。
- モデルツリーのグループ、検索、表示、ロックは維持する。
- Windows 95テーマは意味状態と論理寸法を標準テーマと共通にする。

## 8. Phase 3の移行順

次の順で1群ずつcommit、build、ctest、self-test、pushする。

1. 選択、カメラ、表示だけのコマンド。
2. 直線、作図点、ポリライン、円、円弧。
3. トリム、延長、分割、結合、接続、変形。
4. 作業平面、グリッド、測定。
5. 形状ガイドの役割表。
6. 押し出し、閉空間から部品、厚みを付けて部品化。
7. 製作用近似、組立確認、型紙。
8. 出力。

## 9. 最初に追加する試験

1. 同じWireの2 SegmentIdをCtrlで同時保持できる。
2. 同じPartの2 SubshapeKeyを同時保持できる。
3. tool-firstとpreselection-firstで同じDocumentCommandになる。
4. tool切替で古いPreview/Input/parameterが消える。
5. Escを3回使い、段階戻り、session終了、selection解除になる。
6. ShiftがConstraint、SがSnapSuppressionになる。
7. WorkPlane削除後もWire IDと3D座標が不変。
8. 押し出し4出力が入力を削除しない。

## 10. 検証コマンド

Windows本番:

```powershell
.\scripts\check-v2.ps1
.\build-msvc2022-x64\Release\kachakacha_cad_next.exe --self-test
```

変更ごとに `git diff --check` も通す。Qt/OCCTを使う変更はWindows実機で緑になるまで
「検証済み」と書かない。

## 11. 現在の進捗

- Phase 0: 規範統合完了。coverage、command catalogを含むV2の108試験は通過。
- Phase 1: 未着手。
- Phase 2: 未着手。
- Phase 3: 未着手。
- Phase 4: 未着手。

受入台帳ではAT-UIX-001から004、008から011を `部分`、012と013を `未` として再開した。
既存試験を削除して緑にせず、新仕様を検査する内容へ更新する。

Phase 0直後の `scripts/check-v2.ps1` では110件中108件が通過し、`v2_cad_next_smoke` と
それを内包する `v2_package_zip` が失敗した。失敗した自己試験は次の2件で、文書変更によるビルド破損ではない。

1. `使えないコマンドは理由を出す`: 選択不足でdisableする旧前提と、tool-first開始の新仕様が衝突。
2. `モードを変えても選択と文書が変わらない`: 直前実装はモード切替でtoolを戻すが、旧試験名と確認対象が不一致。

Phase 1の最初に期待値を隠して緑にせず、CommandAvailabilityとSetMode/ToolSessionの実挙動を調査し、
新しいAT-UIX-001/002を検査する試験へ置き換える。

## 12. Claudeへ渡す短い指示

```text
kachakachaCAD V2のWP-14 UI/UX統合改訂を継続してください。
branchは codex/v2-ui-ux-overhaul です。最初にAGENTS.mdと
docs/v2/handover-ui-ux-2026-09-12.mdを読み、そこに記載された規範順、所有範囲、
Phase順、試験、push規則を厳守してください。仕様を推測して変更せず、現行コードとV1を調査し、
SelectionRef -> ToolSession -> 画面骨格 -> 個別ツールの順で進めてください。
利用者向け板材を復活させず、WireをSketch/WorkPlaneに所有させず、固定4モードを最終UIに残さず、
tool-firstを標準にしつつ有効な事前選択も同じCommandへ渡してください。
変更ごとにWindows build、ctest、self-testを緑にしてcommit/pushし、
この引継ぎ書の進捗と次作業を更新してください。V1は削除しないでください。
```
