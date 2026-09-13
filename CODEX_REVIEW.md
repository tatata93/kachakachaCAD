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
