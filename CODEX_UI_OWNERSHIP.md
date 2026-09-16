# Codex UI Frontend Ownership

## Work

- Owner: Codex
- Branch: `codex/ui-frontend-redesign`
- Scope: kachakachaCAD V2 frontend redesign (`UI-RD-01` through `UI-RD-08`)
- Started from: `a864e0b`

## Owned paths

- `src/apps/cad_next/**`
- UI-only adapters in `src/next/kachakacha/app/**` when the existing frontend contract is insufficient
- V2 human-path UI tests and screenshot automation
- UI redesign documentation and command-to-UI reachability maps

## Not owned

- Document model, transactions, Undo/Redo implementation, or persistence core
- Geometry and OCCT kernels
- Extrude, Surface, or fabrication mathematics
- Approximation and unfolding algorithms
- V1 implementation under `src/apps/cad` and `src/core`
- Other agents' in-progress task ledgers in `.ai/**`

## Coordination

Other agents must not edit the owned frontend paths while this branch is active. Backend gaps are recorded as `BACKEND_REQUIREMENT` items instead of being implemented as alternate geometry paths in the UI.

## Planned commits

1. `c34241f` `UI-RD-01`: shell and responsive layout
2. `0592d92` `UI-RD-02`: selection, tree, and workplane interaction
3. `9dfaf8e` `UI-RD-03`: drawing tools
4. `3e38af9` `UI-RD-04`: Extrude interaction layer
5. `f54a4d6` `UI-RD-05`: Surface interaction layer
6. `364feb6` `UI-RD-06`: approximation, bend, and output
7. `c35f23f` `UI-RD-07`: focus, shortcuts, and responsive behavior
8. `UI-RD-08`: human-path tests and screenshots (current commit)

## Completion gate

- Seven canonical Extrude/Surface states captured at 1366 x 768 under
  `build-codex-ui/ui-rd08/`.
- 1024 x 600 compact layout checked without horizontal toolbar overflow.
- Human path covers drawing a profile, creating and picking the completed surface,
  creating a fabrication model, bending it to 70%, and deriving wire output without
  injecting entity IDs into selection.
- Windows build, all CTest cases except the packaging-only zip case, and
  `kachakacha_cad_next.exe --self-test` must be green before the final push.
