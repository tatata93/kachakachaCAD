# AI development orchestration

This directory coordinates risk-sized V2 tasks between a design/review agent and
an implementation agent. It does not replace the normative product documents.

## AI development structure

1. Codex investigates the repository, chooses task granularity and risk policy,
   writes acceptance criteria, and reviews only the required boundaries.
2. Claude Code edits the assigned files and adds or updates tests.
3. `scripts/orchestrator.py` creates one task worktree, invokes the worker, runs
   the existing local build/test gates, and packages a reviewer request.
4. A reviewer returns only `PASS` or `REVISE`. The task ledger records the result.
5. Commit and push remain manual by default even after `PASS`.

Before execution, the orchestrator assigns both an Execution Mode and Review
Effort. Missing fields on an old task are inferred and saved when execution starts.

- `BATCH`: one cohesive low-to-medium-risk implementation, then one final review.
- `STAGED`: two to four meaningful stages; review only marked boundaries and final.
- `GUARDED`: bounded high-risk infrastructure work with short revision loops.
- `LOW / MEDIUM / HIGH / EXTRA_HIGH`: logical review depths mapped to the local
  Codex CLI by `.ai/ORCHESTRATOR_CONFIG.json` without hard-coding a model name.

`TASKS.json` is the sole task ledger. Chat history, TODO comments, and commit
messages are not substitutes for it.

## Sources of truth

- UI behavior: `docs/v2/ui-ux-integrated-spec.md`
- Workflows: `docs/v2/ui-workflows.md`
- Acceptance: `docs/v2/acceptance-tests.md`
- Code ownership: `docs/ai/CODEBASE_MAP.md`
- Current assignment: `.ai/CURRENT_TASK.md`
- Cross-session state: `.ai/STATE.md`
- Review profile mapping: `.ai/ORCHESTRATOR_CONFIG.json`

## Roles

### Codex

- Inspect current code and specifications.
- Keep tasks to one purpose. Use BATCH for cohesive work, STAGED for 5-15 file
  cross-module work, and GUARDED for high-risk shared foundations.
- Review `CURRENT_TASK`, worker report, build/test evidence, and the complete diff.
- Return `PASS` or a concrete `REVISE` request; do not silently repair the worker
  diff during the review role.

### Claude Code

- Treat `.ai/CURRENT_TASK.md` as the only current assignment.
- Follow `AGENTS.md`, the referenced V2 specification sections, and
  `.ai/prompts/CLAUDE_WORKER.md`.
- Reuse existing selection, tool, command, and document infrastructure.
- Report real build/test outcomes and unresolved issues.

### Orchestrator

- Validate the ledger and dependency graph.
- Select one `ready` task.
- Work on `codex/ai-<task-id>` in a sibling Git worktree.
- Stop on missing tools, dirty tracked files, dangerous branches, command errors,
  invalid reviewer output, or the revision limit.
- Promote BATCH to STAGED for unexpectedly broad diffs and any mode to GUARDED
  when Undo, save format, Document architecture, ownership, or threading is touched.
- If Codex reports an account usage limit, use the configured read-only Claude
  fallback reviewer and record the provider in `last_review`.
- Never runs `reset --hard`, `clean -fd`, force push, or direct commits to
  `main`/`master`.

Optional task fields are `execution_mode`, `review_effort`, their reason strings,
and `stages`. Old entries remain valid. A Stage has `title`, optional `id`,
`objective`, `work_steps`, `acceptance_tests`, and `review_after`. STAGED tasks
without an explicit plan receive a conservative two-stage runtime plan.

## Actual execution flows

### BATCH

`Codex task specification -> one Claude implementation -> local gates -> Claude
self-review -> one independent final review`. A reviewer correction returns the
whole bounded batch to Claude. Small ledger fragments are not automatically
merged because semantic compatibility cannot be inferred safely; Codex should
write one cohesive feature group as one BATCH task.

### STAGED

Claude implements two to four declared stages in order. Every stage must pass the
local gates and Claude self-review. The independent reviewer runs only where
`review_after` is true and always after the final stage. The generated fallback
plan has a foundation boundary and a final integration boundary.

### GUARDED

The task itself must already be a small high-risk unit. Claude, gates,
self-review, and independent review form a bounded revision loop. Changes to
Undo/Redo foundations, save format, Document architecture, shape ownership,
lifetime, or threading force this mode.

## Automatic escalation

The initial score uses estimated file count, subsystem count, state/data changes,
shared foundations, regression breadth, and specification decisions. Actual
`git diff --numstat` and changed paths then update the policy. A BATCH diff is
promoted to STAGED at 10 files, three subsystems, or 2000 changed lines. A diff
that reaches a guarded foundation is promoted to GUARDED. Promotion immediately
raises the self-review, independent review, and any following revision depth. It
never pretends to split an implementation pass that Claude has already completed;
an originally BATCH run therefore retains its single final boundary. Explicit
stages remain valid if STAGED is promoted to GUARDED. Automatic demotion is not
performed.

Review Effort is scored separately from Execution Mode. Source logic uses MEDIUM
as its normal floor; multi-module state or geometry work rises to HIGH, and
history/ownership/data-integrity work can rise to EXTRA_HIGH. The logical effort
is recorded in `TASKS.json`, `CURRENT_TASK.md`, and `STATE.md`. The config maps it
to a supported Codex effort and chooses the nearest configured effort if the
preferred one is unavailable. If the Codex account reports a usage limit, only
that condition permits the configured read-only Claude fallback.

## Commands

Run from the repository root with Python 3:

```powershell
python scripts/orchestrator.py validate
python scripts/orchestrator.py doctor
python scripts/orchestrator.py --dry-run --task UI-P1-003
python scripts/orchestrator.py --execute --task UI-P1-003
```

No subcommand means `run`. A run is dry by default; only `--execute` may create a
worktree or invoke an agent. `--dry-run` always wins over `--execute`.

Useful options:

```text
--task ID             choose one ready task
--timeout SECONDS     per external command timeout (default 1800)
--max-revisions N     reviewer revision limit, 1..3 (default 3)
--auto-commit         commit a PASS result on the task branch (default off)
--base-ref REF        worktree start ref (default current HEAD)
```

## Windows build and test gates

The orchestrator uses the existing project commands from `CMakePresets.json`:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel
ctest --preset windows-msvc
build-msvc2022-x64\Release\kachakacha_cad_next.exe --self-test
```

The equivalent one-command developer gate is:

```powershell
.\scripts\check-v2.ps1
```

Linux workers use `linux-core` and cannot claim Qt/OCCT UI verification.

## Dry run

Dry run validates the ledger, resolves the task, checks the current branch and
tracked cleanliness, and prints the planned worktree, Claude, build, test, and
review commands together with the inferred mode, effort, reasons, and stages. It
does not write files, create branches/worktrees, or start an agent. Missing
optional agent CLIs are warnings in dry run and fatal in execute mode.

Claude runs non-interactively with file edits accepted and permission prompts
disabled. The task worktree is the containment boundary. If the worker exits
without leaving any task-worktree change, the run stops before configuration or
build instead of treating a permission request as successful implementation.

## Task statuses

Only these values are valid:

- `pending`: dependency or scheduling wait.
- `ready`: eligible for one worker run.
- `in_progress`: worker currently owns it.
- `review`: implementation and local gates finished.
- `revision`: reviewer requested a bounded correction.
- `passed`: reviewer accepted it; commit/push may still be pending.
- `blocked`: execution cannot proceed; `blocked_reason` must explain why.

## Worktree operation

Worktrees are created beside the repository, under
`kachakachaCAD-worktrees/<task-id>`. They are never bulk-created. The orchestrator
may reuse only a worktree already checked out on the expected task branch.
Untracked owner files in the main checkout are left alone; tracked modifications
cause the run to stop.

## Reports and reviews

- `.ai/reports/<TASK_ID>.md`: worker result plus build/test summary.
- `.ai/reviews/<TASK_ID>.md`: exact reviewer decision and findings.
- `.ai/runtime/<TASK_ID>/`: disposable raw output and diff; ignored by Git.

These files must contain no API keys, tokens, passwords, or private paths that are
not needed to locate the repository worktree.

## Stopping and recovery

Use `Ctrl+C` to stop the current child process. The orchestrator does not clean or
reset the worktree. Inspect it with `git status` and resume the same task after
fixing the stated blocker. Never delete a worktree until its diff and reports have
been reviewed.

If a task is incorrectly left `in_progress`, inspect `.ai/STATE.md`, its runtime
logs, and the task worktree. Then change the ledger to `ready`, `revision`, or
`blocked` with a written reason. Do not invent `passed` without review evidence.

## Current machine

At preparation time, `codex`, Git, CMake, CTest, and PowerShell were detected.
Claude Code 2.1.268 was installed by WinGet, but its link directory was absent from
the inherited `PATH`. The orchestrator also checks WinGet's standard link and
`~/.local/bin`; it still never installs tools or stores authentication data.
