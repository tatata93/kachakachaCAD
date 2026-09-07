# Claude development handoff

This repository's binding instructions are in `AGENTS.md`. Read it before changing code.
Product decisions belong in `docs/`; do not treat this file as a replacement product specification.

For the current Surface/Plate usability work, read these files in order:

1. `docs/product-principles.md`
2. `docs/refactoring-plan.md`, especially item 8
3. `docs/sheet-part-ui-spec.md`
4. `docs/usability-review.md`

The intended result is one user-facing **面部品** workflow while preserving the internal
`Surface` (shape) and `Plate` (manufacturing specification) distinction. Do not reintroduce
separate ordinary-user screens for surfaces and plates, an explicit source-surface picker,
or the former multi-output thickness form. Keep `.kcd` read compatibility.

When resuming interrupted work:

1. Run `git status --short --branch` and do not overwrite unrelated changes.
2. Check the state and branch recorded in `docs/refactoring-plan.md`.
3. Continue from the latest pushed commit, then run the Windows verification gates in
   `AGENTS.md`.
4. Update documentation and self-tests together with UI behavior.
5. Commit and push all completed work. Do not leave `main` red.

The reusable implementation prompt and acceptance criteria are in
`docs/sheet-part-ui-spec.md` under "Claude・別AIへ渡す実装プロンプト".
