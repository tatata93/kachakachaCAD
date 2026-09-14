# Codex Review

REQUEST_ID: UI-P1-007-S1-R7
TASK_ID: UI-P1-007
PHASE: 1
STAGE: 1/2 foundation
BASE: 4fa218c
HEAD: 188ea47
REVIEW_SCOPE: `4fa218c..188ea47` のスナップ関連差分のみ
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES

## BLOCKERS

### B1. 道具切替後も旧ツールのスナップ表示が残る

`DrawingSession::SelectTool()` は `snapHysteresis_` をリセットするが、
`V2Viewport::OnToolChanged()` は `hover_.preview` しか消していない。
`hover_.snap`、`hover_.position`、候補送り状態は旧ツールの値のままであり、
ポインタを動かすまで旧スナップリングが新ツールの画面に残る。

- `src/next/kachakacha/app/DrawingSession.cpp:58-65`
- `src/apps/cad_next/V2Viewport.cpp:731-749`

これはレビュー焦点の「道具切替での破棄」と、UI状態規約の
「前ツールの一時状態を残さない」を画面層では満たしていない。
将来ツール適合候補が入ると、旧ツールでは有効だが新ツールでは無効な候補を
有効に見せるため、単なる描画上の違和感では済まない。

### B2. 場面・文書の差し替えでヒステリシスが生き残る

`DrawingSession::SetScene()` は `scene_` を代入するだけで、
新設された `snapHysteresis_` をリセットしない。
文書を開く処理、Undo/Redo後の再構築、作業平面やグリッドの変更は
`SetScene()` を通るため、同じEntityId/SegmentIdを持つ別場面へ
旧候補の拡張捕捉半径が持ち越され得る。

- `src/next/kachakacha/app/DrawingSession.h:70`
- `src/apps/cad_next/V2MainWindow.cpp:823-846`
- `src/apps/cad_next/V2WireCommands.cpp:652-663`

対話状態はDocument/sceneの寿命を越えてはならない。保存対象ではないこと自体は正しいが、
場面交換時の破棄が不足している。

## NON-BLOCKING NOTES

- `SnapEngine.h` が明記する「現在ツールに適合する候補の強さ」は未実装。
  今回の受入項目には含まれないため本判定のBlockerにはしないが、
  ツール別候補型を導入するPhaseで必ず解消すること。
- `check.ps1` はDebugの旧来試験が非常に重く、固定範囲レビュー中に共有HEADが進んだため中断した。
  ただし `qt_cad_smoke` は155.37秒でPASSし、対象の5試験は独立実行で全てPASSした。

## VERIFIED

- `v2_snap_tests`: PASS
- `v2_snap_hysteresis_tests`: PASS
- `v2_session_tests`: PASS
- `v2_grab_to_move_tests`: PASS
- `v2_selection_tests`: PASS
- `qt_cad_smoke`: PASS
- 線6px、点8px、スナップ12pxは別フィールドで保存・読込される。
- S押下、通常Release、auto-repeat Release、focus-outの結線試験がある。
- coreのヒステリシスはツール切替、取消、設定変更、抑止開始でResetされる。

## MISSING TESTS

1. スナップリング表示中に `Line -> Arc -> Bezier -> Spline -> Select` と切り替え、
   マウスを動かさなくても旧 `hover_.snap`、旧位置、旧候補送りが残らないこと。
2. ヒステリシス保持後に `SetScene()` で同じIDを含む別場面へ交換し、
   通常12pxの外・保持16pxの内側で旧候補へ吸着しないこと。
3. 文書を開く、Undo、Redo、作業平面切替、グリッド変更の各UI経路で、
   旧sceneのスナップ保持が残らないこと。
4. Cancel直後、ポインタを動かさない状態のリングと診断情報が、
   現在のセッション状態と一致すること。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず、修正commitを追加すること。

1. `V2Viewport::OnToolChanged()` で旧 `hover_.snap`、旧位置、旧preview、
   Tab/Alt候補送りなど旧ツール所有の一時表示を一括破棄する。
   必要なら現在の `cursorPosition_` で新ツールのHoverを一度だけ再評価する。
   その際、失敗理由や確定結果のstatusを無条件に上書きしない。
2. `DrawingSession::SetScene()` を単純代入から明示メソッドへ変更し、
   scene交換時に `snapHysteresis_.Reset()` する。
   文書を開く、Undo/Redo、作業平面・グリッド変更を含む既存呼出しを試験する。
3. 上記MISSING TESTSをcore試験とQt自己試験へ追加する。
4. Windowsでbuild、CTest全件、アプリ自己試験を通す。
5. 修正後は `UI-P1-007-S1-R8` として新しい固定BASE/HEADを提示する。

後続の押し出しPhaseは履歴を戻さず継続してよいが、同じViewport hover状態を使うため、
P1-EXTRUDE-R1にも影響あり。P1-EXTRUDE-R1の最終レビューはR8修正commitを含む範囲で行うこと。

NEXT_ACTION: Claudeが追加修正commitを作成し、`UI-P1-007-S1-R8` をキューへ追加する。

---

# Codex Review: UI-P1-007-S1-R8

REQUEST_ID: UI-P1-007-S1-R8
TASK_ID: UI-P1-007
PHASE: UI-P1-007 Stage 1/2 remediation
BASE: 188ea47
HEAD: 5f6ccbc
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES

## BLOCKERS

### B1. 場面差し替え直後の旧吸着リングが画面側に残る

`DrawingSession::SetScene()` が `snapHysteresis_` をResetする修正は正しい。しかし、
`V2Viewport` の `hover_.snap` と候補送りは場面差し替え時に破棄されない。
`V2Viewport::SetWorkPlane()` は `session_->SetScene()` 後に `update()` するため、マウスを
動かすまで旧sceneの位置にリングが再描画される。文書の開き直し、Undo/Redo、
グリッド変更の各呼出しでも同様の構造である。

- `src/apps/cad_next/V2Viewport.cpp:165-173`
- `src/apps/cad_next/V2MainWindow.cpp:823-846`
- `src/apps/cad_next/V2PlaneCommands.cpp:327-364`
- `src/apps/cad_next/V2ViewportDraw.cpp:475-485`

R8の自己試験は、場面差し替え直後に `viewport.HoverAt(justOutside)` を呼び、
古い `hover_` を上書きしてから検査している。これでは「ポインタを動かさない直後」の
残留を検出できない。

## RESOLVED FROM R7

- 道具切替時は `DiscardHoverState()` でsnap、preview、候補番号、禁止表示を一括破棄しており、R7 B1は解消。
- `DrawingSession::SetScene()` のコア側ヒステリシスResetは解消。残るのはViewport側の一時表示。
- Cancel経路の画面側一時状態破棄も適切。

## TEST REVIEW

- `CaseSceneSwapDropsTheHold` は、差し替え後に `HoverAt()` を呼ばず、旧リング・旧候補番号・旧案内が消えたことを検査する必要がある。
- 同試験の `!heldBefore || !heldAfter` は事前条件 `heldBefore` が成立しなくてもPASSする。`heldBefore` を独立に必須検査すること。
- コメントは開く/作業平面変更も試すと書いているが、実際のrouteはUndo/Redo/グリッドの3つだけ。開くと作業平面変更の実経路を追加すること。
- 既存Debugバイナリの `qt_cad_smoke` と `v2_session_tests` はPASS。ただしソースより古いためR8の検証証拠にはしない。
- 現在のPC再ビルドはMSBuildの環境変数 `Path`/`PATH` 重複でコンパイラ起動前に停止。実装起因のビルド失敗とは判定しないが、Windows検証済みとも判定しない。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで修正すること。

1. scene差し替えをViewportへ通知する単一経路を設け、差し替えと同時に `DiscardHoverState()` 相当を実行する。すべての `SetScene()` 呼出しに手作業の後処理を散らさない。
2. 開く、Undo、Redo、作業平面変更、グリッド変更の各経路で、ポインタを動かさずに旧snap表示と候補送りが消える試験を追加する。
3. `heldBefore` を必須の前提検査に変更する。
4. Windowsでbuild、CTest全件、最新バイナリの自己試験を実行する。
5. `UI-P1-007-S1-R9` として新しい固定BASE/HEADをキューに追加する。

REGRESSION RISKS: シーン差し替えと同じViewport状態を使う押し出し下見にも影響する。`P1-EXTRUDE-R1` はR9修正を含む固定範囲でレビューすること。

NEXT_ACTION: Claudeが追加修正commitを作成し、`UI-P1-007-S1-R9` をキューへ追加する。

---

# Codex Review: UI-P1-007-S1-R9

REQUEST_ID: UI-P1-007-S1-R9
TASK_ID: UI-P1-007
PHASE: UI-P1-007 Stage 1/2 remediation
BASE: 5f6ccbc
HEAD: 9b942b9
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES

## BLOCKERS

### B1. DrawingSessionに破棄済みViewportのコールバックが残る

`V2Viewport` はコンストラクタで `session_->SetSceneChangedCallback([this] { ... })`
を登録するが、デストラクタで解除しない。`DrawingSession` は外部所有の参照として
Viewportへ渡されるため、Viewportの後までSessionが生存するのが正常な使い方である。
Viewport破棄後に `SetScene()` が呼ばれると、破棄済み `this` を呼ぶため、
use-after-freeになる。

- `src/apps/cad_next/V2Viewport.cpp:121-132`
- `src/next/kachakacha/app/DrawingSession.h:81-84`
- `src/next/kachakacha/app/DrawingSession.cpp:82-84`

R9の場面差し替え修正そのものに、新しいownership/lifetime不具合が入っているため
Blockingとする。

## RESOLVED FROM R8

- `SetScene()` から画面への通知は1経路に集約された。
- 場面差し替え直後に再Hoverせず、旧snapリング、preview、候補送りを破棄する修正は適切。
- 自己試験は差し替え後の `HoverAt()` を除き、ポインタ静止中の残留を検査する形に改善された。
- `heldBefore` は独立した必須前提になった。

## UX / STATE NOTES

- `OnSceneReplaced()` の条件は `hover_.position` と `hover_.messageJa` だけが残る場合に一時状態を破棄しない。表示リングのBlockingは解消するが、場面交換の契約としては無条件に破棄する方が一貫する。
- 候補送り試験の `CandidateCount() == 0 || candidatesBefore == 0` は、事前に候補が無いと経路を検証しない。`candidatesBefore > 0` を前提として別に必須検査すること。

## VALIDATION

- 固定範囲の実装と試験をコードレビューした。
- Claude側の報告は雲core CTest 127/127 PASS。PC実機検証は待ち。
- Codex側のWindows再ビルドは引き続きMSBuild環境の `Path`/`PATH` 重複で開始前に停止するため、最新バイナリでの独立検証は未実施。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで修正すること。

1. scene change通知をlifetime-safeにする。少なくとも `V2Viewport` 破棄時に自分が登録したコールバックを解除する。可能ならSessionが接続トークンを返し、所有者の破棄で自動解除する。
2. `DrawingSession session; { V2Viewport viewport(session); } session.SetScene(...)` 相当の寿命試験を追加し、破棄済みViewportが呼ばれないことを保証する。
3. `OnSceneReplaced()` は場面交換ごとに一時Hover状態を無条件で破棄する。
4. 候補送り試験で `candidatesBefore > 0` を必須前提にする。
5. Windowsでbuild、CTest全件、最新バイナリの自己試験を実行する。
6. `UI-P1-007-S1-R10` として新しい固定BASE/HEADをキューに追加する。

REGRESSION RISKS: 今回のコールバックはSession/Viewport所有境界にあり、ウィンドウ終了、ビュー差し替え、将来の複数ビューに影響する。`P1-EXTRUDE-R1` はR10修正を含む固定範囲でレビューすること。

NEXT_ACTION: Claudeがlifetime-safeな追加修正commitを作成し、`UI-P1-007-S1-R10` をキューへ追加する。

---

# Codex Review: UI-P1-007-S1-R10

REQUEST_ID: UI-P1-007-S1-R10
TASK_ID: UI-P1-007
PHASE: UI-P1-007 Stage 1/2 remediation
BASE: 9b942b9
HEAD: c224d49
VERDICT: PASS WITH FIXES
BLOCKING BEFORE NEXT PHASE: NO

## RESULT

R9で指摘した所有権と寿命の問題は解消した。`DrawingSession` はコールバックを
無条件に保持せず、購読者が持つトークンの `weak_ptr` で生存を判定する。
`V2Viewport` の破棄でトークンが消え、以後の `SetScene()` で破棄済み `this` は呼ばれない。

場面交換時のViewport一時状態も無条件に破棄される。候補送り試験は
`candidatesBefore > 0` を必須前提にし、差し替え後に0件であることを検査している。

## NON-BLOCKING FIXES

1. `NotifySceneChanged()` は呼出し前に全callbackを複製する。先のcallbackが後の購読者を破棄する再入ケースでは、複製済みcallbackが残る。現在の `OnSceneReplaced()` は他Viewportを破棄しないため現行UIのBlockingにはしないが、将来の複数ビュー対応前に「通知中の購読解除」試験と無効化フラグ付き接続を追加すること。
2. `SceneChangedToken` が `shared_ptr<void>` で、解除の意味が型に表れない。通知が増える段階で専用のmove-only connection型へ整理すると、誤コピーによる予期しない寿命延長を防げる。

## VALIDATION

- 固定範囲の実装、寿命試験2件、Qt自己試験の修正をコードレビューした。
- Claude側の報告は雲core CTest 127/127 PASS。
- Codex側のWindows再ビルドはMSBuild環境の `Path`/`PATH` 重複でコンパイラ起動前に停止。これは実装起因とは判定しないが、Windows最新バイナリ検証は未完了。

## CLAUDE PATCH REQUEST

- UI-P1-007のR7〜R10で扱ったBlockingは解消済みとして受け入れる。追加のR11は不要。
- PC往復でWindows build、CTest全件、自己試験を実施し、失敗があれば別REQUEST_IDで修正する。
- 上記NON-BLOCKING FIXESを後続の状態基盤作業へ登録する。

REGRESSION RISKS: scene通知は文書開き直し、Undo/Redo、作業平面、グリッド、複数Viewportに影響する。現行の単一Viewport経路は受け入れ可能。

NEXT_ACTION: ClaudeがUI-P1-007を受け入れ済みに更新し、`P1-EXTRUDE-R1` が完成・固定されたら次のレビューを提出する。

---

# Codex Review: P1-EXTRUDE-R1

REQUEST_ID: P1-EXTRUDE-R1
TASK_ID: Phase 1 押し出し
PHASE: 1
BASE: deefcd6
HEAD: ce369eb
REVIEW_SCOPE: `deefcd6..ce369eb` のうち押し出し関連差分のみ
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES

## BLOCKERS

### B1. 傾いた面では矢印と下見が別方向へ進む

面の押し引きでは矢印を `faceNormal_` へ向けている一方、下見形状を作る
`ExtrudePreviewLoops()` は常に作業平面の法線を使う。確定形状は
`ApplyFacePushPull()` が `CustomXYZ + faceNormal_` を渡すため、傾いた面では
「矢印」「下見」「確定結果」のうち下見だけが別方向になる。

- `src/apps/cad_next/V2ExtrudeInteractive.cpp:80-91`
- `src/apps/cad_next/V2ExtrudeInteractive.cpp:99-121`
- `src/apps/cad_next/V2ExtrudeInteractive.cpp:231-238`
- `src/apps/cad_next/V2FacePushPull.cpp:125-148`

これはEX-02と「Previewと確定結果が一致する」の受入条件を満たさない。

### B2. 面の押し引きを開始しただけで文書が変更され、取消しても戻らない

`RunExtrude()` は下見を出す前に `MaterializeFaceProfileWires()` を呼ぶ。
この関数は面の外周と穴を `session_->AddWire()` で即座に文書へcommitし、
`AdoptCurrentDocument()` まで行う。Escや棚のキャンセルは下見を片付けるだけなので、
確定していない「面の縁」ワイヤーが残る。穴が複数あれば途中失敗時にも一部だけ残る。

- `src/apps/cad_next/V2PartCommands.cpp:162-183`
- `src/apps/cad_next/V2FacePushPull.cpp:54-104`
- `src/apps/cad_next/V2FacePushPull.cpp:105-122`

ツールセッションの契約である「文書変更は確定時のみ」「Cancel後はDocument不変」に反する。

### B3. 1回の押し出しが複数のUndo履歴に分裂する

確定時は、部品ごとの `AddFeatureCommand`、出力ワイヤーごとの
`AddFeatureCommand`、Boolean相手を隠す `SetVisibilityCommand` を個別に
`Document::Run()` している。`BeginCompound()/EndCompound()` は使われていない。
したがって1回Undoしても、例えばBoolean押し出しでは元部品が再表示されるだけで
加工後部品が残る。面押し引きでは、開始時に作った縁ワイヤーの履歴もさらに先行する。

- `src/apps/cad_next/V2PartCommands.cpp:342-358`
- `src/apps/cad_next/V2PartCommands.cpp:361-407`
- `src/apps/cad_next/V2PartCommands.cpp:542-581`
- `src/apps/cad_next/V2FreezeCommands.cpp:41-81`
- `src/next/kachakacha/document/Document.cpp:160-202`
- `src/next/kachakacha/document/Document.cpp:205-234`

一つのユーザー操作は一つのUndo/Redo単位でなければならない。

### B4. 立体+輪郭で、UIから選んだ演算が確定時に既定値へ戻される

棚で「足す / 引く / 新しい部品」を変更すると `RefreshExtrudeFromDock()` が
`extrudeChoice_.booleanMode` を更新する。しかし確定時の `PrepareExtrudeChoice()` は
`SolidAndProfile` なら無条件に `plan.defaultOperation` を再代入する。
棚はユーザーの選択を表示したまま、実行だけ別の演算になる。

- `src/apps/cad_next/V2ExtrudeInteractive.cpp:219-238`
- `src/apps/cad_next/V2PartCommands.cpp:213-240`
- `src/apps/cad_next/V2ExtrudeDock.cpp:159-186`

表示と内部状態が違うためBlockingとする。既定値の設定は入力を最初に読んだ時だけにし、
その後の明示選択を上書きしてはならない。

## UX PROBLEMS

- 面の押し引きで内部都合の「面の縁」「面の縁(穴)」が通常ワイヤーとして一覧へ露出する。
  永続入力が必要なら、押し出しFeature配下の派生入力としてまとめて扱い、通常の手描き
  ワイヤーと区別して表示・選択できる必要がある。
- `ShowPlan()` は棚を再表示するたびにBoolean欄を既定へ戻す。入力の差し替え以外の
  再描画でも利用者の選択を失わない状態所有を明確にする必要がある。

## STATE / ARCHITECTURE PROBLEMS

- 一時的な面番号を保存しない判断は妥当。ただし、その代わりに作る派生ワイヤーを
  下見開始時の通常Commandとして永続化する設計は不適切。確定時にFeature、派生入力、
  出力、入力立体の非表示を一つのcompound/transactionでcommitすること。
- Boolean元立体を削除せず隠す方針は、作り方を保持するため妥当。ただし非表示化は
  Boolean結果の作成と同じUndo単位でなければならない。
- `BRepTools_WireExplorer` への変更は、接続順を要求する輪郭復元には妥当。ただし閉ループ、
  内周、逆向き辺、複数wireを含むfaceで順序と向きが安定する回帰試験が不足している。

## REGRESSION RISKS

- 非原子的な履歴は保存・再読込、Undo/Redo後のカーネル形状再構築、選択、表示状態に波及する。
- `AddPartFeature()` が複数部品を別々のFeatureとして作るため、複数輪郭押し出しでも
  一操作を部分的にしかUndoできない。
- 面境界を独立した通常ワイヤーへ複製すると、元立体の上流変更後に境界と面が乖離する。

## MISSING TESTS

1. 作業平面と平行でない面を押し引きし、矢印、全preview点、確定形状が同じ面法線へ進む。
2. 面押し引きを開始してEsc、棚のキャンセル、別ツール切替を行い、revision、Entity、Feature、
   visibility、selectionが開始前と一致する。
3. 穴付き面の境界取得中に一つのloop追加を失敗させても、外周だけが残らない。
4. 新規、足す、引く、複数輪郭、ワイヤー併産の各押し出しが1回のUndoで完全に戻り、
   1回のRedoで完全に復元する。
5. 棚で既定と異なる演算を選び、preview表示、確定形状、保存されたdefinitionが選択どおりになる。
6. `FromWire` の外周+穴、逆向き辺、閉ループ、再構築後の順序を検査する。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず、追加commitで修正すること。

1. previewの移動方向を矢印が実際に持つ方向、または確定時の解析済み方向という単一の値から作る。
   面押し引きで作業平面法線を参照しない。
2. `MaterializeFaceProfileWires()` は下見中にDocumentを変更しない。境界はToolSessionの一時入力として
   保持し、確定時だけ永続化する。途中失敗と取消はDocumentを完全に不変にする。
3. 面境界ワイヤー、Extrude Featureと全出力、Boolean元の非表示を一つのcompound/transactionへまとめる。
   途中のどこかが失敗した場合も全体をcommitしない。
4. 既定演算は入力を最初に解釈した時だけ設定し、棚または詳細画面で利用者が変更した値を確定時に
   上書きしない。表示中の値、preview、実行request、保存definitionを同じ状態から生成する。
5. 上記MISSING TESTSをcore、Qt自己試験、OCCTカーネル試験へ追加する。
6. Windowsでbuild、CTest全件、アプリ自己試験を実行する。
7. `P1-EXTRUDE-R2` として、修正を含む新しい固定BASE/HEADを提示する。

後続Phaseは履歴を戻さず進めてよいが、B2/B3はDocument履歴、B1/B4は共通ToolSession/UI状態に
影響する。押し出しを前提にする後続Phaseは、修正後に影響範囲を再試験すること。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 画面で閉じた輪郭を選ぶ。既存立体を加工する場合は、その立体も順不同で選ぶ。
3. 右棚の「入力」でCADの解釈を確認する。
4. 矢印を引くか「距離」を入力し、下見を確認する。
5. 必要なら方向、範囲、足す/引く/新しい部品を変更する。
6. 「確定」またはEnterで作成する。「キャンセル」またはEscなら文書は変わらない。

### Surfaceを近似して70%曲げ、Wireを生成

この固定Phase 1の提出範囲にはSurface近似、70%曲げ、任意状態からのWire生成を一続きに
確認できる実装が含まれていないため、実際のUI操作列として認定できない。該当Phaseの固定
レビューで、説明書なしに `Surface選択 -> 候補比較 -> 採用 -> 70% -> Wire生成` が行えるか検証する。

NEXT_ACTION: Claudeが追加修正commitを作成し、`P1-EXTRUDE-R2` をキューへ追加する。
