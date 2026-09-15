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

1. `UI-RD-01`: shell and responsive layout
2. `UI-RD-02`: selection, tree, and workplane interaction
3. `UI-RD-03`: drawing tools
4. `UI-RD-04`: Extrude interaction layer
5. `UI-RD-05`: Surface interaction layer
6. `UI-RD-06`: approximation, bend, and output
7. `UI-RD-07`: focus, shortcuts, and responsive behavior
8. `UI-RD-08`: human-path tests and screenshots

