# AI orchestration state

- Current branch: `codex/v2-ui-ux-overhaul`
- Current phase: `1`
- Current task ID: `UI-P1-007`
- Execution Mode: `STAGED`
- Review Effort: `EXTRA_HIGH`
- Current Stage: `1/2 foundation`
- Last PASS task: `UI-P1-006`
- Current status: `revision`
- Current note: Snap regression repaired; rerun Stage 1 gates and review

## Important design decisions

- `docs/v2/ui-ux-integrated-spec.md` is the master UI specification.
- `.ai/TASKS.json` is the only AI task ledger.
- One task uses one sibling worktree and one task branch.
- Automatic commit and push are off unless explicitly requested.
- V1 remains available as a behavioral reference.

## Next action

Resume Stage 1 from gates, then continue to Stage 2 integration after review PASS.
