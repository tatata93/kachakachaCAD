# AI orchestration state

- Current branch: `codex/v2-ui-ux-overhaul`
- Current phase: `1`
- Current task ID: `UI-P1-003`
- Last PASS task: `UI-P1-003`
- Current status: `passed`
- Current note: UI-P1-003 passed after one revision. CTest 128/128 and application self-test 155/155 are green.

## Important design decisions

- `docs/v2/ui-ux-integrated-spec.md` is the master UI specification.
- `.ai/TASKS.json` is the only AI task ledger.
- One task uses one sibling worktree and one task branch.
- Automatic commit and push are off unless explicitly requested.
- V1 remains available as a behavioral reference.

## Next action

UI-P1-004 is ready. Inspect its scope before starting the next isolated worktree task.
