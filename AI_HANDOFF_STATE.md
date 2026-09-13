# AI Handoff State (Claude が更新する。Codex は書き換えない)

このファイルは Claude だけが更新する。Codex の返答は `CODEX_REVIEW.md` に書く。
互いのファイルを書き換えないことで、同じファイルへの同時投入を避ける。

## 現在

REQUEST_ID: P1-EXTRUDE-R1
TASK_ID: Phase 1 押し出し
STAGE: 1/1
STATUS: WORKING(面の押し引きが残っている)
REVIEW_STATUS: PENDING_CODEX
BASE: 4fa218c
HEAD: 7f13b72
BUILD_RESULT: PASS
TEST_RESULT: PASS(PC: CTest 134/134、アプリ自己試験 187/187。雲: core 128/128、当て木 57ファイル)
REVIEW_SCOPE: 4fa218c..188ea47 のうち、スナップに関わる分
REVIEW_FOCUS: 捕捉と解除の分離、持ち越しの同一視、道具切替と取消での破棄、既存選択の退行
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
| 道具切替で残らない | 済 | `DrawingSession::SelectTool` で `Reset` |
| 取消で残らない | 済 | `CancelTool` で `Reset` |
| S キーで解除 | 済 | 自己試験「Shiftで水平になりSで吸着が止まる」ほか |
| 既存 Selection を壊さない | 済 | `selection_tests` 136行追加、全体 134/134 |
| grab-to-move を壊さない | 済 | `grab_to_move_tests` |

Stage 2 として挙がっていた結線(DrawingSession・道具切替・取消・S キー)は、
取り込んだ Stage 1 の差分にすでに入っていた。上の表のとおり動いている。

## Codex の状況

`.ai/STATE.md` の最終更新は 2026-09-13 20:34。以後応答が無い。
利用制限とみなし、オーナー指示にしたがって **CODEX_PENDING_CONTINUE** で進める。
自分の build / test / 受入試験 / 自己レビューをゲートにする。

## PENDING_CODEX_REVIEWS(古い順。消さない)

- REQUEST_ID: UI-P1-007-S1-R7 / TASK: UI-P1-007 / STAGE: 1/2
  BASE: 4fa218c / HEAD: 188ea47
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS / TEST: PASS
- REQUEST_ID: P1-EXTRUDE-R1 / TASK: 押し出しUI / STAGE: 途中
  BASE: 4fa218c / HEAD: 188ea47
  CLAUDE_SELF_REVIEW: 未(機能として未完成。完成まで送らない)
  BUILD: PASS / TEST: PASS

## 要確認(Codex 領分に触った)

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
  - 面の押し引き(EX-02): **未**。画面が面を拾えず、カーネルにも道が無い。
    ここだけは往復が要る(雲では OCCT を組み立てられない)。
- Phase 2〜5(板材近似): 未着手。

## 押し出しの受入試験(指示 §33)

| ID | 内容 | 状態 |
| --- | --- | --- |
| EX-01 | 閉じた輪郭 → 押し出し → ハンドル → Enter → 新規立体 | 済 |
| EX-02 | 立体の面 → 押し引き | **未**(面を拾えない) |
| EX-03 | 立体だけ → 「面または輪郭を選んでください」 | 済(案内まで。面選択は EX-02 待ち) |
| EX-04 | 立体+輪郭を順不同で選んでも役割が決まる | 済 |
| EX-05 | 距離の欄と矢印が同期 | 済 |
| EX-06 | 方向反転で矢印も反転 | 済 |
| EX-07 | 入力の差し替え | 一部(選び直せば読み直す。専用の「再選択」ボタンは未) |
| EX-08 | 道具を替えると下見・棚・一時状態が残らない | 済 |
