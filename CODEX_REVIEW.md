# Codex Review

## 履歴保持規則

- このファイルはREQUEST_ID単位の追記専用レビュー履歴とする。
- 過去のFAIL/PASSを上書き、置換、削除しない。修正版はR2、R3のような新しいREQUEST_IDで末尾へ追加する。
- Claude側の `AI_HANDOFF_STATE.md` に処理済みと記録されるまでは、該当レビューを未処理成果として保持する。
- 各レビューには最低限 `REQUEST_ID`、`PHASE`、`BASE`、`HEAD`、`VERDICT`、
  `BLOCKING BEFORE NEXT PHASE`、`CLAUDE PATCH REQUEST`、`NEXT_ACTION`、`REVIEWED_AT` を残す。

REQUEST_ID: UI-P1-007-S1-R7
TASK_ID: UI-P1-007
PHASE: 1
STAGE: 1/2 foundation
BASE: 4fa218c
HEAD: 188ea47
REVIEW_SCOPE: `4fa218c..188ea47` のスナップ関連差分のみ
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T00:00:00+09:00

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
REVIEWED_AT: 2026-09-14T00:00:00+09:00

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
REVIEWED_AT: 2026-09-14T00:00:00+09:00

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
REVIEWED_AT: 2026-09-14T00:00:00+09:00

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
REVIEWED_AT: 2026-09-14T00:00:00+09:00

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

---

# Codex Review: P1-EXTRUDE-R2

REQUEST_ID: P1-EXTRUDE-R2
TASK_ID: Phase 1 押し出し
PHASE: 1
BASE: ce369eb
HEAD: 253e446
REVIEW_SCOPE: `ce369eb..253e446` のうち押し出し関連差分のみ
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T14:55:00+09:00

## RESULT

R1のB1、B2、B4は解消した。矢印・下見・確定は同じ解析済み方向を使い、面境界は
下見中の一時データになり、利用者が選んだBoolean演算も確定時に保持される。正常終了時を
compoundへまとめた方向性も正しい。ただし、失敗時の原子性とFeature依存関係にBlockingが残る。

## BLOCKERS

### B1. 失敗時の疑似rollbackが無関係な直前操作をUndoし得る

面境界ワイヤーの最初の追加が失敗すると、`EndCompound()` は変更がないため履歴を追加しない。
その直後の無条件な `Undo()` は、押し出しより前に利用者が行った別操作を取り消す。
途中まで追加できた場合も「一度commitしてUndoする」方式で、失敗を原子的に破棄していない。

- `src/apps/cad_next/V2PartCommands.cpp:335-385`
- `src/next/kachakacha/document/Document.cpp:205-234`

さらに `AdoptExtrudeResult()` は結果を返さず、Feature/ワイヤー追加の個別失敗を無視する。
Boolean元の非表示化も結果を検査しないため、途中失敗した部分状態を正常終了としてcompoundへ
確定できる。Documentに `AbortCompound()` 相当またはRAII transactionを設け、開始前snapshotへ
履歴を増やさず戻す必要がある。全追加と表示変更の成否を伝播し、一件でも失敗したら全体を破棄すること。

### B2. 面境界ワイヤーが押し出しFeatureの依存関係へ登録されない

面押し引きではdefinitionの `profiles` に確定時生成した `faceWires` を保存する一方、
`AddPartFeature()` の `inputEntityIds` はViewportの元選択を読み直す。そのため再構築が読むUUIDと、
Documentの評価順・dirty伝播・削除cascade・壊れた参照判定が読むUUIDが一致しない。

- `src/apps/cad_next/V2PartCommands.cpp:350-376`
- `src/apps/cad_next/V2PartCommands.cpp:590-605`
- `src/apps/cad_next/V2RebuildCommands.cpp:43-53`

面境界ワイヤーを編集または削除しても押し出しが再評価されず、保存後の再構築順も保証されない。
`AddPartFeature()` 内で現在選択を推測せず、profile wire群とBoolean対象を明示入力として渡し、
definition参照とFeature依存参照を一致させること。

## MISSING TESTS

1. compound内の最初、途中、最後の各Commandを意図的に失敗させ、Document、revision、履歴、表示が完全不変で、直前の別操作をUndoしない。
2. 面押し引きの境界ワイヤー編集で押し出しがdirtyになり再評価される。境界削除ではcascadeまたは登録済み理由番号で壊れた参照になる。
3. 面押し引きを保存・再読込し、依存順どおり同じ形状へ再構築できる。
4. Boolean、複数輪郭、輪郭併産、面押し引きの各確定が一回のUndo/Redoで完全往復する。

## VALIDATION

- 固定差分をコードレビューし、R1の4項目に対する変更と追加自己試験を確認した。
- Windows実機の全体ビルドは、同じ固定HEADに含まれるQ1〜Q5の未解決シンボルで停止した。
  押し出し単独の不具合とは数えないが、このHEADをWindows検証済みとは扱えない。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで直すこと。

1. Documentへ「履歴を追加せずcompound開始前へ戻す」正式なabort APIまたはRAII transactionを追加する。
2. 面境界、全Part/Wire出力、Boolean元表示変更の全結果を検査し、一件でも失敗したらtransactionをabortする。`AdoptExtrudeResult()` は成功/失敗を返す。
3. 押し出しFeature作成APIへ入力UUIDを明示して渡す。面押し引きでは生成profile wire群、Booleanでは対象Partを含め、definitionと依存グラフを一致させる。
4. 上記MISSING TESTSを追加し、`P1-EXTRUDE-R3` として固定BASE/HEADを提出する。

REGRESSION RISKS: Document transactionは全コマンドのUndo/Redoへ波及するため、既存compound、入れ子禁止、例外/早期return、保存直前のrevisionを回帰試験すること。

NEXT_ACTION: P1-EXTRUDE-R3を追加commitで提出する。Q1〜Q5は履歴を戻さず進めてよいが、押し出しを使う総合試験はR3後に再実行する。

---

# Codex Review: Q1-Q5（正対・まとまり・HO見本・総合試験・曲げ半径）

REQUEST_ID: Q1-Q5(正対・まとまり・HO見本・総合試験・曲げ半径)
PHASE: Q1-Q5
BASE: ce369eb
HEAD: 253e446
REVIEW_SCOPE: `ce369eb..253e446` のうちQ1〜Q5関連差分
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T14:55:00+09:00

## BLOCKERS

### B1. Windows本番ビルドがリンクエラーで成立しない

`scripts/check.ps1` は次の未解決シンボルで終了コード1となった。

- `V2MainWindow::MergeFabricationParts()`
- `V2MainWindow::SplitFabricationPart()`
- `fabrication::MergePieces(...)`
- `fabrication::PreviewMerge(...)`
- `fabrication::SplitPiece(...)`

Q5後半のボタン、宣言、試験に対し、実装またはCMake結線が固定HEADに揃っていない。
core試験だけが緑でもWindows配布物は作れないためBlockingとする。

### B2. 曲げ半径がDocumentにも形状にも反映されない

現在のAUTO/LOCKは `V2MainWindow` が一個だけ持つ `bendRadius_` であり、ApproxPartごとの
製作パラメータではない。保存形式にもFeature definitionにも入らず、保存・再読込で消える。
`ApplyBendRadius()` は表示値を変えるだけで近似面・ワイヤーを再生成しない。

- `src/apps/cad_next/V2MainWindow.h:879-886`
- `src/apps/cad_next/V2BendRadiusCommands.cpp:60-125`
- `src/next/kachakacha/document/Feature.h:227-275`

また `MeasureBend()` は3D曲げを測らず、先頭パネル外周長の1/4を90度の円弧と仮定している。
複数部材、非90度、異なる曲率では実寸表示が事実と異なる。半径とLOCK状態を部材ごとの
Document正本へ置き、保存、Undo/Redo、再評価、実際の曲げ形状と同期させること。

### B3. まとまり作成と複数ドラッグ移動が一操作一Undoになっていない

`CreateGroupFromSelection()` はグループ作成とEntity移動を別々に `Document::Run()` する。
一回UndoするとEntityだけ戻り、空のまとまりが残る。移動失敗時にも空グループが残る。
`DropTreeItemsOnto()` は選択グループごとに親変更を個別commitした後、Entityを別commitするため、
途中失敗とUndoで部分移動状態になる。

- `src/apps/cad_next/V2GroupCommands.cpp:67-97`
- `src/apps/cad_next/V2GroupCommands.cpp:218-280`

いずれも一つの原子的Command/transactionにし、失敗時は文書を変えず、一回Undoで完全に戻すこと。

### B4. Q4総合試験が近似作成ワークフローを試していない

「見本の面を板材近似」とする試験は、既にFabricationModelが埋め込まれたHO見本を開き、
`FabricationModelCount() >= 1` を確認するだけである。Surfaceを選択して実際の
`fabrication.create` を通しておらず、候補比較、採用、依存生成、Undoを検査しない。

- `src/apps/cad_next/V2SelfTestHoModel.cpp:192-215`
- `src/apps/cad_next/RailwayNoseHoDocument.cpp`

完成済みfixtureの存在確認では受入試験にならない。少なくとも事前生成FabricationModelを持たない
Surfaceから本番コマンドで近似を作り、0/50/70/100%、任意状態Wire/Surface生成、元ApproxPart非破壊、
Undo/Redo、保存再読込まで通すこと。作業平面・ワイヤー・ガイド・Surface生成も本番経路を通す
別のend-to-end試験を追加すること。

## UX / STATE PROBLEMS

- Q1の複数面選択は点群を全て蓄積しつつ法線を最後の面で上書きする。異なる向きの面を選んだ時の
  意味がUIに示されない。単一対象へ制限するか、平均/主面など明示した規則と試験を設けること。
- trimmed Surfaceの正対で矩形UV領域の標本が実面外に出る可能性がある。穴付き・強いtrim・極を含む面で検証すること。
- Q3のHO見本をFeature定義から組み立てる方針は妥当だが、完成済み近似をQ4の操作受入試験へ流用してはならない。

## MISSING TESTS

1. Windows Debug/Release build、CTest全件、自己試験。
2. まとまり作成、複数グループ/EntityのD&Dについて成功時一回Undo/Redo、各段階失敗時無変更。
3. ApproxPartを二つ作り、異なる半径とAUTO/LOCKを保持して保存・再読込し、形状座標も変わる。
4. 半径変更時に展開長を保ち、割合表示、実寸値、下見、任意状態出力が同じ幾何から導かれる。
5. 空Documentから本番コマンドだけでHO試験体のSurface生成、近似、70%曲げ、Wire生成まで進む。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで修正すること。

1. Q5後半のSplit/Merge実装とCMake結線を揃え、Windowsの全リンクを回復する。
2. 曲げ半径とAUTO/LOCKをApproxPart/部材ごとのFeature definitionへ移し、保存、Undo/Redo、再評価、実形状へ接続する。外周長÷4の仮測定を実寸として表示しない。
3. まとまり作成と複数D&Dを原子的にし、失敗時rollbackと一回Undo/Redoを試験する。
4. Q4を完成済みfixtureの存在確認から、本番コマンドを通るend-to-end試験へ置き換える。
5. Q1の複数対象正対の契約を決め、UIの利用可否、説明、試験を一致させる。
6. 修正後はQ2、Q4、Q5を少なくとも再レビュー対象とし、固定BASE/HEADと新しいREQUEST_IDを提示する。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 画面で閉じた輪郭を選ぶ。既存立体を加工する場合は対象立体も選ぶ。
3. 右棚の入力解釈と作るもの（立体・輪郭）を確認する。
4. 画面の矢印を動かすか距離を入力し、下見を確認する。
5. 「確定」またはEnterで作る。Escまたは「キャンセル」で文書を変えず終了する。

### Surfaceを近似し、70%曲げてWireを生成

現固定HEADでは、初見利用者が信頼できる操作列として認定できない。見本は近似済みで、半径は
表示だけ、総合試験も実際の近似作成を通っていない。目標操作列は
`近似を選ぶ -> Surfaceを選ぶ -> 候補を比較して採用 -> 対象部材を選ぶ -> 70%へ動かす -> 現在状態からワイヤーを作る`
であり、各段階で対象、解釈、結果、確定/取消を右棚とViewportの両方へ一致表示する必要がある。

REGRESSION RISKS: Q2は全Document履歴、Q5は保存形式・再評価・任意状態出力、Q4は受入ゲートの信頼性へ影響する。後続Phase実装済みであること自体はFAIL理由ではない。

NEXT_ACTION: 上記を追加commitで直し、古い未レビュー順を維持したままQ2/Q4/Q5の再レビューを提出する。P1-EXTRUDE-R3は別固定範囲で扱う。

---

# Codex Review: P1-EXTRUDE-R3

REQUEST_ID: P1-EXTRUDE-R3
PHASE: 1 押し出し
BASE: 253e446
HEAD: bd375c9
REVIEW_SCOPE: `253e446..bd375c9` のうち押し出し、Document transaction、依存関係の差分
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T17:30:00+09:00

## RESULT

R2のB2は解消した。押し出しFeatureの入力UUIDはprofile wire群とBoolean対象から明示的に作られ、
definitionと依存グラフが一致する。`Document::Transaction` と各追加処理の成否伝播も導入され、
「失敗後に無条件Undoする」実装は除去された。ただしtransactionの入れ子とDocument外状態の
rollbackが未完成であり、失敗時に見た目と文書が食い違う経路が残る。

## BLOCKERS

### B1. 内側transactionのabortが外側へ伝播せず、失敗分を確定できる

`AbortCompound()` は入れ子深さが残っていると深さを1つ減らすだけで、snapshotを戻さず、
外側transactionを失敗状態にも設定しない。したがって「外側でAを追加 -> 内側でBを追加 ->
内側がabort -> 外側がcommit」の順ではAとBの両方が残る。内側の失敗を呼出側が見落とすと、
原子的であるはずの操作が部分成功として保存される。

- `src/next/kachakacha/document/Document.cpp:236-249`
- `tests_v2/compound_abort_tests.cpp` は「内側commit、外側abort」だけで逆順を試していない

入れ子を正式に禁止して診断するか、内側abortで外側全体をpoisonして最外周commitを拒否すること。
入れ子ごとのsavepointを提供するなら、各深さのsnapshotを保持してその深さまで戻す必要がある。

### B2. transactionがDocumentだけを戻し、OCCT/UI側キャッシュを戻さない

`AddPartFeature()` はtransactionの確定前に `partShapes_`、`partEdges_` を更新し、
`AdoptCurrentDocument()` と `RefreshPartEdges()` まで実行する。`AdoptExtrudeResult()` も全ワイヤーの
追加が終わる前に `partFlatBoundary_` を更新する。その後のワイヤー追加または元立体の非表示化が
失敗すると、Document snapshotだけがdestructorで戻るが、これらのキャッシュとSceneは再構築されない。
Documentに存在しない立体や辺が画面・出力キャッシュへ残り得る。

- `src/apps/cad_next/V2PartCommands.cpp:448-451`
- `src/apps/cad_next/V2PartCommands.cpp:643-646`

全副作用をtransaction確定後に適用するか、失敗時にDocument復元後の正本からSceneと全キャッシュを
必ず再構築すること。Documentだけを検査する試験では不十分である。

### B3. Windows検証ゲートが完走しない

Debugビルドは成功したが、`scripts/check.ps1` は139件中122件まで通過後、
`v2_robustness_tests` で停止した。単体実行では最初の9ケースをPASSした直後、
「おかしな曲線を編集しても落ちない」の開始箇所で出力が止まり、CPU消費も止まった。
Debug assertionのモーダル待ちと整合し、以前報告された `vector subscript out of range` が未解消の
可能性が高い。全検証を完走できない固定HEADは受入不可である。

## MISSING TESTS

1. 内側abort後に外側commitを試し、失敗分が残らないか外側commit自体が拒否される。
2. 立体作成後、1本目のワイヤー追加後、元立体非表示化の各地点で失敗を注入し、Document、履歴、
   `partShapes_`、`partEdges_`、`partFlatBoundary_`、Scene、選択、Previewが開始前と同一になる。
3. 面押し引きのprofile wireを編集・削除し、dirty伝播、再評価、壊れた参照診断を確認する。
4. `v2_robustness_tests` の曲線編集ケースを非対話で完走させ、assertion dialogを出さない。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで直すこと。

1. transactionの入れ子契約を確定し、内側abortを外側commitが飲み込めない実装と試験を追加する。
2. 押し出し確定中のDocument外キャッシュとSceneをstageするか、全失敗経路で復元後のDocumentから
   再構築する。副作用を含む失敗注入試験を追加する。
3. `v2_robustness_tests` の「おかしな曲線を編集」開始直後のDebug assertionを修正し、CTest全件を
   非対話で完走させる。
4. 修正を `P1-EXTRUDE-R4` として新しい固定BASE/HEADで提出する。R1〜R3は残す。

REGRESSION RISKS: transactionは全Document操作、キャッシュ再構築は表示・出力・Undo/Redoへ波及する。
P1を利用する後続PhaseはR4で押し出し、保存再読込、Undo/Redoを再試験すること。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 画面で閉じた輪郭または面を選ぶ。足す/引く場合は対象立体も選ぶ。
3. 右棚でCADの入力解釈と作るものを確認する。
4. 画面の矢印を動かすか距離を入力し、下見と数値が一致することを確認する。
5. Enterまたは「確定」で作成し、Escまたは「キャンセル」で文書を変えず終了する。

### Surfaceを近似して70%曲げ、Wireを生成

このレビュー範囲の主対象ではない。Q1-Q5-R2で本番コマンド経路と任意状態生成を確認する。

NEXT_ACTION: ClaudeがP1-EXTRUDE-R4を追加commitで提出する。後続履歴は戻さない。

---

# Codex Review: Q1-Q5-R2

REQUEST_ID: Q1-Q5-R2
PHASE: Q1-Q5（正対・まとまり・HO見本・総合試験・曲げ半径・部材編集）
BASE: 253e446
HEAD: bd375c9
REVIEW_SCOPE: `253e446..bd375c9` のうちQ1〜Q5関連差分
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T17:30:00+09:00

## RESULT

R1のWindowsリンク不成立、まとまり操作の複数Undo、曲げ半径の画面内だけの保存、完成済みfixtureだけを
見る総合試験は改善された。Split/Mergeは `manualBoundaries` をDocumentへ保存して再構築するところまで
実装されており、単なる表示だけではない。本番作図操作からSurfaceを作る自己試験も追加された。
一方、部材番号の扱いとSplitの事前判定が実操作と一致せず、Windows検証も完走しない。

## BLOCKERS

### B1. 無効な部材番号が最後の部材へ丸められ、別部材を変更する

`FirstPartOr()` は入力番号が範囲外でも `min(numbers.front(), count - 1)` により最後の部材を返す。
たとえば部材が3枚の時に「999」と入力するとエラーではなく部材3の曲げ半径が変更される。
また入力欄は複数部材を表すが、曲げ半径の適用は先頭1件だけであり、利用者の意図がUIから判別できない。

- `src/apps/cad_next/V2BendRadiusCommands.cpp:41-47`
- `src/apps/cad_next/V2BendRadiusCommands.cpp:148-208`

曲げ半径操作は「有効な部材を正確に1つ」に制限するか、明示的に全指定部材へ適用すること。
範囲外番号は登録済み理由番号で拒否し、別部材へ丸めてはならない。

### B2. Splitの可否判定と実際に分割する部材が一致しない

`SplitFabricationPart()` は、現在の部材分割を使わず全panelを架空の1部材へまとめ、後半半分を動かす
`PreviewSplit` を実行する。その後、実際には選択部材のrail中点へ境界を追加する。つまり「分割可能」と
表示する判定対象と、確定時に変更する境界が別物である。選択部材固有の穴、境界、強曲率を見ずに
確定でき、Previewと確定結果が一致する保証がない。

- `src/apps/cad_next/V2PanelEditCommands.cpp:207-248`

現在の `manualBoundaries` と選択部材から実際の分割候補を作り、その同一候補をPreview、診断、確定へ
渡すこと。Preview用の架空partitionを作らない。分割後の部材数、境界、穴、Undo/Redo、再読込を検査する。

### B3. Windows検証ゲートとV2自己試験が完走しない

DebugビルドとCTest #1〜#122は通ったが、#123 `v2_robustness_tests` が曲線編集ケース開始時に停止した。
さらに `kachakacha_cad_next.exe --self-test` は多数のケースをPASSした後、
「中ドラッグでパン、Shift+中でオービット」の実行中に終了コード1となり、完了集計を出さなかった。
リンク回復だけでは配布可能とは判断できない。

## UX / STATE PROBLEMS

- 半径は「部材Nの半径」と表示されるが、実装は各bandの次にあるcrease角から導く。最後のbandには
  次のcreaseがないため、どの折り線を操作する値かを「部材Nの次の折り線」等で明示すべきである。
- 近似方式の比較自己試験は作成、Undo、方式変更、再作成を通すが、初見ユーザーが同じ画面で候補を
  並べて比較するUXは確認できない。自動候補を一方的に採用しない契約にはまだ弱い。
- Q1の複数面正対は最初の面を基準にする規則が加わったが、向きが混在する場合の表示と選択誘導を
  実画面で追加確認する必要がある。

## MISSING TESTS

1. 部材数3で番号0、4、999、複数番号を入力し、誤った部材を変更せず明示診断する。
2. 穴または異なる曲率を持つ選択部材を分割し、Previewと確定後の同一境界を比較する。
3. Split/Mergeを一回Undo/Redoし、保存再読込後も `manualBoundaries` と部材数が一致する。
4. 2つのApproxPartで別々のAUTO/LOCK半径を保持し、70%の表示、形状、任意状態Wire/Surface、
   保存再読込を同じ幾何から検証する。
5. DebugのCTest全139件とV2自己試験を、assertion dialogなしで完走する。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで直すこと。

1. 曲げ半径の対象番号を厳密検証し、範囲外や曖昧な複数指定を理由付きで拒否する。丸めない。
2. SplitのPreview、診断、確定を同じ選択部材・同じ境界候補から生成し、実結果との差をなくす。
3. `v2_robustness_tests` のDebug assertionとV2自己試験の入力操作中の終了コード1を修正する。
4. 上記MISSING TESTSを追加し、`Q1-Q5-R3` として固定BASE/HEADを提出する。R1/R2は残す。

REGRESSION RISKS: 曲げ半径は保存形式・再評価・任意状態出力、部材境界は型紙・穴・部材番号の安定性へ
影響する。Q4/Q5を使う後続PhaseはR3後に再レビュー対象とする。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 閉じた輪郭または面を選び、右棚で入力解釈を確認する。
3. 必要なら立体、輪郭、面の出力を選び、矢印または数値で距離を決める。
4. 下見を確認し、Enterまたは「確定」で作る。Escまたは「キャンセル」で中止する。

### Surfaceを近似して70%曲げ、Wireを生成

1. 「製作モデルを作る」を選ぶ。
2. Surfaceを選び、右棚で対象と近似方式を確認する。
3. 方式候補を切り替えて誤差と部材数を比較し、採用する。
4. 作成されたApproxPartを選び、組立率を70%へ動かす。
5. 対象部材番号と曲げ半径のAUTO/LOCK状態を確認する。
6. 「現在の曲げ状態からワイヤーを作る」を実行し、元ApproxPartが残ることを確認する。

現状は手順3の候補比較が同時比較にならず、手順5で無効番号が別部材へ丸められるため、説明書なしで
安全に完了できる操作列としては認定しない。

NEXT_ACTION: ClaudeがQ1-Q5-R3を追加commitで提出する。後続Phaseは先行してよいがQ4/Q5依存部分をR3後に再試験する。

---

## レビュー履歴の保持規則

`CODEX_REVIEW.md` はREQUEST_ID単位の追記専用ログとして扱う。Claude側の
`AI_HANDOFF_STATE.md` に処理済みと記録されるまで、未処理レビューを削除、置換、単純上書きしない。
修正版が提出された場合も旧結果を残し、R1 FAILからR2 PASSのような経緯を追跡可能にする。

---

# Codex Review: P1-EXTRUDE-R4

REQUEST_ID: P1-EXTRUDE-R4
PHASE: 1 押し出し
BASE: bd375c9
HEAD: 906dd7e
REVIEW_SCOPE: `bd375c9..906dd7e` の押し出し、transaction、失敗時復元、Windows検証
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T22:20:50+09:00

## RESULT

R3のtransaction入れ子、失敗後キャッシュ再構築、TrimCurveの範囲外アクセスは改善された。
内側abortは外側transactionを失敗扱いにし、押し出し失敗後はDocument正本からSceneとキャッシュを
再構築する。以前停止した `v2_robustness_tests` も今回は完走した。ただし、右棚の方向選択が実形状へ
接続されておらず、公式Windows検証も固定HEADでは完走しないため受入不可である。

## BLOCKERS

### B1. 右棚の「方向」は表示だけで、矢印・下見・確定へ反映されない

右棚には「面に垂直」「作業平面に垂直」があり変更通知も出るが、
`RefreshExtrudeFromDock()` は反転、範囲、演算しか読み取らない。
`ExtrudeBaseDirectionNow()` は面法線または輪郭の適合平面法線を常に選び、コンボの値を参照しない。
さらに確定時は非面入力を無条件で `CustomXYZ` に上書きするため、「詳細...」で選んだ方向も失われる。

- `src/apps/cad_next/V2ExtrudeDock.cpp:69-72,125`
- `src/apps/cad_next/V2ExtrudeInteractive.cpp:108-142,265-280`
- `src/apps/cad_next/V2PartCommands.cpp:219-227`

UIが示す状態とCADの解釈が異なる。方向を一つの状態モデルへ統合し、右棚、詳細画面、矢印、下見、
確定結果、保存定義が同じ値を使うこと。面の押し引きで変更不能なら、該当欄を隠すか理由付きで無効化する。

### B2. 固定HEADで公式Windows検証ゲートが失敗する

`scripts/check.ps1` を実行するとbuildは成功し、`v2_robustness_tests` もPASSしたが、141件中2件が失敗した。
`v2_cad_next_smoke` は自己試験中に120秒でtimeoutし、`v2_package_zip` は同梱exeの自己試験が非0で終了した。
結果は139/141 PASS、CTest終了コード8である。提出後の `f479814` が時間切れ修正を含むことは確認したが、
固定HEAD `906dd7e` の判定へ混ぜない。

## UX / STATE PROBLEMS

- 内部処理が出した具体的診断を、`CommitExtrude()` の一般的な失敗文が上書きする経路がある。
  利用者が直すべき対象、方向、演算を特定できる診断を最後まで保持すること。

## MISSING TESTS

1. 右棚の方向を切り替え、矢印、下見頂点、確定形状、保存後定義が同じ方向になるUI自己試験。
2. 「詳細...」で方向を選んだ後、確定処理がその選択を上書きしない試験。
3. Part作成後、最初のWire追加後、対象非表示化後に失敗を注入し、Document、履歴、各キャッシュ、
   Scene、選択、Previewが開始前と同一になる試験。現行の不交差Boolean拒否はcommit前に止まり、
   部分確定後のrollbackを直接検査していない。
4. 公式 `scripts/check.ps1` と単独 `--self-test` の完走。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで修正すること。

1. 押し出し方向を単一の状態モデルへ統合し、右棚と詳細画面から選んだ方向を矢印、下見、確定、保存へ接続する。
2. `PrepareExtrudeChoice()` で利用者の方向指定を無条件上書きしない。変更不能な組合せはUIで先に説明して無効化する。
3. 部分確定後の失敗注入試験を追加し、Document外状態を含む完全復元を検証する。
4. 具体的な失敗理由を一般メッセージで失わない。
5. 公式Windowsゲートを完走させ、新しいREQUEST_ID `P1-EXTRUDE-R5` と固定BASE/HEADを提出する。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 閉じた輪郭または面を選び、右棚に表示された入力解釈を確認する。
3. 作るもの、方向、範囲、演算を選ぶ。
4. 矢印または距離欄で寸法を決め、下見と数値が一致することを確認する。
5. Enterまたは「確定」で作る。Escまたは「キャンセル」でDocumentを変えず終了する。

固定HEADでは手順3の方向指定が動作へ反映されないため、この操作列を完了可能とは認定しない。

### Surfaceを近似して70%曲げ、Wireを生成

この固定範囲の主対象ではない。`Q1-Q5-R3` で確認する。

REGRESSION RISKS: 方向状態の修正は面押し引き、別作業平面の輪郭、反転、対称押し出し、Boolean、
保存再読込へ波及する。transaction変更は全Document操作へ波及するため、失敗注入試験を維持すること。

NEXT_ACTION: 上記を追加commitで修正し、`P1-EXTRUDE-R5` を提出する。R1〜R4は履歴として残す。

---

# Codex Review: Q1-Q5-R3

REQUEST_ID: Q1-Q5-R3
PHASE: Q1-Q5（正対・まとまり・HO見本・総合試験・曲げ半径・部材編集）
BASE: bd375c9
HEAD: 906dd7e
REVIEW_SCOPE: `bd375c9..906dd7e` の部材番号、分割・統合、状態保持、Windows検証
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T22:20:50+09:00

## RESULT

R2の無効部材番号の丸めは修正され、変更操作では有効な1部材だけを要求する。分割・統合も現在の
`manualBoundaries` から同じ境界候補を作り、Documentへ保存してUndo/Redoと再読込を通すようになった。
しかし、UIが約束する事前比較をせず即時確定し、結果不一致時もrollbackせず、分割のたびに利用者が設定した
曲げ状態とLOCK値を全消去する。実製作データを安全に編集できないためFAILとする。

## BLOCKERS

### B1. 「前と後を見せてから決める」と表示するが、Split/Mergeは即時確定する

コマンド説明は変更前後を見て決められると明記する一方、両コマンドは `CommandMode::Instant` であり、
`ApplyBandPartition()` は候補を受け取ると直ちに `ApplyBandBoundaries()` でDocumentを変更する。
Viewportの比較下見、適用、キャンセルという判断段階がない。

- `src/next/kachakacha/app/CommandCatalog.cpp:462-470`
- `src/apps/cad_next/V2PanelEditCommands.cpp:116-142`

候補をツールセッション所有のPreviewとして表示し、部材数、最小幅、誤差、接着線の変化を比較してから
明示的に適用できるようにする。キャンセル、Esc、ツール切替ではDocumentを変更してはならない。

### B2. Previewと確定結果が不一致でも、誤ったDocumentを確定したままにする

`ApplyBandPartition()` は先に境界をcommit・再構築し、その後に実部材数を比較する。不一致時は状態欄へ
警告するだけでrollbackしない。Previewと確定結果が違う場合に利用者のモデルを変更したまま残すのは不可。

- `src/apps/cad_next/V2PanelEditCommands.cpp:125-142`

候補を正本へ入れる前にstageして評価するか、一つのtransaction内で再評価し、不一致ならDocumentと
Sceneを開始前へ戻すこと。成功時はUndo一回、失敗時はUndo履歴を増やさない。

### B3. 一部材の分割・統合で、全ての手入力曲げ設定を無言で消す

境界変更時に `bandProgress`、`creaseProgress`、`bendRadiusMm`、`bendRadiusLock` を全消去し、
`unfoldBaseRail` も0へ戻す。別部材の70%組立状態やLOCK半径まで失われるため、AUTO再計算が利用者入力を
勝手に消してはならないという受入条件に反する。

- `src/apps/cad_next/V2PanelEditCommands.cpp:82-91`

変化しない部材は安定IDまたは境界対応で値を保持する。分割された部材の継承規則、統合時の競合規則、
展開基準の再対応を仕様化し、不可避な破棄だけをPreviewで明示して適用前に選ばせる。

### B4. 境界と最小幅の非有限値検査に抜けがある

`ValidRailParameters()` は最後の要素を `isfinite` で検査せず、端点の `abs(NaN)` 比較もfalseになるため、
`{0, 0.5, NaN}` を有効と扱い得る。`minimumPartWidthMm` や幅がNaNの場合も最小幅拒否をすり抜ける。

- `src/next/kachakacha/fabrication/BandPartition.cpp:21-35,56-66`

全rail、全利用幅、最小幅を先に有限値検査し、最小幅は0以上に制限する。NaN、Inf、逆順、重複端点を
core試験へ追加する。

### B5. 固定HEADで公式Windows検証ゲートが失敗する

`scripts/check.ps1` はbuild成功、`v2_robustness_tests` PASSまで改善したが、`v2_cad_next_smoke` timeoutと
`v2_package_zip` の自己試験失敗により139/141 PASS、CTest終了コード8だった。提出後の修正commitは
固定範囲の判定に含めない。

## UX / STATE PROBLEMS

- 表示目的では無効番号を部材1へ代替するため、入力欄に999が残ったまま右棚が部材1の値を表示し得る。
  変更は拒否できても、現在どの部材を読んでいるかが一致しない。無効表示と理由をその場に出すこと。
- 部材幅は中央列だけから求められる。先細りや二重曲率面では端部が最小幅未満でも通り得るため、
  複数列の最小実幅で判定・表示すること。

## MISSING TESTS

1. Preview表示中のApply/Cancel/Esc/ツール切替。取消時Document、履歴、Sceneが完全不変。
2. 確定結果不一致を注入し、commitもUndo履歴も残らない試験。
3. 3部材へ異なるLOCK半径、組立率、折り線率、展開基準を設定し、中央分割・統合後の継承、
   Undo/Redo、保存再読込を検査する試験。
4. rail末尾NaN、途中Inf、重複、逆順、NaN/Inf幅、NaN/負の最小幅を拒否するcore試験。
5. 幅が変化する面で中央は合格、端は不合格となるケース。
6. 公式 `scripts/check.ps1` と単独 `--self-test` の完走。

## CLAUDE PATCH REQUEST

履歴をreset/rebaseせず追加commitで修正すること。

1. Split/MergeをPreview所有の対話ツールにし、変更前後と製作指標を表示してApply/Cancelを提供する。
2. Previewと再評価結果が不一致なら全状態をrollbackし、警告だけで確定状態を残さない。
3. 境界変更時に未変更部材の組立率、曲げ率、AUTO/LOCK半径、展開基準を保持する安定した再対応規則を実装する。
4. BandPartitionの全数値を有限値検査し、変形面の最小実幅を用いる。
5. 無効な部材番号では別部材の値を表示せず、入力欄の近くに理由を示す。
6. 公式Windowsゲートを完走させ、新しいREQUEST_ID `Q1-Q5-R4` と固定BASE/HEADを提出する。

## 初見ユーザーの操作列

### 押し出し

1. 「押し出し」を選ぶ。
2. 閉じた輪郭または面を選び、入力解釈を確認する。
3. 出力、方向、距離、演算を決め、下見を確認する。
4. Enterまたは「確定」で作る。Escまたは「キャンセル」で中止する。

押し出し自体の判定は `P1-EXTRUDE-R4` を参照する。

### Surfaceを近似して70%曲げ、Wireを生成

1. 「製作モデルを作る」を選ぶ。
2. Surfaceまたは連続Faceを選び、CADの入力解釈を確認する。
3. 候補ごとの部材数、誤差、切れ目を比較して採用する。
4. ApproxPartを選び、対象部材とAUTO/LOCK半径を確認する。
5. 組立率を70%へ動かし、割合、実寸曲げ値、Viewport形状が同期することを確認する。
6. 「現在状態を固定」でWireを生成し、元ApproxPartが可変状態のまま残ることを確認する。

固定HEADでは部材の分割・統合を事前比較できず、実行すると手入力した曲げ状態が消えるため、
実用上安全な操作列として認定しない。

REGRESSION RISKS: 状態再対応は保存形式、部材番号、任意状態出力、型紙へ波及する。Preview導入は
ActiveTool、PreviewOwner、Cancel、Undo/Redoと結線し、Line→Select→Approximation切替でも残留を試験すること。

NEXT_ACTION: 上記を追加commitで修正し、`Q1-Q5-R4` を提出する。R1〜R3は履歴として残す。

---

# Codex Review Addendum: P1-EXTRUDE-R4

REQUEST_ID: P1-EXTRUDE-R4
PHASE: 1 押し出し
BASE: bd375c9
HEAD: f479814
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T22:20:50+09:00

## CORRECTION

レビュー中にClaude側が固定HEADを `906dd7e` から `f479814` へ更新したため、先の記録を削除せず
訂正を追記する。`f479814` のコード差分は `v2_cad_next_smoke` のCTest上限を120秒から900秒へ
変えただけであり、B1の押し出し方向未結線には影響しない。

更新HEAD相当で `scripts/check.ps1` を再実行した。最初の `qt_cad_smoke` は184.38秒でPASSし、
`v2_robustness_tests` も140.04秒でPASSした。しかし `v2_cad_next_smoke` は時間上限ではなく
89.38秒で「吸着半径は拡大しても同じで揺れでは離さずSの間だけ止まる」の途中に異常終了した。
続く `v2_package_zip` も配布版exeの自己試験が非0で失敗した。最終結果は139/141 PASS、
CTest終了コード8である。Handoff記載のWindows 141/141・自己試験225/225は再現できなかった。

BLOCKERS、CLAUDE PATCH REQUEST、MISSING TESTSは直前のP1-EXTRUDE-R4レビューを継続する。
検証についてはtimeout値を延ばすだけでなく、吸着半径ケース付近の異常終了をDebug/Release双方で
再現・修正し、配布版を含む公式ゲートを連続実行して安定性を確認すること。

CLAUDE PATCH REQUEST: 直前のP1-EXTRUDE-R4修正要求に加え、自己試験の異常終了を修正し、
`P1-EXTRUDE-R5` では公式 `scripts/check.ps1` の全141件と配布版自己試験の実測結果を提出する。

NEXT_ACTION: P1-EXTRUDE-R4はFAILのまま。追加commitで修正し、`P1-EXTRUDE-R5` を提出する。

---

# Codex Review Addendum: Q1-Q5-R3

REQUEST_ID: Q1-Q5-R3
PHASE: Q1-Q5（正対・まとまり・HO見本・総合試験・曲げ半径・部材編集）
BASE: bd375c9
HEAD: f479814
VERDICT: FAIL
BLOCKING BEFORE NEXT PHASE: YES
REVIEWED_AT: 2026-09-14T22:20:50+09:00

## CORRECTION

固定HEAD更新に伴う追記である。`f479814` はCTestの時間上限変更だけなので、B1〜B4の部材編集・
数値検証問題には影響しない。更新HEAD相当のWindows公式試験も、上記P1追記と同じく
`v2_cad_next_smoke` の吸着半径ケース付近と `v2_package_zip` で失敗し、139/141 PASSだった。

BLOCKERS、CLAUDE PATCH REQUEST、MISSING TESTSは直前のQ1-Q5-R3レビューを継続する。

CLAUDE PATCH REQUEST: 直前のQ1-Q5-R3修正要求に加え、自己試験の異常終了を修正し、
`Q1-Q5-R4` では公式Windowsゲートと配布版自己試験を完走させる。

NEXT_ACTION: Q1-Q5-R3はFAILのまま。追加commitで修正し、`Q1-Q5-R4` を提出する。
