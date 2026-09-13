PASS

Stage 1/2 (foundation) of UI-P1-007 is done and nothing blocks it. I edited nothing.

**Evidence**
- **Separate radii:** `GeometryTolerance.h` has three fields: `displayPickPx=8` (points), `edgePickPx=6` (lines) and `snapPickPx=12` (snap). Each radius is used in only one place:
  - points at `Selection.cpp:144`
  - lines at `Selection.cpp:114`
  - snapping at `SnapEngine.cpp:64` and `:246`.

  Nothing still uses the old `highPriorityRadiusPx`, `lowPriorityRadiusPx` or `IsHighPrioritySnap`. Tests check that changing one radius leaves the others alone (`snap_tests.cpp:1065`, `selection_tests.cpp:348-362`).
- **Same radius at any zoom:** radii are measured in screen px on the projected curve (`geometry::ApproachToCurveOnScreen`). The perspective correction from screen fraction back to 3D fraction, `s = t·w0/((1−t)·w1 + t·w0)`, is derived correctly. Zoom is tested for snapping (`snap_tests:1040`) and for circle and Bezier picking when zoomed in (`selection_tests:325`, `:332`). Line picking is shared with Selection instead of duplicated.
- **Hysteresis:** `ChooseSnap` switches at once to a higher-rank candidate. At the same rank it switches only when the new candidate is more than 4 px closer, and it holds out to 12 + 4 px. Candidates are sorted by rank, then distance, so the held candidate can never outrank the best one. `S` (suppressed) and `Reset` drop the held candidate. Tests cover jitter, the radius edge, higher-rank takeover, and not handing the hold to an overlapping curve (`snap_tests:1092-1310`).
  - The held candidate keeps `EntityId`s but only compares them and never looks the entity up. A deleted or undone entity cannot cause a bad access; it just stops matching.
  - No `SnapCandidate` equality comparison exists, so the new `held` field changes no existing behaviour.
- **Ranking against §6.1 and §4.3:**
  - `SnapPriorityRank` follows the six levels, and ties go to the closer candidate.
  - Moving the values in `SnapKind` breaks nothing: the only numeric use is a tie-break inside the sort. The app only names the kinds in `switch` statements, and they are not saved to files.
- **File format:** save and load are symmetric. A 0, negative or non-number value is rejected with `KCD2-D002`, and a missing key falls back to the default; both are tested. Both samples were regenerated, `v2_sample_document_tests` passes, and `kcd2-format.md` §19.3 matches the code.
- **Boundaries:** V1 is unchanged, no Qt or OCCT dependency was added under `src/next`, and there are no untracked files. The extra files outside the task's file list all follow from the work: persisting the tolerances, sharing the curve-distance code, docs and samples.
- **Build and test evidence:** the attempt 3 gates ran after the last source edit (`SnapEngine.h`, 16:21:58).
  - Configure passed (16:23:08).
  - Build passed at 16:25:04. Its only two warnings are in `V2ViewportDraw.cpp:537` and `V2Viewport.cpp:1119`, which this diff does not touch.
  - CTest passed 129/129.
  - The app self-test passed 168/168.

  The worker report's Build and Test sections still quote attempt 2; the attempt 3 logs above supersede them.

**Not blocking, for Stage 2 or the owner**
1. **Speed on every pointer move:** closest-point snapping now samples each curve at `interactiveJoinMm` (0.01 mm) instead of length/200. For a circle of radius 100 mm that is about 17 times more samples, and there is no early exit for curves outside the radius. §13 says hover must not slow a normal frame, and this has not been measured.
   - Suggested fix: skip spans whose lower bound is beyond `snapPickPx + holdMarginPx`.
   - Add the 10,000-wire performance test.
2. **Rules the specs don't define:**
   - Closest-on-curve is dropped when a perpendicular or tangent point on the same curve is in range. Without this, the §6.1 order would make those points impossible to pick.
   - The extension and project-to-plane snaps are ranked below perpendicular/tangent and above the grid.

   Both are written down in `v1-drawing-parity.md`, but the owner should confirm them.
3. **Not implemented yet:** stronger snap for candidates suited to the current tool (§6.1) and ranking the tool's required type first (§4.3 item 1, PRD-060). This is recorded honestly in the `SnapEngine.h` header, and no rule was invented.
4. **Grid:** with a 12 px radius, free placement on the plane becomes impossible when grid points are about 17 px or less apart on screen.
5. **Stage 2 work:**
   - Connect `SnapHysteresis` to `DrawingSession.cpp:81-83`, which still calls `ChooseSnap` without it.
   - Call `Reset` on tool switch and cancel.
   - Add the `S`-key self-test in `V2SelfTestInput.cpp`.
6. **Stale comment:** `V2SelfTestScreen.cpp:544` still says lines are hit at 8px; it is now 6px. The test's 16 px margin still holds.
7. **Two plane checks:** the new `geometry::CurveLiesInPlane` and the existing `app::CurveLiesOnPlane` (`PlaneFocus.cpp`) answer the same question and should be merged later.
