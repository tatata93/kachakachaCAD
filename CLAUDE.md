# Claude development handoff

The binding repository instructions are in `AGENTS.md`. Read them before changing code.

Wire-first V2 is the active architecture direction. Do not continue the former Surface/Plate
usability design as the future product model. The current application remains runnable only
until the V2 acceptance gate is complete.

Read in this exact order:

1. `AGENTS.md`
2. `docs/product-principles.md`
3. `docs/refactoring-plan.md`
4. `docs/v2/README.md`
5. every normative V2 document listed by `docs/v2/README.md`
6. `docs/adr/0026-wire-first-v2.md`
7. legacy specifications only when migrating an existing tested algorithm

Do not invent product behavior. Work on exactly one assigned package from
`docs/v2/implementation-work-packages.md`. Use the reusable execution prompt in
`docs/v2/agent-master-prompt.md`, fill in its package, integration branch, and base commit,
then follow it literally.

If you are assigned responsibility for coordinating multiple agents rather than one package,
use `docs/v2/integration-lead-prompt.md` instead. Do not combine integration-lead ownership
with an overlapping implementation package.

Non-negotiable summary:

- Wire and Part are the main user-facing geometry.
- GuideSurface is reference geometry, not a physical part.
- Surface, Plate, and Body do not return as separate ordinary-user object types.
- One Part is one connected closed solid.
- Dependencies use typed UUID references, never display names.
- Derived geometry is read-only until explicitly frozen.
- Core remains independent of Qt and OCCT.
- UI-only stubs, silent fallback meshes, widened tolerances, skipped tests, and nearest-object
  reference repair are not acceptable implementations.
- Old `.kcd` compatibility is not required.
- A work package is complete only after its specified acceptance tests, Windows/CI gate when
  applicable, push, and progress-table update.

When resuming:

1. Synchronize and inspect status without overwriting unrelated changes.
2. Verify the assigned package is available and all dependencies are complete.
3. Push the in-progress lock before implementation.
4. Implement contracts and tests together.
5. Use the exact final report format in `docs/v2/agent-master-prompt.md`.
