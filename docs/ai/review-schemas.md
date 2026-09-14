# レビュー基盤の JSON の形(schema_version 1)

`.ai-runtime/` を流れるファイルはこの4つだけです。すべて `schema_version: 1`。
形を変えるときは番号を上げ、古い番号を読む側で断ります(黙って読み替えない)。

## 1. 依頼の宣言 `tools/ai-local/next-review.json`(git 管理・Claude が書く)

Claude が **REQUEST_ID と BASE を固定する**ためのファイル。HEAD は書きません。
書いても機械は見ません。ビルドした commit だけが HEAD になります。

```json
{
  "schema_version": 1,
  "kind": "review_request_declaration",
  "request_id": "P1-EXTRUDE-R5",
  "base_commit": "<40桁>",
  "review_profile": "QUICK | NORMAL | HIGH_RISK",
  "force_profile": false,
  "scope_ja": "何を見てほしいか(日本語の一段落)",
  "focus": ["観点", "観点"],
  "paths": ["src/", "tests_v2/"],
  "allow_large": false,
  "policy": "docs/ai/CODEX_REVIEW_POLICY.md"
}
```

- `request_id` と `base_commit` だけが必須です。
- 正規の欄は `review_profile` です。古い `review_effort`(LOW/MEDIUM/HIGH/EXTRA_HIGH)も
  読み取りますが、新しく書くときは使いません。
- `review_profile` は**省略してよい**。機械が変更の中身から測ります。書いた場合、
  機械はそれより**深くはしますが浅くはしません**(`force_profile: true` のときだけ従います)。
- `paths` は**見せる範囲**を狭めます。ビルドした commit は変わりません。
- `allow_large` は、大きすぎる変更を1回で見てもらう必要があるときだけ。
- 複数出すときは `kind` を `review_request_declarations` にして `requests` に並べます。
- 1つの依頼を分けて見てもらうときは `segments` を書きます。

```json
{
  "request_id": "P1-EXTRUDE-R5",
  "base_commit": "...",
  "segments": [
    { "name": "QUEUE", "paths": ["tools/ai-local/review-dispatcher.ps1"] },
    { "name": "PARSER", "paths": ["tools/ai-local/review-runner.ps1"], "profile": "HIGH_RISK" }
  ]
}
```

`P1-EXTRUDE-R5` + 区間 `QUEUE` は `P1-EXTRUDE-QUEUE-R5` になります。
区間ごとに履歴が分かれるので、連続 BLOCKING の数え方も区間ごとです。

## 2. 依頼 `.ai-runtime/incoming|ready|processing/<REQUEST_ID>.json`(機械が書く)

```json
{
  "schema_version": 1,
  "kind": "review_request",
  "request_id": "P1-EXTRUDE-R5",
  "root_request_id": "P1-EXTRUDE",
  "attempt": 5,
  "created_utc": "2026-09-14T15:00:00Z",
  "producer": "_FIX_AND_BUILD.cmd",
  "repo_path": "C:/Users/tak01/github/kachakachaCAD",
  "base_commit": "<40桁>",
  "review_commit": "<40桁>",
  "tested_commit": "<40桁>",
  "branch_at_test": "codex/v2-wp01-build-scaffold",
  "build_result": "PASS",
  "test_result": "PASS",
  "selftest_result": "PASS | SKIPPED",
  "evidence": { "ctest": "...", "selftest": "...", "log": "_claudeout/run.txt" },
  "review_profile": "HIGH_RISK",
  "review_effort": "high",
  "profile_reason": "なぜその深さになったか",
  "profile_declared": "QUICK",
  "profile_measured": "HIGH_RISK",
  "risk_signals": ["path:src/next/kachakacha/document/", "name:BeginCompound"],
  "changed_files": ["..."],
  "changed_file_count": 12,
  "changed_lines": 340,
  "diff_bytes": 21044,
  "timeout_seconds": 2400,
  "paths": ["src/", "tests_v2/"],
  "scope_ja": "...",
  "focus": ["..."],
  "policy": "docs/ai/CODEX_REVIEW_POLICY.md"
}
```

`review_commit` はビルドの直前に見た HEAD、`tested_commit` は試験が終わった後の HEAD。
**この2つが違う依頼は作られません。**何を試験したのか言えないからです。

## 3. 結果 `.ai-runtime/results/<REQUEST_ID>.json`(機械が書く)

```json
{
  "schema_version": 1,
  "kind": "review_result",
  "request_id": "P1-EXTRUDE-R5",
  "root_request_id": "P1-EXTRUDE",
  "attempt": 5,
  "base_commit": "<40桁>",
  "review_commit": "<40桁>",
  "tested_commit": "<40桁>",
  "reviewer": "codex | claude-fallback | none",
  "reviewer_command": "実際に走らせた1行",
  "started_utc": "...", "finished_utc": "...",
  "outcome": "REVIEWED | TIMEOUT | INFRA_ERROR | RETRYABLE_ERROR",
  "verdict": "PASS | BLOCKING | STOP | MALFORMED  (outcome が REVIEWED のときだけ)",
  "next_action": "PROCEED | FIX_AND_REVIEW | STOP | RETRY | HUMAN_DECISION_REQUIRED",
  "review_profile": "HIGH_RISK",
  "review_effort": "high",
  "invocation": {
    "reviewer": "codex", "executable": "...", "version": "codex-cli 0.153.4",
    "reasoning_effort": "high", "review_profile": "HIGH_RISK",
    "command_line": "実際に走らせた1行",
    "base_commit": "...", "review_commit": "...",
    "changed_file_count": 12, "changed_lines": 340, "diff_bytes": 21044,
    "path_filter": ["src/"], "timeout_seconds": 2400,
    "queued_utc": "...", "started_utc": "...", "finished_utc": "...",
    "queued_to_start_seconds": 2, "duration_seconds": 214
  },
  "blocking_count": 0,
  "consecutive_blocking": 0,
  "exit_code": 0,
  "review_text_file": ".ai-runtime/results/<REQUEST_ID>.md",
  "packet_file": ".ai-runtime/processing/<REQUEST_ID>/packet.md",
  "worktree": "...",
  "notes": ["読み取り専用違反など、機械が気づいたこと"],
  "processed_by_claude": false
}
```

読み物は同じ名前の `.md`。Codex の答えがそのまま入っています。
`processed_by_claude` は Claude が取り込んだ後に `true` にします。

## 4. 台帳 `.ai-runtime/logs/review-ledger.jsonl`(追記専用)

1行1件の JSON。**書き換えも切り詰めもしません。**

| `event` | 意味 |
| --- | --- |
| `no_request` | 依頼が無かった(Codex は起きていない) |
| `request_enqueued` | 依頼を積んだ |
| `request_refused` | build/test が FAIL、または HEAD がずれた |
| `request_rejected` | 依頼の点検で落ちた |
| `duplicate_request` | 同じ REQUEST_ID をもう一度出そうとした |
| `review_started` | Codex を起動した |
| `review_started` | レビューアーを起動した(profile・effort・差分の大きさ・待ち時間つき) |
| `review_completed` | 判定が出た。**この番号はここで使い切られる** |
| `review_invocation` | 1回の起動の記録(実際のコマンド・所要時間・結果) |
| `review_timeout` | 時間切れで打ち切った。**不合格ではない。**番号は残る |
| `review_infra_error` | 起動できなかった。番号は残る |
| `review_retryable_error` | 起動したが落ちた・何も答えなかった。番号は残る |
| `review_malformed` | 答えは返ったが規約の形ではない。**不合格ではない。**番号は残る |
| `request_too_large` | 1回で読むには広すぎる。区間に分けて出し直す |
| `review_unavailable` | (古い名前。上の3つに分かれた) |
| `claim_recovered` | 落ちた dispatcher の取得を queue へ戻した |
| `dry_run` | 起動するはずのコマンドだけ記録した |
