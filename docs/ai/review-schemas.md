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
  "review_effort": "LOW | MEDIUM | HIGH | EXTRA_HIGH",
  "scope_ja": "何を見てほしいか(日本語の一段落)",
  "focus": ["観点", "観点"],
  "policy": "docs/ai/CODEX_REVIEW_POLICY.md"
}
```

複数出すときは `kind` を `review_request_declarations` にして `requests` に並べます。
`request_id` と `base_commit` は必須、ほかは省略できます(既定は上の通り)。

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
  "review_effort": "HIGH",
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
  "reviewer": "codex | none",
  "reviewer_command": "実際に走らせた1行",
  "started_utc": "...", "finished_utc": "...",
  "verdict": "PASS | BLOCKING | STOP | ERROR",
  "next_action": "PROCEED | FIX_AND_REVIEW | STOP | HUMAN_DECISION_REQUIRED",
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
| `review_completed` | 判定が出た。**この番号はここで使い切られる** |
| `review_unavailable` | Codex が居ない等で、レビューが成立しなかった(番号は残る) |
| `claim_recovered` | 落ちた dispatcher の取得を queue へ戻した |
| `dry_run` | 起動するはずのコマンドだけ記録した |
