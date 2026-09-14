# ローカル・イベント駆動レビュー基盤

Codex に10分ごとに「仕事はありますか」と探させるのをやめ、
**PC が本当にビルドしてテストに通ったときだけ、普通のプログラムが Codex を1回起動する**
仕組みに移しました。

## 全体の流れ

```
Claude(クラウド)          PC(Windows)                          Codex
  実装・コミット
  git bundle  ──────────▶  _WATCH.cmd が bundle を見つける
                            _FIX_AND_BUILD.cmd
                              fetch / ff-merge
                              REVIEW_HEAD を記録   ← ここで固定
                              configure / build
                              CTest / self-test
                              TESTED_HEAD を記録
                              review-enqueue.ps1
                                 依頼が無い          → 何も起きない
                                 build/test が FAIL  → failed/ に理由を書いて Claude へ戻す
                                 HEAD がずれた       → 起動しない
                                 合格                → incoming/ に依頼を1件置く
                            review-dispatcher.ps1(常駐・PowerShell)
                              incoming → precheck → ready
                              ready → processing(原子的な取得)
                              detached worktree を HEAD に固定 ───────▶ codex exec(1回だけ)
                              結果を results/ と台帳へ           ◀─────── VERDICT / NEXT_ACTION
  結果を読む ◀────────────  results/*.json, results/*.md
  CODEX_REVIEW.md へ追記
  修正して新しい REQUEST_ID で再提出
```

AI が queue を見張る役をしません。見張るのは `review-dispatcher.ps1` という普通のプログラムです。

## 置き場所

`.ai-runtime/`(git 管理外。`.gitignore` 済み)

| ディレクトリ | 中身 |
| --- | --- |
| `incoming/` | 出来立ての依頼。まだ検査していない |
| `ready/` | 検査を通り、レビュー待ち |
| `processing/` | 誰かが取得済み。`<name>.json.owner` に取得した pid が入る |
| `results/` | レビュー結果(`<id>.json` と読み物の `<id>.md`)と、処理済みの依頼 |
| `failed/` | 断った依頼と、その理由 |
| `stale/` | 取り残されて捨て場所に移したもの |
| `locks/` | `dispatcher.lock`(常駐を1つに保つ) |
| `logs/` | `dispatcher.log`, `review-ledger.jsonl`(追記専用台帳), `codex-interface.json` |
| `worktrees/` | 予備の worktree 置き場(通常は `..\kachakachaCAD-worktrees\ai-review` を使う) |

## スクリプト

| ファイル | 役目 |
| --- | --- |
| `tools/ai-local/review-common.ps1` | 置き場所・原子的な書き込み・鍵・プロセス起動の共通部品 |
| `tools/ai-local/review-precheck.ps1` | 機械の点検と、依頼1件の点検。**Codex の実際の interface を実物で調べる** |
| `tools/ai-local/review-enqueue.ps1` | 依頼の唯一の入り口。PC の実測結果から manifest を作る |
| `tools/ai-local/review-dispatcher.ps1` | 常駐。監視・検査・取得・起動。`-Once` / `-DryRun` あり |
| `tools/ai-local/review-runner.ps1` | 依頼1件を worktree 固定でレビューさせ、結果を書く |
| `tools/ai-local/review-ledger.ps1` | 追記専用台帳の読み書きと、重複・連続 BLOCKING の判定 |
| `tools/ai-local/review-recover.ps1` | 落ちた後の後始末(取り残し・書きかけ・迷子の worktree) |
| `tools/ai-local/queue-status.ps1` | いまの queue を1画面で見る |
| `tools/ai-local/review-selftest.ps1` | 上の約束を、使い捨ての git リポジトリで実際に確かめる |
| `tools/ai-local/start-dispatcher.cmd` | 常駐を1回だけ起動する |

## 依頼の出し方(Claude 側)

`tools/ai-local/next-review.json` を**コミットに含めて**送ります。

```json
{
  "schema_version": 1,
  "kind": "review_request_declaration",
  "request_id": "P1-EXTRUDE-R5",
  "base_commit": "570473e...",
  "review_effort": "HIGH",
  "scope_ja": "何を見てほしいか",
  "focus": ["観点1", "観点2"],
  "policy": "docs/ai/CODEX_REVIEW_POLICY.md"
}
```

- **REQUEST_ID と BASE は Claude が固定します。**
- **HEAD は機械が固定します。**実際にビルドした commit しか対象になりません。
- このファイルが無いビルドでは、Codex は一度も起動しません。
- 同じ REQUEST_ID は二度受け付けません。直したら `R5 → R6` にします。

## 守っていること

| 危険 | 手当て |
| --- | --- |
| 依頼が無いのに Codex が動く | 依頼ファイルが無ければ起動しない。台帳に `no_request` が残る |
| 同じ依頼を二度レビュー | 台帳の `review_completed` と原子的な取得の両方で止める |
| dispatcher の二重起動 | `locks/dispatcher.lock` を排他で掴む。掴めない方は黙って終わる |
| レビュー中に HEAD が進む | detached worktree を REVIEW_HEAD に固定。branch は見ない |
| ビルドした commit と違うものをレビュー | `TESTED_HEAD != REVIEW_HEAD` なら起動しない |
| build/test が FAIL | 起動せず `failed/` に理由を書いて Claude に戻す |
| 途中で落ちる | `review-recover.ps1` が取得済みを queue に戻す。起動時に必ず走る |
| 書きかけの JSON を読む | 書き込みは必ず `*.tmp` → rename。読む側は `*.tmp` を見ない |
| FileSystemWatcher の取りこぼし | 起動時・毎周・1件処理ごとに全走査もする |
| Codex が source を書き換える | `--sandbox read-only`(実装が持っていれば)＋ 事後に `git status` で検出し破棄・記録 |
| 存在しない CLI option を使う | `codex exec --help` を実物で読み、**見えた option しか渡さない** |
| 同じ所を延々と往復 | 同じ root request で3回続けて BLOCKING なら `HUMAN_DECISION_REQUIRED` |
| 過去の履歴が消える | 台帳は追記専用。`CODEX_REVIEW.md` も追記のみ |

## 動かし方

```
:: 常駐を1回だけ起動(開いたままにする)
tools\ai-local\start-dispatcher.cmd

:: いまの状態を見る
tools\ai-local\queue-status.cmd

:: 起動するはずのコマンドだけ見る(Codex は動かさない)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\ai-local\review-dispatcher.ps1 -Once -DryRun

:: 基盤そのものを検査する
powershell -NoProfile -ExecutionPolicy Bypass -File tools\ai-local\review-selftest.ps1
```

## GitHub の扱い

GitHub は **常時のレビュー通信路としては使いません**。
push は今までどおり控えの取り方として残します。
レビューの依頼・結果・台帳は、この PC の `.ai-runtime/` の中だけで完結します。
