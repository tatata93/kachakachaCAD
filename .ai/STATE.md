# AI orchestration state

- Current branch: `codex/v2-ui-ux-overhaul`
- Current phase: `1 - Selection / Hover / Cursor / Snap / Mouse / Highlight`
- Current task ID: `UI-P1-003`
- Last PASS task: `UI-P1-002`
- Current blocked items: `claude` command is not available on PATH on this PC.
- Orchestrator mode: preparation verified; real execution is blocked before mutation because the Claude CLI is unavailable.

## Important design decisions

- `docs/v2/ui-ux-integrated-spec.md` remains the only master UI specification.
- `.ai/TASKS.json` is the only AI task ledger.
- Work is isolated in one sibling Git worktree per task.
- One task has one purpose and normally changes one to five files.
- Claude implements; Codex reviews with `PASS` or `REVISE`.
- Build and test commands are the repository's CMake presets and application
  self-test, not ad-hoc commands.
- Automatic commit is off by default. No automatic push exists in the initial
  orchestrator.
- V1 remains available as a behavioral reference.

## Next action

1. Validate and dry-run `UI-P1-003`.
2. Make the `claude` CLI available on PATH without storing credentials in this
   repository.
3. Run `python scripts/orchestrator.py --execute --task UI-P1-003`.
4. Review the task worktree and recorded evidence before committing/pushing.
