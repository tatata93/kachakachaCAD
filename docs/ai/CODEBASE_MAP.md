# CODEBASE_MAP

This map is the short entry point for an implementation worker. The normative V2
documents remain under `docs/v2/`; this file only identifies ownership and code
locations. Read `AGENTS.md` and `docs/v2/README.md` before changing V2 code.

## Product and UI specifications

- `docs/product-principles.md`: whole-product intent and fabrication priorities.
- `docs/v2/ui-ux-integrated-spec.md`: normative selection, hover, cursor, mouse,
  preview, property-panel, and logical-pixel behavior.
- `docs/v2/ui-workflows.md`: command-by-command user workflows.
- `docs/v2/command-catalog.md`: stable command IDs and availability rules.
- `docs/v2/acceptance-tests.md`: acceptance test IDs and required behavior.
- `docs/v2/implementation-work-packages.md`: ownership and integration gates.
- `docs/v2/diagnostic-catalog.md`: stable refusal and diagnostic codes.

Do not create a second master UI specification under `.ai/`.

## Main window

- `src/apps/cad_next/V2MainWindow.h`
- `src/apps/cad_next/V2MainWindow.cpp`
- `src/apps/cad_next/V2ModeBar.cpp`
- `src/apps/cad_next/V2Shelves.cpp`
- `src/apps/cad_next/V2PendingCommand.cpp`

`V2MainWindow` owns the application shell, command dispatch, document/session
wiring, model-tree synchronization, tool selection, and dock visibility. Geometry
decisions must not be added here. New panels should stay in their own files.

## CAD viewport and rendering

- `src/apps/cad_next/V2Viewport.h`
- `src/apps/cad_next/V2Viewport.cpp`
- `src/apps/cad_next/V2ViewportDraw.cpp`
- `src/apps/cad_next/V2ViewportInput.cpp`
- `src/apps/cad_next/V2ViewportPanel.cpp`
- `src/apps/cad_next/V2ShapeViews.cpp`

`V2Viewport` maps Qt pointer/key events to V2 core operations and renders the
result using `QPainter`. `V2Viewport.cpp` currently owns hit testing, hover,
selection, and camera event routing. `V2ViewportDraw.cpp` owns visible state such
as ordinary geometry, hover, selection, preview, points, and shape meshes.

The viewport is a boundary layer. Screen-independent selection, snapping, and
geometry calculations belong in `src/next/kachakacha`.

## Model tree

- `src/apps/cad_next/V2MainWindow.cpp`
- `src/apps/cad_next/V2TreeCommands.cpp`
- `src/next/kachakacha/app/NameFilter.h`
- `src/next/kachakacha/app/NameFilter.cpp`
- `src/next/kachakacha/document/Document.h`

The Qt tree and its selection synchronization currently live in the main-window
boundary. Entity/group identity comes from the document and uses UUID-like
strong IDs, not display names.

## Property panels and tool panels

- `src/apps/cad_next/V2ParameterDock.*`
- `src/apps/cad_next/V2DrawingDock.*`
- `src/apps/cad_next/V2PartDock.*`
- `src/apps/cad_next/V2FabricationDock.*`
- `src/apps/cad_next/V2ExportDock.*`
- `src/apps/cad_next/V2MeasureDock.*`
- `src/apps/cad_next/V2WorkPlaneDock.*`
- `src/apps/cad_next/V2EditDock.*`
- `src/apps/cad_next/V2DisplayDock.*`
- `src/apps/cad_next/V2GridDock.*`

These are transitional Qt docks. The integrated UI specification requires one
contextual right property area in the final UI; do not add another permanent
command shelf or modal-only editing path.

## Selection and picking

- `src/next/kachakacha/app/Selection.h`
- `src/next/kachakacha/app/Selection.cpp`
- `src/next/kachakacha/modeling/MeshPick.h`
- `src/next/kachakacha/modeling/MeshPick.cpp`
- `src/next/kachakacha/modeling/SubshapeKey.*`
- `src/apps/cad_next/V2Viewport.cpp`

`SelectionSet::ordered` is the V2 selection source of truth. It stores
`SelectionRef` values and preserves entity, segment, subshape, hit point, and
selection order. `entityIds` is only a compatibility projection for commands not
yet ported to sub-elements.

`CollectPickCandidates` returns all nearby point/curve candidates in stable
priority and distance order. `CollectMeshHits` returns front-to-back shape hits.
The viewport still consumes only one nearest result and is the next integration
boundary for candidate cycling, deep selection, and candidate menus.

## Snap

- `src/next/kachakacha/modeling/SnapEngine.h`
- `src/next/kachakacha/modeling/SnapEngine.cpp`
- `src/next/kachakacha/modeling/GridModel.*`
- `src/next/kachakacha/app/PlaneFocus.*`
- `src/apps/cad_next/V2Viewport.cpp`

The core owns snap candidates and tolerance-based ranking. The viewport supplies
the screen mapping and modifier state. `S` suppresses snapping temporarily;
`Shift` is reserved for temporary drawing constraints.

## Tool control and cursor input

- `src/next/kachakacha/app/DrawingSession.*`
- `src/next/kachakacha/modeling/ToolController.*`
- `src/next/kachakacha/app/ToolTargeting.*`
- `src/next/kachakacha/app/CursorInput.*`
- `src/next/kachakacha/app/EscapeAction.*`
- `src/apps/cad_next/V2ViewportInput.cpp`
- `src/apps/cad_next/V2PendingCommand.cpp`

`DrawingSession` owns the active drawing tool, preview, point placement, and
document command entry. `ToolController`/`ToolSession` implement tool-specific
state without Qt. Pending feature commands use tool-first targeting and must not
invent a second selection manager.

## Undo and redo

- `src/next/kachakacha/document/Document.h`
- `src/next/kachakacha/document/Document.cpp`
- `src/next/kachakacha/document/Commands.h`
- `src/next/kachakacha/document/Commands.cpp`

`Document::Run` is the only mutation gateway. Commands modify a candidate
snapshot, validate it, then commit an atomic delta. Undo/redo stores those deltas;
continuous edits can use compound commands. UI code must not mutate snapshots
directly.

## Document model and feature graph

- `src/next/kachakacha/domain/Entity.*`
- `src/next/kachakacha/domain/Feature.*`
- `src/next/kachakacha/document/Document.*`
- `src/next/kachakacha/evaluation/*`
- `src/next/kachakacha/io/DocumentFile*`

The Feature DAG is authoritative. Entities identify user-visible outputs.
References use strong IDs. V2 `.kcd2` persistence and evaluation are separate
from V1 compatibility import.

## Geometry generation

- `src/next/kachakacha/geometry/*`
- `src/next/kachakacha/modeling/ExtrudeInput.*`
- `src/next/kachakacha/modeling/GuideSurfaceInput.*`
- `src/next/kachakacha/modeling/GuideSurfaceTable.*`
- `src/next/kachakacha/modeling/WireCage.*`
- `src/next/kachakacha/fabrication/*`

These modules contain dependency-free C++20 geometry, approximation,
fabrication, assembly, and unfolding decisions. `src/next` must not depend on Qt
or OCCT. Numerical comparisons use `GeometryTolerance`.

## OCCT boundary

- `src/next_occt/kachakacha/kernel/OcctGuideSurface.*`
- `src/next_occt/kachakacha/kernel/OcctExtrude.*`
- `src/next_occt/kachakacha/kernel/OcctBoolean.*`
- `src/next_occt/kachakacha/kernel/OcctShapeCache.*`
- `src/next_occt/kachakacha/kernel/OcctTessellate.*`
- `src/next_occt/kachakacha/kernel/OcctSolidExport.*`

This adapter layer converts V2 inputs to OCCT shapes, caches/tessellates them,
and exports solids. OCCT types must not leak into `src/next` public APIs.

## Build system

- `CMakeLists.txt`: V1, V2 core, V2 OCCT, applications, and tests.
- `CMakePresets.json`: supported configure/build/test presets.
- `cmake/KachakachaEnvironment.cmake`: local Qt/OCCT discovery.
- `scripts/check-v2.ps1`: Windows V2 configure, build, V2 tests, and all tests.
- `scripts/check-v2.sh`: Linux/core equivalent.
- `.github/workflows/windows-build.yml`: Windows CI and distribution packaging.

Production Windows verification uses the `windows-msvc` preset. Do not replace
the preset with an ad-hoc build directory.

## Test system

- `tests_v2/*.cpp`: dependency-free and kernel V2 test executables registered as
  `v2_*` CTest tests.
- `src/apps/cad_next/V2SelfTest*.cpp`: application-level headless self-tests.
- `build-msvc2022-x64/Release/kachakacha_cad_next.exe --self-test`: UI/kernel
  integration gate.
- `scripts/check-v2.ps1`: full Windows gate.

Selection core coverage is in `tests_v2/selection_tests.cpp`; mesh hit ordering
is in `tests_v2/shape_mesh_tests.cpp`; viewport interaction coverage is mainly in
`src/apps/cad_next/V2SelfTestScreen.cpp` and `V2SelfTestInput.cpp`.

## V1 reference implementation

- `src/apps/cad/*`
- `src/core/*`
- `src/occt/*`

V1 may be inspected for behavior and labels that have not yet been ported. It is
not the V2 architecture and must not be deleted or copied wholesale.
