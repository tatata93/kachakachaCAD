REVISE

1. **Perpendicular and tangent snaps can no longer be chosen**
   - **Where:** `src/next/kachakacha/modeling/SnapEngine.cpp:95-111` (`SnapPriorityRank`: ClosestOnCurve is rank 3, Perpendicular/Tangent rank 4), together with `CollectSnapCandidates` at `:175-197` and `:211-220`. The app hits this through `src/next/kachakacha/app/DrawingSession.cpp:76-83`.
   - **Problem:**
     - A ClosestOnCurve candidate is added for every curve within 12px of the pointer.
     - A perpendicular foot or tangent point always lies on that same curve. The ClosestOnCurve sample is always at least as close to the pointer, give or take one sampling step (length/200).
     - Candidates of different ranks are no longer compared by distance. So whenever a foot or tangent point is within 12px, ClosestOnCurve from the same curve is also within 12px and always wins.
     - Before this diff, Perpendicular (enum 5) beat ClosestOnCurve (enum 10), so these snaps worked. `DrawingSession::Hover` sets `referencePoint` after the first placed point, so the app is affected now, not only after Stage 2.
     - Even with the pointer exactly on the foot, the chosen snap is a sampled point on the curve up to half a sampling step away (about 2px on an 80mm line at 10px/mm). The placed coordinate is not the real perpendicular foot.
     - No test catches this. `tests_v2/snap_tests.cpp:606-623` ("基準点があれば垂足が出る") only checks `HasKind` and never calls `ChooseSnap`. The self-test does not exercise it either.
   - **Why it matters:**
     - A snap type listed in §6.1, `geometry-contract.md:120` and `v1-drawing-parity.md:96` silently stops working.
     - Following the §6.1 list literally makes item 5 impossible to reach. §6.1 ("現在ツールに適合する候補で強さを変える") and §4.3 item 1 ("入力中ツールが要求する型") say a candidate the current input asks for should win.
     - The worker deferred this as "Stage 2", but Stage 1 already changed the stateless ranking the app uses today. Check 8 (focused regression tests) is not met.
   - **Required fix:**
     - Add tests that call `ChooseSnap` (stateless and through `SnapHysteresis::Resolve`) with a reference point set and the pointer at a perpendicular foot and at a tangent point. Assert that the chosen kind is Perpendicular/Tangent and the position is the exact foot or tangent point.
     - Then make them reachable without inventing behavior. For example, rank reference-point-derived Perpendicular/Tangent above ClosestOnCurve per §4.3 item 1. Or have ClosestOnCurve give way to an on-curve Perpendicular/Tangent from the same entity/segment within `interactiveJoinMm`.
     - If the owner has to choose between these, stop and escalate the §6.1 conflict. Do not ship the regression.

2. **Invalid values for the new tolerance keys are accepted without an error**
   - **Where:** `src/next/kachakacha/io/DocumentFileRead.cpp:689-698` and the rule at `:704-707`. `docs/v2/kcd2-format.md` §19.3, edited in this diff.
   - **Problem:**
     - `edgePickPx` and `snapPickPx` accept 0, negative or wrong-typed values (a string is silently ignored).
     - `snapPickPx: 0` or a negative value turns off every snap for that document. `edgePickPx <= 0` makes lines impossible to pick. Nothing is reported.
     - These keys are new in this diff, and §19.3 was rewritten here without saying what happens to invalid values.
   - **Why it matters:**
     - A damaged or hand-edited `.kcd2` quietly changes core interaction, which breaks check 9 (errors must not be swallowed).
     - The reason given ("`displayPickPx` already behaves this way") copies an existing gap onto new, persisted fields.
   - **Required fix:**
     - When a key is present, reject a non-number or a value that is not `> 0` with `kBadValue` at `$.tolerances`, the same way the other four tolerances are handled. Omitted keys still fall back to the defaults.
     - Update §19.3 to say this.
     - Add rejection cases to `tests_v2/document_file_tests.cpp` ("点と線とスナップの拾い半径を別々に読み戻す" or a sibling test).

**Checked and fine:**
- **Gates:** they ran after the last edit (11:09:20); configure, build (two unrelated warnings), CTest 129/129 and self-test 168/168 all passed.
- **Samples:** the `.kcd2` changes only add `"edgePickPx": 6` and `"snapPickPx": 12` to `document.json`.
- **Enum order:** nothing saves or displays `SnapKind` as an integer, so reordering it is safe.
- **Lifetimes:** `Collector` now holds references to `settings` and `tolerance`, and both outlive it.
- **Hysteresis:** the held/rank takeover logic is correct.
- **Removed API:** nothing still refers to it.
- **Boundaries:** `src/next` still has no Qt or OCCT.

**Owner confirmation still needed (not blocking this Stage):**
- `holdMarginPx = 4`
- The Extension/ProjectedOnPlane rank and the FreeOnPlane rank
- The stale Ctrl comment at `V2ViewportInput.cpp:7`
