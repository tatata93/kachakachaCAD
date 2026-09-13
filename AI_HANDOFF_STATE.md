# AI Handoff State (Claude が更新する。Codex は書き換えない)

このファイルは Claude だけが更新する。Codex の返答は `CODEX_REVIEW.md` に書く。
互いのファイルを書き換えないことで、同じファイルへの同時投入を避ける。

## 現在

REQUEST_ID: UI-P1-007-S1-R9
TASK_ID: UI-P1-007
STAGE: 1/2
STATUS: READY_FOR_REVIEW(R8 の B1 を直した)
REVIEW_STATUS: PENDING_CODEX
BASE: 5f6ccbc
HEAD: 9b942b9
BUILD_RESULT: PASS(雲 core)
TEST_RESULT: PASS(雲: core CTest 127/127 + 当て木 59ファイル。PC は往復待ち)
REVIEW_SCOPE: 5f6ccbc..9b942b9 のうち、場面差し替えと Viewport の一時表示
REVIEW_FOCUS: SetScene の知らせが1本になっていること、
  差し替え直後にポインタを動かさなくても旧リング・旧候補送りが消えること、
  自己試験が前提(掴めていること)を必須にしていること
CREATED_AT: 2026-09-14
UPDATED_AT: 2026-09-14

## UI-P1-007 が止まっていた理由と、どう直したか

`.ai/STATE.md` は「blocked: CTest failed with exit code 8」で止まっていた。
実装そのものは作業場 `kachakachaCAD-worktrees/ui-p1-007` に
**コミットされずに** 31ファイル・2250行あった。基点は `ae84ffa`。

その差分を取り出して三方向併合でこの枝へ入れた。**書き直していない。**
ぶつかったのは2か所だけで、どちらも include の並びだった。

止まっていた原因は3つとも門だった。

1. `snap_tests.cpp` が 1562行。1ファイル1500行の門を越えていた。
   持ち越しの試験を `snap_hysteresis_tests.cpp` へ分けた。
2. `V2SelfTestInput.cpp` に `#include <QEvent>` が無かった。
   2026-09-13 に入れた include の門が捕まえた。
3. `V2PartCommands.cpp` の関数が102行。100行の門を越えていた。

併合後、PC の自己試験で17件落ちた。これは併合ではなく、

- 押し出しを二段(下見→確定)にした私の変更に、自己試験19か所が追随していなかった
- Stage 1 が、吸着の入切のたびに帯を案内で上書きしていた
  (断った理由が読む前に消えた)

の2つで、どちらも直した(`188ea47`)。

## §5 の機能要件に対する現状

| 要件 | 状態 | どこで確かめているか |
| --- | --- | --- |
| 捕捉距離と解除距離の分離 | 済 | `snap_hysteresis_tests`(同順位は4px以上近くないと乗り換えない) |
| 微小移動で切り替わらない | 済 | 同上(揺れ・境目の揺れ) |
| 近接した端点/中点/中心/交点で安定 | 済 | 同上(重なった別の形へ移らない) |
| ズームで操作感が変わらない | 済 | `snap_tests`(10 / 1 / 1000 px/mm) |
| いま吸着している対象が分かる | 済 | 白フチ付きの印 + 帯の `[端点]` 表示 |
| 道具切替で残らない | 済 | core `SelectTool` で `Reset` + 画面 `DiscardHoverState`(R8) |
| 取消で残らない | 済 | core `CancelTool` で `Reset` + 画面 `DiscardHoverState`(R8) |
| 場面の差し替えで残らない | 済 | `DrawingSession::SetScene` で `Reset`(R8) |
| S キーで解除 | 済 | 自己試験「Shiftで水平になりSで吸着が止まる」ほか |
| 既存 Selection を壊さない | 済 | `selection_tests` 136行追加、全体 134/134 |
| grab-to-move を壊さない | 済 | `grab_to_move_tests` |

Stage 2 として挙がっていた結線(DrawingSession・道具切替・取消・S キー)は、
取り込んだ Stage 1 の差分にすでに入っていた。上の表のとおり動いている。

## Codex の状況

`.ai/STATE.md` の最終更新は 2026-09-13 20:34。以後応答が無い。
利用制限とみなし、オーナー指示にしたがって **CODEX_PENDING_CONTINUE** で進める。
自分の build / test / 受入試験 / 自己レビューをゲートにする。

## UI-P1-007-S1-R7 の指摘と、どう直したか

Codex は 2026-09-14 に戻ってきて `CODEX_REVIEW.md` に **VERDICT: FAIL** を書いた。
指摘は2つ。どちらも「前の状態が残る」という同じ形をしていた。

- **B1 道具を替えても前の道具の吸着表示が残る。**
  `DrawingSession::SelectTool()` は持ち越しを捨てるのに、
  `V2Viewport::OnToolChanged()` は `hover_.preview` しか消していなかった。
  `hover_.snap`・位置・案内・Tab/Alt の候補送りは前の道具の値のままだった。
  → 道具の持ち物をまとめて捨てる `DiscardHoverState()` を足し、
  いまのカーソル位置で **一度だけ** Hover を取り直す。
  帯は、いま出ているのが前の案内のときだけ書き換える。
  断った理由や確定結果は消さない(`RefreshHoverAfterToolChange`)。
- **B2 場面・文書の差し替えで持ち越しが生き残る。**
  `DrawingSession::SetScene()` が `scene_` を代入するだけだった。
  開く・Undo/Redo・作業平面切替・グリッド変更はどれもここを通る。
  → 明示メソッドにして `snapHysteresis_.Reset()` する。

直しながら **同じ穴がもう1か所** あったので一緒に直した。
`V2Viewport::CancelTool()` も、core が持ち越しを捨てるのに画面は preview しか
消しておらず、取り消した直後のリングと診断情報が session の中身と食い違っていた。

指摘された MISSING TESTS はすべて足した。

| Codex の要求 | どこ |
| --- | --- |
| (1) 直線→円弧→ベジェ→スプライン→選択、マウスを動かさず残らない | 自己試験「道具を替えると前の吸着と候補送りが消える」 |
| (2) SetScene 交換後、12px の外・16px の内で旧候補へ吸着しない | core `session` 「場面の差し替え後は12pxの外の旧候補へ吸着しない」 |
| (3) 開く/Undo/Redo/作業平面切替/グリッド変更の各経路 | 自己試験「場面を差し替えると吸着の持ち越しが消える」 + core「場面を差し替えると吸着の持ち越しを捨てる」 |
| (4) Cancel 直後のリングと診断情報が現在の状態と一致 | 自己試験「取り消した直後に古いリングが残らない」 |

非阻害の指摘(§6.1 道具に応じた吸着の強さ)は未着手のまま残す。
候補の種類を道具ごとに変える段でまとめて片づける。

## UI-P1-007-S1-R8 の指摘と、どう直したか

Codex は R8 を **FAIL** にした。指摘は的確だった。

- **B1 場面差し替え直後の旧吸着リングが画面側に残る。**
  core の持ち越しは捨てたが、画面の `hover_` を捨てていなかった。
  `SetScene` の呼び口は10か所以上ある。呼び口ごとに後始末を書けば必ず抜ける。
  → `DrawingSession::SetScene` から必ず呼ばれる知らせを1本足した
  (`SetSceneChangedCallback`)。画面はそこで `DiscardHoverState()` する。
  取り直しはしない。菜単から替えたときはポインタが画面の外にありうる。
- **試験の指摘。** R8 の自己試験は差し替えのあと `HoverAt()` を呼んでいた。
  古い `hover_` を上書きしてから見ていたので、
  「ポインタを動かさない直後」の残りを検出できていなかった。
  → 差し替え後は何も呼ばずに見る。
- **前提が緩かった。** `!heldBefore || !heldAfter` は前提が崩れても通る。
  → 「14px 先でも持ち越した端点へ吸い付いている」を必須検査にした。
- **道が足りなかった。** 実際に通していたのは Undo/Redo/グリッドの3つだけだった。
  → 開き直しと作業平面変更を足して5つにした。

## PENDING_CODEX_REVIEWS(古い順。消さない)

- REQUEST_ID: UI-P1-007-S1-R9 / TASK: UI-P1-007 / STAGE: 1/2
  BASE: 5f6ccbc / HEAD: 9b942b9
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS(雲) / TEST: PASS(雲 core 127/127)
  前身: R7(FAIL) → R8(FAIL)。R8 の B1(画面側の一時表示)を直した。
- REQUEST_ID: P1-EXTRUDE-R1 / TASK: 押し出しUI / STAGE: 途中
  BASE: 4fa218c / HEAD: 188ea47
  CLAUDE_SELF_REVIEW: 未(機能として未完成。完成まで送らない)
  BUILD: PASS / TEST: PASS

## 要確認(Codex 領分に触った)

- **面の押し引きが文書にワイヤーを増やす**(EX-02、2026-09-14)。
  面を押すと、押した面の縁が「面の縁」という名前のワイヤーとして残る
  (穴があればその数だけ)。押し出しの記録(`ExtrudeDefinition`)は
  「どのワイヤーを押したか」で出来ており、面番号は作り直すたびに変わるので
  記録できない(architecture-and-data.md §6)。縁をワイヤーにすれば、
  保存の形も Command の作られ方も変えずに、開き直しても作り直せる。
  代わりの案は `ExtrudeDefinition` へ面の意味的キーを足すことで、そちらは GUARDED。
  **「面を押したのに線が増える」ことの是非を見てほしい。**

- `docs/manual.html` に3行足した(`help.copy_diagnostics` `wire.center_points`
  `wire.key_points`)。門が全コマンドの説明を求めるため。文言の確認だけ。
- `samples/*.kcd2` を書き直した。保存の鍵が増えたため(切れ目の上限、選択半径)。

## Phase の進み

- Phase 0 Baseline: 完了。
- UI-P1-007: Stage 1 取り込み・修正まで完了。Codex レビュー待ちだが止まらない。
- Phase 1 押し出し: ほぼ完了。
  - 選択の読み取り(A〜E、Target/Profile 判定): 完了 `2349ebd`
  - 矢印ハンドル・距離同期・下見・Enter/Esc: 完了 `e774350`
  - 右ペイン(入力・距離・方向・範囲・操作・確定): 完了 `5354276` `7f13b72`
  - 入力の選び直し(EX-07): 完了。`app::SelectionWithout` + 棚の2つのボタン。
  - 面の押し引き(EX-02): 実装済み・PC 確認待ち。
    `kernel/OcctFaceQuery` が面の縁を返し、`app/FacePushPull` が
    符号つきの距離を押し出しの言葉へ言い換え、`V2FacePushPull` が
    縁を文書のワイヤーにしてから、いままでの押し出しへ渡す。
    雲では OCCT を組み立てられないので、PC の1往復で初めて確かめられる。
- Phase 2〜5(板材近似): 未着手。

## 押し出しの受入試験(指示 §33)

| ID | 内容 | 状態 |
| --- | --- | --- |
| EX-01 | 閉じた輪郭 → 押し出し → ハンドル → Enter → 新規立体 | 済 |
| EX-02 | 立体の面 → 押し引き | 実装済み・PC 確認待ち |
| EX-03 | 立体だけ → 「面または輪郭を選んでください」 | 済 |
| EX-04 | 立体+輪郭を順不同で選んでも役割が決まる | 済 |
| EX-05 | 距離の欄と矢印が同期 | 済 |
| EX-06 | 方向反転で矢印も反転 | 済 |
| EX-07 | 入力の差し替え | 済(棚に「対象を選び直す」「輪郭を選び直す」。片方だけ外れる) |
| EX-08 | 道具を替えると下見・棚・一時状態が残らない | 済 |
