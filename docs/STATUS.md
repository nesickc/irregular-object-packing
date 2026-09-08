# C++ Implementation Status

Last updated: 2026-09-08

## Current Summary

Milestones 1 through 5 are verified. The C++20 library and thin CLI now provide hardened STL inspection, deterministic bounded initialization, project-owned TetGen/CAT/Ipopt boundaries, strict collision and containment validation, and a bounded end-to-end packing coordinator. `irop pack` composes exact scale barriers, optional adaptive VTK resampling, deterministic per-object local solves, bounded correction/termination, and atomic success or summary-only failure publication by default; explicit diagnostic capture may add one numeric failed-solve snapshot. Already-satisfied barriers complete before sampling or dependency work while retaining mandatory full-resolution and serialized-output validation. Success is gated on the exact float32-coordinate meshes written to binary STL. Public APIs remain free of Ipopt, TetGen, VTK, and Eigen types. GL2PS `1.4.2#5` remains resolved from the package-scoped official vcpkg registry without changing the reviewed default dependency baseline. Milestone 6 is implemented and its local acceptance gates pass: a live pinned Python 3.10 oracle and the C++ parity corpus cover initialization, dense-search exhaustion, transforms, CAT, local mathematics, outcomes, metrics, and artifacts; rare correction/recovery and real Windows Ctrl+Break cancellation paths are exercised; and source-release notice/CI tooling is checked in. The first hosted workflow run has not yet been observed, so Milestone 6 remains `Implemented` rather than `Verified`.

Current milestone: Milestone 8 — Basic Visualization UI is Verified under ADR-0014. Native Debug/Release and Ninja clang-tidy builds, affected test suites, real desktop workflows and the default option-off CLI build pass, with one documented account-dependent symlink test skipped in each matrix. Milestone 7 remains Verified for its authorized first measured scope; Milestone 6 hosted CI acceptance remains pending.

Next outcome: the requested [improvement plan](IMPROVEMENT_PLAN.md) prioritizes automatic run folders and diagnostics, reliable full-size growth, measured 100-300-object workloads, then 1,000 objects. Tranche 1 is Verified under ADR-0015. Tranche 2 is Verified under ADR-0016: three of three supplied ten-object genuine-growth runs reach exact target, pass both validation gates and load successfully within 300 seconds. Tranche 3 is Verified for the registered known-fit 100/300-object scope under ADR-0017; heavier supplied meshes retain the documented collision-work limit. Tranche 4 remains planned. Hosted CI acceptance remains a parallel Milestone 6 item; public distribution remains separate under ADR-0009/0010.

## Status Vocabulary

- `Not started`: no implementation exists.
- `In progress`: implementation exists but milestone acceptance is incomplete.
- `Implemented`: code exists and focused verification passes.
- `Verified`: milestone-level acceptance and integration evidence pass.
- `Blocked`: progress requires an external decision or unavailable prerequisite.
- `Deferred`: explicitly outside the current implementation horizon.

## Milestones

| Milestone | Status | Evidence | Next gate |
| --- | --- | --- | --- |
| 0. Baseline and agent onboarding | Verified | `AGENTS.md`, project documents, ADRs, validated `develop-irop-cpp` skill, and clang-format 22.1.3 profile validation | Begin build foundation |
| 1. Build foundation and STL vertical slice | Verified | Fresh official-registry resolution of GL2PS `1.4.2#5`; Visual Studio 2026 Debug and Release builds with 30/30 CTest cases in each; Ninja clang-tidy build and 30/30 tests; format gate; ASCII/binary STL and Unicode CLI smoke coverage through 2026-08-22 | Begin Milestone 2 |
| 2. Transforms, sampling, and initialization | Verified | 2026-08-22: unit/geometry/RNG Python-golden/schema/CLI coverage; atomic initialized STL/JSON artifact set; scoped-registry Visual Studio Debug and Release builds plus Ninja clang-tidy build, each passing 80/80 tests; format gate | Begin Milestone 3 |
| 3. Tetrahedralization and CAT constraints | Verified | 2026-08-22: exact TetGen 1.6.0 AGPL overlay and private serialized adapter; literal Python `O0/0Q` compatibility path; all Python split goldens; ownership/range/normal/limit/error regressions; VTU/VTP round trips; Visual Studio Debug/Release and Ninja clang-tidy builds passing 102/102 tests plus format gate | Begin Milestone 4 |
| 4. Local nonlinear optimization | Verified | 2026-08-22: exact official Ipopt 3.14.19 binary overlay with MUMPS and private C adapter; Python-golden objective/constraints; exact analytic Jacobian; project-owned statuses and bounded reusable workspace; solver-space and actually applied transform postchecks; hostile `ipopt.opt`, invalid-input, infeasible, limit, and seven-variable regressions; Visual Studio Debug/Release and Ninja clang-tidy builds passing 114/114 tests plus format gate; analytic derivative benchmark about 8.8x faster than forward differences at 2,048 constraints | Begin Milestone 5 |
| 5. End-to-end packing CLI | Verified | 2026-08-22: genuine STL-to-TetGen/CAT/Ipopt growth; strict full-resolution and binary-STL-quantized validation; atomic four-artifact success and summary-only cancellation/resource/infeasible outcomes; clean pre-input/pre-state cancellation; conditional JSON schemas; Visual Studio Debug/Release and Ninja clang-tidy builds passing 154/154 tests plus format and schema gates | Begin Milestone 6 |
| 6. Compatibility, robustness, and release readiness | Implemented | 2026-08-28 and 2026-09-05 local evidence: live pinned Python oracle; seven-case/403-assertion C++ parity run; rare correction/recovery and Windows Ctrl+Break process coverage; 174 Catch2 plus four process tests, 178/178 under Visual Studio Debug, Visual Studio Release, and Ninja clang-tidy; Python 141 passed/1 skipped plus Ruff; exact 39-package notice audit, package-build validation, clean-checkout smoke, and adaptive profile | Observe the first hosted workflow run before marking the milestone Verified |
| 7. Measured scalability improvements | Verified | 2026-09-05: 11 benchmark cases with three before/after process repeats each; dense ten/36-cylinder structured recovery; unchanged successful reference seeds; strict AABB collision filtering and cumulative pair limits; focused 19-case/746-assertion coverage; Visual Studio Debug, Release and Ninja clang-tidy each pass 192/192; live Python parity oracle passes; actual supplied STL ten/36-object pack runs succeed | Authorized first measured scope complete; further optimization requires new measurements |
| 8. Basic visualization UI | Verified | 2026-09-05: optional native Win32/private-VTK irop_studio and bounded load_run_scene; Debug/Release/Ninja each complete 203 tests with 202 passed, one symlink skip and no failures; zero clang-tidy diagnostics and format pass; six desktop smoke cases pass in Debug and Release; native interactions, real 36-object and unsuccessful saved runs, and option-off CLI build verified | Authorized basic Windows UI scope complete; retain documented environment limits and select further UI work separately |

## Module Status

| Area | Status | Notes |
| --- | --- | --- |
| Project documentation | Verified | Baseline documents, BSD policy, formatting policy, and ADR process established |
| Agent development skill | Verified | Repository-scoped skill created, structurally validated, and synchronized with the formatter policy |
| C++ formatting profile | Verified | Root `.clang-format` accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| CMake and presets | Verified | Target-based CMake, preset-derived fresh Visual Studio and Ninja configurations, warnings, formatting, and CTest all exercised with GL2PS `#5` |
| vcpkg manifest and overlay ports | Verified | Default baseline pinned; GL2PS-only official registry pinned; narrow VTK/MSVC overlay active; patchless TetGen 1.6.0 overlay hash-verified; official Ipopt 3.14.19 MD/MDD archives hash-pinned in an x64-windows binary overlay with exact runtime-closure checks |
| CLI | Verified | Thin `irop inspect`, `irop initialize`, and `irop pack` commands expose bounded controls, Unicode Windows arguments, atomic artifact sets, cooperative SIGINT through input preparation, initialization, engine work, and successful pre-commit publication, one-based progress, and stable exit categories 0/2/3/4/5/6/70/130; on Windows, SIGBREAK/targeted Ctrl+Break reaches the same cancellation flag and a real child-process test verifies exit 130 with summary-only publication; an already-final unsuccessful engine outcome remains authoritative |
| Domain model | Verified | Project-owned mesh, transform, packing state/configuration/result/progress/history/work/validation, tetrahedralization, CAT, local-solve, collision, resampling, and error types cover the complete Milestone 5 boundary |
| STL I/O and mesh validation | Verified | ASCII/binary preflight, checked resource boundaries, VTK isolation, post-load validation, and exact binary-STL float quantization are covered; the packing success gate validates the same representation the writer serializes |
| Geometry operations | Verified | Existing transform, containment, surface-intersection, and nesting contracts remain covered. Milestone 7 skips narrow collision work only for strictly separated validated AABBs, preserving contacts, nesting, and pair order; every enumerated object pair is bounded cumulatively across correction, engine-final, and binary-STL output validation. Debug, Release and Ninja clang-tidy regressions pass at 192/192 |
| Initialization and sampling | Verified | Successful origin/random paths preserve Python transforms, NumPy MT19937 draw order, and work. After candidate-attempt exhaustion only, a bounded six-orientation centered grid uses strict actual containment and independent envelope separation with shared work budgets and cancellation. Ten/36-cylinder, concave/shifted-container, forged-state, limit, and reference-parity regressions pass; --no-initialization-fallback preserves historical bounded failure. Debug, Release and Ninja clang-tidy pass at 192/192; live Python parity oracle passes |
| Tetrahedralization | Verified | Private TetGen 1.6.0 adapter preserves ordered ownership and the reference `O0/0Q` point-union cell complex, validates closed participants/output invariants, serializes process-global backend state, bounds accepted counts, and translates failures |
| CAT construction | Verified | Four-owner, 2+1+1, 3+1, and 2+2 Python goldens; relevant-cell filtering; participant-contiguous polygons/constraints; inward unit-normal and coplanarity invariants; malformed-input and exhaustion coverage |
| Nonlinear optimization | Verified | Private Ipopt 3.14.19 C adapter selects MUMPS, disables ambient option files, evaluates the seven-variable incremental problem with an exact dense Jacobian, left-composes accepted rotations in constraint-model order, translates every backend outcome, bounds project-controlled work, and publishes only independently postchecked transforms |
| Packing engine | Verified | Exact barriers with overflow-safe finite local scale caps and independently checked near-target snaps, pre-work completion of already-satisfied barriers without transform/RNG/history mutation, adaptive resampling, TetGen/CAT rebuilding, deterministic transactional per-object solves, run-owned RNG continuation, bounded recovery/correction/termination, history/work metrics, and mandatory full-resolution final validation pass representative integration coverage |
| Collision correction | Verified | Deterministic fixtures prove selective object/container reduction to convergence, bounded correction-limit accounting, reduction of both members of an object/object overlap, and zero correction for CAT-only contact under COMPAT-0002; full-resolution and output-quantized validation still gate success |
| Result serialization | Verified | Success artifacts retain their physical and serialized-output acceptance gates. Failures remain summary-only by default; explicit capture may add failed-local-solve.json under ADR-0015. Optional version-one diagnostics/timing fields preserve historical records. Captured real-input summary/snapshot and all six benchmark summaries validate against their schemas; snapshot replay needs no source meshes. |
| C++ tests | Verified | Tranche 3 Debug, Release and Ninja each complete 253 tests with 252 passed, one skipped and no failures. Added coverage includes exact Hessians, current/reference growth, adaptive and physical-step recovery, CAT diagnostic bounds and interrupted-report metadata, malformed inputs and genuine-growth/non-fit fixtures. The saved-artifact symlink test remains skipped because this account cannot create file symlinks. |
| CI and static analysis | Implemented | The source-only Windows workflow enables optional UI and benchmark targets alongside C++/Python/parity/notice gates. Tranche 3 local Debug/Release/Ninja builds and 253-test matrices pass (one account-dependent skip); final Ninja clang-tidy has zero diagnostics and the formatter passes. The real desktop/OpenGL smoke is separate from headless CTest. First hosted workflow acceptance remains pending. |
| Benchmarks | Verified | The earlier Milestone 7 results remain in benchmarks/results/windows-20260905.json. Tranche 1 extends the opt-in harness to real-STL packing/export/loading through count 1,000 with source/input hashes, stage time/work and peak memory. The retained tranche-1 genuine-growth baseline fails 3/3; tranche 2 succeeds 3/3 with exact targets, both physical gates, export and loading, at 221.49 seconds median total. The tracked tranche-2 ledger records hashes, limits, all samples and source provenance. Tranche 3 adds three successful full-growth repetitions each at 100 and 300 known-fit objects, with medians 33.484 and 125.446 seconds; the heavier supplied100 failure remains documented below. No 1,000-object throughput is claimed. |
| Saved-run loading | Verified | Project-owned load_run_scene reads the display contract of local version-one pack/initialize summaries and fixed sibling artifacts, with bounded JSON/aggregate mesh input, canonical checks, cancellation and structural validation. Recorded source/individual-STL paths are not followed and recorded physical validation is not recertified. Debug/Release/Ninja hostile-input/relocation/worker tests pass except the documented symlink privilege skip |
| Visualization UI | Verified | Studio now remembers a Runs parent and reserves a fresh numbered child for every dispatch, including retries and restarts; Open result folder and an independent completed-result path are available. Preference-save failures retain a usable session parent with a warning. Thirteen actual desktop cases pass in Debug and Release, with inspected captures and distinct next-run/saved-run growth labels. The supplied ten-object genuine-growth result loads and renders successfully. One worker owns background preparation/loading/packing; VTK and controls remain on the UI thread. |

## Known Compatibility Work

The catalog in `docs/COMPATIBILITY.md` is synchronized through `IROP-COMPAT-0007` and `IROP-DEV-0037`. ADR-0017 adds scoped numerical thread control and bounded immediate-retry reuse; their registered tranche-3 acceptance gates pass. ADR-0016 and DEV-0028 through DEV-0035 add the measured current-pose/exact-derivative, sampling, diagnostic and bounded physical-retry policies summarized below; the explicit reference-growth switch preserves earlier growth policies. ADR-0012 retires COMPAT-0001's unbounded solve-then-clamp path and COMPAT-0006's additive Euler update after practical failures, replacing them with exact left-composed rotations and overflow-safe barrier-bounded scale solves with independently checked exact-target snapping. DEV-0024 prevents redundant TetGen/CAT/Ipopt work from degrading an already-complete barrier while retaining both final validation gates. DEV-0007 bounds the reference initializer's greedy search without claiming that attempt exhaustion proves infeasibility. Its accepted prefix, work, and timeout behavior have live Python/C++ parity evidence; DEV-0026 now adds a bounded structured restart after candidate-attempt exhaustion, while the explicit disable option preserves the historical failure oracle. DEV-0027 bounds enumerated collision pairs after AABB rejection removes their narrow-phase work. COMPAT-0002 and the recovery/correction bounds in DEV-0001 and DEV-0004 now have targeted rare-path fixtures. DEV-0025 omits an unsupported, default-valued PyVista 0.38.4 keyword so the pinned Python oracle can exercise the live reference surface extraction without changing its behavior. Barrier-multiplied translation bounds remain the base policy; default full-input physical retries can reduce them within their original envelope.

## Milestone 7 Evidence

- Visual Studio Debug, Release and Ninja clang-tidy each pass 192/192 tests
  (188 Catch2 plus four process tests). Ninja analysis builds with zero diagnostics;
  its CTest run takes 14.26 seconds. The focused selection passes 19 cases/746
  assertions, and the pinned live Python parity oracle passes again on 2026-09-05.
- The final `irop-format-check` target and `git diff --check` pass. The Ninja
  benchmark smoke reports a physically valid success with 45 pairs and 5,280
  triangle tests; measured initializer/collision source hashes still match.
- The [recorded measurement matrix](MILESTONE_7_RESULTS.md) contains 11 cases with
  three cold-process repeats for each version. Dense ten/36-cylinder initialization
  changes from bounded failure to success after the same million reference
  attempts, at roughly 278 ms; sparse placements, RNG, and work remain exact.
- The separated 100-object collision case falls from 144.0279 to 1.1271 ms median
  total time and from 9,636,000 to 52,800 triangle tests. Pair enumeration remains
  4,950; this does not establish a speedup for dense collision or general solving.
- The supplied STL pair packs ten and 36 full-scale objects successfully in
  `build/manual-m7-exact-ten` and `build/manual-m7-exact-36`. Initialization of ten
  objects also succeeds with 100 reference attempts. Disabling the fallback
  restores the six-of-ten bounded failure, exit 4, and no output directory.

The grid uses six fixed orientations, conservative envelopes and centered cell
lattices. It is not complete search: feasible irregular/concave scenes can still
exhaust candidate or shared geometry budgets. Collision pair enumeration remains
quadratic and bounded. Dense growth still encounters the existing TetGen/solver
limits; aggregation, regional subdivision and concurrency remain future work.

## Milestone 8 Implementation and Evidence

`irop_studio` is implemented under `IROP_BUILD_UI=ON`. It provides object/container
STL pickers and centered/scaled preview, count/scales/steps/seed/timeout/adaptive
and structured-fallback controls, a new output directory, progress/cancellation,
orbit/pan/zoom and fit/axis views, visibility/wireframe, and reopening saved runs.
One background worker performs input preparation, `pack_scene` and saved loading;
VTK and Win32 updates stay on the UI thread. Closing requests cancellation and
defers destruction until the worker is terminal and joined. Active dependency
operations remain non-preemptible. No packing algorithm or compatibility policy
changes; no new compatibility identifier is required.

The project-owned [`load_run_scene`](../include/irop/io/run_scene.hpp) accepts the
display contract of version-one pack/initialize summaries and summary-only
packing diagnostics. It reads fixed sibling geometry after canonical checks,
never recorded original inputs or individual STL paths. Defaults bound summary
bytes to 16 MiB, JSON depth to 32 and nodes to 250,000; both display meshes share
128 MiB, three million vertices and one million triangles. Loaded geometry is
structurally validated for display. Saved physical validation is labeled as a
recorded result; initialization summaries have no such record.

Confirmed 2026-09-05 evidence:

- Supported Visual Studio Debug, Release and Ninja clang-tidy builds succeed.
  Each complete CTest run has 202 passed, one skipped and no failures out of
  203 tests: Debug takes 14.07 seconds, Release 9.23 and Ninja 10.04. The focused
  loader/worker selection has 10 passed, one skipped and 186 assertions across
  11 cases. The canonical symlink-escape test is skipped because this Windows
  account cannot create file symlinks; that branch has not run here.
- The final Ninja build emits zero clang-tidy diagnostics. The authoritative
  `irop-format-check`, workflow YAML/PowerShell-script parsing and
  `git diff --check` pass. A fresh option-off configuration and Release `irop`
  build succeed in `build/windows-vs2026-m8-cli-only`.
- `tests/cpp/studio_smoke.cmake` passes all six real desktop cases in
  `build/studio-smoke-release-final` and `build/studio-smoke-debug`: preview,
  genuine `0.1 -> 0.2` packing, reopen, cancellation, close during active work,
  and malformed saved input.
- Native events exercise mouse orbit, fit/X/Y/Z views, wireframe/container
  toggles and resizing, and check that `q/Q/e/E` cannot terminate the application.
  `viewport.png`, `orbit.png`, `wireframe.png` and `window.bmp` record rendered
  results. Whole-window visual inspection confirms clear controls without
  clipping, including long output paths that scroll to their tail.
- The actual supplied 36-object full-scale run opens and renders successfully
  in `build/studio-real-36`; the final Ninja application repeats it in
  `build/studio-real-36-final`, with an inspected `studio-window.png` capture.
  `build/studio-unsuccessful` opens an existing two-object `resource_exhausted`
  summary and displays diagnostics without rendering success geometry.
- The Computer Use runtime could not launch its sandbox helper, so the evidence
  is the application's real native desktop exercise and inspected captures.
  Manual interaction with the Windows file-picker dialogs was not automated;
  native dialog integration exists and worker coverage includes Unicode paths.
- CI enables both `IROP_BUILD_UI` and `IROP_BUILD_BENCHMARKS`; loader/worker CTest
  coverage requires no UI renderer. The desktop/OpenGL smoke remains a separate
  local check, not a claim of hosted GPU interaction coverage.

Milestone 8 is Verified for the authorized basic Windows UI scope with the
explicit environment limitations above. See the [Studio quickstart](STUDIO_QUICKSTART.md)
for build, workflow and smoke commands, and
[ADR-0014](adr/0014-native-windows-visualization-ui.md) for the boundaries.

## Supplied ten-object growth diagnosis

On 2026-09-05, the user's Studio run with `ulamok_2kg_simplified.stl`,
`10_kg_np.stl`, ten copies, seed 1918, initial/target volume scales `0.1`/`1.0`,
nine steps, adaptive sampling on, fallback off and a 300-second engine timeout
failed after 20.64 seconds. Random initialization accepted all ten in 23 attempts.
The nineteenth local solve exhausted its 1,000-iteration cap during the second
engine iteration at the first `0.2` barrier. The 2.64% occupancy describes the last
committed partial state; final physical validation never ran. The summary does
not identify the underlying numerical reason for nonconvergence.

The current Release executable reproduced the same outcome, 19 solves, 4,499
aggregate Ipopt iterations and identical metrics in 22.29 seconds; evidence is
`build/diagnose-user-ten-original-20260905/run-summary.json`. Disabling adaptive
sampling alone, with a 60-second diagnostic engine cap, still reached the local
iteration limit after four solves and 49.50 seconds in
`build/diagnose-user-ten-noadaptive-20260905/run-summary.json`.

The same meshes/count/seed with initial and target scale both `1.0`, one step,
adaptive sampling off and structured fallback on succeeded in
`build/diagnose-user-ten-fullsize-20260905/run-summary.json`. Initialization took
0.224 seconds; every object has exact full size, occupancy is 16.97%, both physical
validation gates pass, and all four artifacts are published. It performs zero
local solves. The [Studio guide](STUDIO_QUICKSTART.md) records the usable settings.
This workaround leaves genuine `0.1` to `1.0` growth robustness unresolved; the
verified milestone fixtures do not establish convergence for this configuration.
This investigation changed documentation only, with no solver or acceptance
policy changes and no claim of a new full-suite run.

## Tranche 1: Repeatable runs and bounded diagnostics

Verified on 2026-09-05 under [ADR-0015](adr/0015-repeatable-runs-and-bounded-diagnostics.md).
Studio defaults to `%LOCALAPPDATA%\IROP\Runs`, remembers a chosen parent and
allocates `run-000001`, `run-000002`, etc. Exclusive reservations and a bounded
sequence ledger preserve names across concurrent instances, cancellation, failures
and restart. Actual completed output remains separately visible. Unusable settings
produce a warning while the session parent remains usable; unsafe metadata is
preserved. The CLI retains its exact requested destination and now prepares private
staging before input loading, with authoritative no-overwrite publication afterward.

Failure summaries identify the object, barrier, engine iteration and local/global
limits. Bounded TetGen records preserve backend causes; optional local records and
numerical traces report dropped samples. Explicit capture can publish one bounded
`failed-local-solve.json` sibling. Numeric-only replay uses the same validated
adapter and does not certify a complete packing. Solver defaults, RNG draws,
accepted transforms and physical/output-validation gates are unchanged; no new
Python compatibility deviation is introduced.

Verification evidence:

- Supported Visual Studio Debug/Release and Ninja analysis configurations build
  with UI and benchmarks enabled. Each full CTest run has **219 passed, one skipped,
  zero failures out of 220**. Logs are `build/t1-debug-ctest-final.log`,
  `build/t1-release-ctest-final.log` and `build/t1-ninja-ctest.log`.
  Final analysis (`build/t1-ninja-build-final-pass.log`) has no clang-tidy diagnostics;
  `irop-format-check` and `git diff --check` pass. The existing saved-artifact
  symlink test still needs a suitably privileged account.
- All **11 actual desktop smoke cases** pass in each of
  `build/studio-tranche1-release-verified` and
  `build/studio-tranche1-debug-verified`. They cover rerun after success, cancellation
  and input failure, restart persistence, and two successful runs despite unusable
  settings. Inspected window captures show the next number, settings warning and
  actual completed path. Native file-picker interaction remains a manual gate.
- The exact supplied ten-object `.1 -> 1.0` configuration is retained at
  `build/tranche1-real10-capture-20260905/run-summary.json`. It still performs
  19 solves and 4,499 aggregate Ipopt iterations, then fails on object 9 (ID 8),
  engine iteration 2, scale step 1 at the `0.2` barrier. Its metrics and all
  non-time local-solve work match the earlier reproduction. The 173,172-byte
  snapshot has 1,012 constraints. Replay independently reaches the same
  1,000-iteration limit in about 4.3 seconds; all 128 retained trace samples match
  exactly (`build/t1-real10-replay.log`). The captured summary and snapshot pass
  Draft 2020-12 schema validation. Test coverage also deletes original meshes
  before replay and verifies default summary-only publication.
- Real-STL benchmark reports are retained in `build/tranche1-benchmark-real10-direct`
  and `build/tranche1-benchmark-real10-growth`, three independent processes each.
  Direct full-size placement validates, exports and loads **3/3**, with median
  measured total 241.4 ms and zero local solves. Original genuine growth fails
  **3/3** with median measured total 21.86 s, including 21.74 s in local solves;
  final physical/output validation never runs. Failure summaries load as diagnostics
  with no geometry. The requested `[10, 100]` ladder stops after the ten-object
  failures. All six canonical summaries validate. Reports retain source/input
  SHA256, resolved settings, machine/dependencies, stage timings and process memory.
  These are baseline measurements, not a speedup or larger-object acceptance claim.

The exact capture/replay commands and benchmark reproduction are below; all output
paths must be fresh. Measurements use separate processes with hash-warmed file
caches. Application/engine stages are nested, not additive totals, and stage CPU
is unavailable where reported as null.

```powershell
./build/windows-vs2026/Release/irop.exe pack `
  --object rc/input_models/ulamok_2kg_simplified.stl `
  --container rc/containers/10_kg_np.stl --count 10 --seed 1918 `
  --initial-volume-scale 0.1 --final-volume-scale 1 --scale-steps 9 `
  --no-initialization-fallback --max-elapsed-milliseconds 300000 `
  --capture-failed-local-solve --local-solve-records 64 `
  --local-solve-trace-records 128 --output build/tranche1-reproduce
./build/windows-vs2026/Release/irop.exe replay-local-solve `
  build/tranche1-reproduce/failed-local-solve.json --trace-records 128
./benchmarks/run-stl-windows.ps1 `
  -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe `
  -Object rc/input_models/ulamok_2kg_simplified.stl `
  -Container rc/containers/10_kg_np.stl -Counts 10,100 -Repeats 3 `
  -DisableInitializationFallback -OutputDirectory build/tranche1-baseline
```

This is the retained tranche-1 failure baseline; the tranche-2 section below records
its resolution and the controlled replay evidence.
The harness accepts count 1,000 under production limits; successful complete
100/300/1,000-object growth remains the acceptance work of later tranches.

## Improvement Tranche 2 — Verified (2026-09-06)

The supplied ten-copy, seed-1918 `.1 -> 1.0` run now completes all nine barriers
with every object at exact target scale. The accepted run at
`build/tranche2-real10-step-retry-experiment` takes **265.819 seconds** in the engine
under the original **300-second** deadline, occupies **16.967%** of the container,
and passes both full-resolution and serialized float32 physical validation. It
uses adaptive sampling, disables structured initialization fallback and performs
328 local solves, five sampling refinements and 17 bounded physical step retries.
It is genuine growth, not full-size initialization. Studio loads and renders its
published geometry (`build/studio-tranche2-real10-open`).

[ADR-0016](adr/0016-current-pose-exact-derivatives-and-growth-recovery.md) and
DEV-0028 through DEV-0035 record the selected policy:

- Start from the current pose, retain completed objects, use an exact barrier
  bound and analytic Hessian, and keep independent local/applied checks.
- Refine the object approximation after correction. Preserve container surfaces
  with double midpoint subdivision and retain their initial sampling across
  barriers, instead of smoothing the container or escalating its triangle count.
- Ignore only guarded, irrelevant single-participant TetGen cells whose tiny
  determinants round to zero. Standalone/reference behavior remains strict.
- Retry rejected full-input physical trials with smaller bounded motion and
  absolute growth increments, up to four levels per object/barrier. Rejected
  transforms/RNG are discarded; all attempted work remains counted. Existing
  scale correction remains the fallback and no failed local iterate is accepted.
- Bound CAT diagnostics separately at ten million triangle pairs and report
  incompleteness explicitly. Physical checks use a one-billion cumulative engine
  allowance; standalone scene queries retain 100 million. This measured ceiling
  change was recorded before final tuning; local limits and the 300-second engine
  envelope remain unchanged. The accepted run uses 762.6 million physical pairs.

The original failure replay has 422 violated constraints at its random/small-scale
start and none at identity; changing only that start resolves the isolated
1,000-iteration failure in 80 iterations. Exact derivatives independently match
finite differences. Exact-Hessian replays resolve further recorded stalls, and
an exact barrier bound avoids a demonstrated post-solve clamp failure. Subsequent
full-run experiments expose coarse-surface cycling, rounded internal TetGen cells,
a smoothed box retaining only 37.14% of its volume, final-barrier oversampling to
196,608 container triangles, and repeated physical shrink/regrowth cycles. Failed
experiments remain in `build/tranche2-*`; their evidence motivates the bounded
policies above rather than relaxed acceptance or an increased time limit.

Verification:

- Visual Studio Release/Debug and Ninja clang-tidy builds with UI/benchmarks enabled
  each pass **243 tests with one existing symlink skip out of 244**. Logs are
  `build/t2-release-ctest-final.log`, `build/t2-debug-ctest-final.log` and
  `build/t2-ninja-ctest-final.log`. Final analysis reports no diagnostics;
  `irop-format-check` and `git diff --check` pass. The symlink test still needs a suitable account.
- The generated corpus covers asymmetric shapes at seeds 0/1918/12345, slender
  shapes at 0/1918, rotation-required growth, regular-point recovery/reference
  failure, and a volume-certified bounded non-fit with no geometry publication.
  Adaptive and concave four-object fixtures exercise real sampling/step recovery,
  exact target completion, physical validity and cancellation retaining committed
  transforms/RNG. Hessian, unsafe-input and cumulative-work regressions pass.
- **13 actual desktop cases pass in each Debug and Release build**, including
  repeated numbered runs, failure/cancellation/restart, current versus recorded
  growth, and full/reduced direct placement. Captures were inspected, including
  the real ten-object result. Native file-picker interaction remains a manual gate.
- Historical replay reproduces all **128 trace samples exactly** and the original
  1,000-iteration outcome. A new bounded exact-Hessian failure captures and replays
  with one iteration and no packed geometry. Schema checks cover historical and
  current summaries/snapshots, placements, optional fields and invalid types.
  Evidence: `build/t2-replay-validation.json`, `build/t2-schema-validation-final.json`.
- Three independent Release benchmark processes at
  `build/tranche2-benchmark-real10-growth` succeed **3/3**, with exact targets,
  both physical gates, export and saved-run loading. Total times are **216.343,
  221.492 and 227.274 seconds**; engine median is 221.385 seconds. All three have
  identical placements, serialized geometry bytes and work. Peak working-set median
  is 69.75 MiB and peak commit median is 152.33 MiB. The
  [tracked ledger](../benchmarks/results/windows-20260906-tranche2.json) retains
  all samples, stages, source/input hashes, resolved limits and the original 0/3
  growth-failure baseline. Local solves take 207.97 seconds median (94% of engine
  time); dependency thread defaults remain unchanged. No speedup against an early
  failed run is claimed.
- Final review fixes one failure-metadata edge: a CAT-bearing physical query that
  throws before returning its report conservatively marks CAT diagnostics
  incomplete while preserving the failure and committed state. Its focused test
  passes 30 assertions across default/reference policies; all three matrices were
  rerun afterward. The ledger distinguishes the measured binary's source hash
  from this later reporting-only edit; successful growth policy is unchanged.

These tranche-2 measurements alone do not establish 100/300/1,000-object throughput.
The current corpus establishes bounded recovery and this supplied ten-object case;
it does not guarantee that every geometry, density or seed will converge.

## Improvement Tranche 3 — Verified for the registered 100/300 scope (2026-09-07)

[ADR-0017](adr/0017-measured-solver-thread-control-and-scaling.md) implements scoped
numerical thread control, nested local timing attribution and bounded reuse of
unchanged TetGen/CAT/local results between immediately rejected physical trials.
Packing defaults to one OpenMP task thread and retry reuse. Explicit
`--solver-openmp-threads 0` and `--no-physical-retry-reuse` retain comparison controls;
MKL environment overrides are recorded separately. Every candidate still receives
full physical validation, and exact targets, float32-output validation, atomic
publication, ordered RNG consumption and cancellation remain required.

The [registered plan](../benchmarks/tranche3-plan.json) set the targets before
optimization. The [complete ledger](../benchmarks/results/windows-20260907-tranche3.json)
retains all successful and failed observations, source/input/executable hashes,
resolved settings, stages, work and peak memory. Primary fixtures use a 44-face
irregular object at 5% full-size density in uniformly scaled, constant-aspect
containers, seed 1918, `.1 -> 1.0` over nine barriers, adaptive sampling and structured
fallback off. Independent float32 fit witnesses are not passed to initialization.

| Controlled Release group | Full validated and loaded successes | Total median (range), seconds | Maximum working set / commit, MiB |
| --- | --- | --- | --- |
| Tranche-2 baseline, 100 | 3/3 | 155.805 (154.702-155.988) | 62.11 / 67.93 |
| Scoped threads only, 100 | 3/3 | 72.552 (72.511-72.964) | 61.79 / 66.62 |
| Delivered threads and retry reuse, 100 | 3/3 | 33.484 (33.474-33.517) | 60.58 / 66.45 |
| Delivered threads and retry reuse, 300 | 3/3 | 125.446 (125.315-125.584) | 127.79 / 135.29 |

The controlled 100-object median improves 4.65x, with identical final poses,
geometry bytes and physical work across the three policies. Retry reuse reduces
actual local solves from 2,559 to 1,153, avoiding 1,406 calls and 34 prepared batches.
At 300, 3,727 calls execute and 7,360 are reused; all nine barriers finish. Earlier
baseline/thread-only 300-object pilots stop at the 300-second deadline after three
and seven barriers respectively; their failures are not speedup denominators.
Both final counts pass the registered 120/300-second median and 1/2-GiB memory
ceilings. Timing processes ran serially without project builds, tests or UI work;
all 44 numerical/runtime DLL hashes and thread environment values were unchanged.

Verification: supported Release, Debug and Ninja clang-tidy matrices each complete
253 tests: 252 passed, one existing account-dependent symlink skip, no failures.
The focused thread/retry/timing/snapshot selection, formatting and static analysis
pass. Nine fixture-generator checks and the Python formatting/lint gates pass;
the source-only CI workflow includes those portable checks. Schema coverage includes
old snapshots and new optional timing/thread/retry fields. After removing the
rejected index experiment, all 56 configure-recorded source hashes match the tested
and measured retry implementation exactly. Fresh delivered build/test logs are
`build/t3-delivered-*-final.log`; the restoration proof is
`build/t3-delivered-source-restoration.json`.

Actual Studio evidence is retained in
`build/studio-tranche3-verified-2/desktop-summary.json`: 13 workflow cases in each
of Debug and Release, four saved 100/300-object rendering/interaction cases, and
12 native cancellation probes. Root inspected the Release 100/300 viewports.
Cancellation request-to-idle ranges are 91-132 ms in Release and 112-329 ms in
Debug, including application polling; these are measured samples, not hard
preemption guarantees. Cancelled runs remain summary-only. Studio uses the same
delivered backend policy; no display-limit increase or worker concurrency was needed.

The secondary corpus passes seeds 0/12345, concave10, irregular36, higher-detail
176-face irregular100 (69.078 seconds), and separate direct100/300 cases. The
original supplied ten-object adaptive-growth regression passes in 88.370 seconds.
These are single observations, not three-sample timing comparisons. A known-fit
slender10 case still stalls at the 0.7 barrier; baseline, thread-only and cached
policies produce identical final poses and physical work.

The supplied 768-face mesh at 100 objects in a uniformly larger container still
exhausts physical triangle-pair work after three barriers (69.517 seconds). A
conservative triangle-index prototype was tested and removed: it slowed the primary
100-object pilot to 35.827 seconds and still failed supplied100 after four barriers
at 265.862 seconds, including 132.684 seconds of collision work. Its negative
measurements and source hashes remain in the ledger; an unmeasured prepass draft is
not delivered. Efficient conservative collision acceleration for heavier meshes is
therefore the next priority. The original supplied container would require 169.67%
of its volume for 100 full-size objects; its bounded non-fit control remains visible.
This tranche establishes the registered known-fit scope, not arbitrary geometry,
density or seed convergence. No 1,000-object throughput claim is made.

## Pryanik follow-up investigation (2026-09-08)

The [pryanik investigation](PRYANIK_INVESTIGATION.md) identifies the reported instant
failure as random initialization exhaustion at 27 of 50, not an Ipopt iteration
limit. Existing-binary reproduction fails in 0.217 seconds; structured fallback
succeeds at 50 in 0.577 seconds with both physical gates and saved loading. Saved
250-object direct placement is a valid positive case at 34.55% volume fraction.
The six current uniform grids top out at 250; neither direct300 nor a bounded
1,000-orientation arithmetic study finds a 300 grid. That does not prove non-fit.

The actual 3,796-triangle input also exposes separate growth issues: adaptive
resampling fails in saved run 16, while run 17 spends 298.6 of 301.3 seconds in
local solves for 30 objects and reaches only one of nine barriers. Collision work
is negligible in that run. These findings prioritize constructive placement,
sampling robustness and smaller local problems alongside the previously measured
heavy-mesh collision bottleneck. The registered tranche-3 fixture scope is unchanged;
no production behavior was changed by this investigation.

## Immediate Next Work

The [reviewed improvement plan](IMPROVEMENT_PLAN.md) records issue triage, four
related tranches and acceptance evidence. The user selected 100-300 objects first,
then 1,000. Tranches 1 through 3 are Verified for their recorded scope; tranche 4 remains planned.

1. Use actual pryanik250/300 and ulamok cases to select the next implementation: constructive placement and adaptive/local-solve robustness for pryanik, collision acceleration where its profile dominates. See the pryanik investigation before generalizing the earlier priority.
2. Extend the registered full-growth, memory and Studio approach to 1,000 in tranche 4; retain the current secondary convergence limits and failed experiments.
3. In parallel, observe the first hosted green workflow before marking Milestone 6 Verified, exercise the file-symlink test on a suitable account, and finish manual file-picker acceptance.
4. If public distribution is proposed, conduct its separate ADR-0009/0010 review. Larger architecture and unrelated UI features remain conditional or deferred in the plan.

## Update Rules

Every C++ implementation change must update this file in the same change.

When updating status:

- Record what is implemented, not what is intended.
- Link or name the test, command, artifact, or benchmark that supports the status.
- Include the verification date when marking a milestone `Verified`.
- Keep the immediate next work list short and executable.
- Move durable rationale into an ADR rather than expanding this ledger.
- Update `docs/COMPATIBILITY.md` when behavior is preserved or changed relative to Python.

## Change Log

| Date | Change | Verification |
| --- | --- | --- |
| 2026-09-07 | Verified tranche 3 for registered known-fit 100/300 growth under ADR-0017 and DEV-0036/0037: scoped solver threads, nested timings, bounded immediate retry reuse and reproducible scaling fixtures; rejected a slower collision-index experiment | Three successes per final count, medians 33.484/125.446 s; 4.65x controlled 100-object improvement; 252 passed/one skipped in all three matrices; format/static analysis and Studio rendering/cancellation gates pass. Heavier supplied100 growth still exhausts physical work; no 1,000 claim |
| 2026-09-06 | Verified improvement tranche 2 under ADR-0016 and DEV-0028 through DEV-0035: current-pose/exact-Hessian growth, exact barriers, object refinement, preserved/reused container surfaces, guarded internal TetGen-cell omission, separate CAT limits and bounded physical step retries | Supplied ten-object genuine growth succeeds 3/3 with identical exact-target placements, both validation gates and loading; median total 221.492 s within 300 s engine bound. Debug/Release/Ninja each 243 passed, one skipped of 244; clean analysis/format; 13 desktop cases per Debug/Release; derivative/recovery/cancellation/non-fit/schema/replay gates pass; tracked benchmark ledger preserves baseline and measured/delivered provenance |
| 2026-09-05 | Verified improvement tranche 1 under ADR-0015: repeatable numbered Studio runs, early destination preflight, bounded failure diagnostics/snapshot replay and real-STL benchmark foundation | Debug/Release/Ninja each 219 passed, one skipped of 220; clean analysis/format; 11 desktop cases per Debug/Release; exact supplied failure and all 128 replay samples match; schemas pass; three real direct successes and three retained growth failures with stage/provenance records. Growth convergence remains tranche 2 |
| 2026-09-05 | Verified Milestone 8 native Studio, bounded saved-run loading, worker lifetime/cancellation, native interaction smoke and UI-enabled CI under ADR-0014 | Debug/Release/Ninja each have 202 passed/one skipped out of 203; zero clang-tidy diagnostics and format pass; focused 10 passed/one skipped and 186 assertions; six desktop smoke cases in Debug/Release, native interactions, inspected real 36-object rendering, unsuccessful saved diagnostics and fresh option-off CLI build |
| 2026-09-05 | Activated Milestone 8 at the maintainer's request: accepted ADR-0014 and concrete native Win32/private-VTK UI, single-worker cancellation/lifetime, safe local saved-run loading and verification scope | Documentation/architecture scope recorded; implementation and verification were pending at activation and are completed in the entry above; Milestone 6 hosted CI remains independent |
| 2026-09-05 | Verified the authorized Milestone 7 measured scope under ADR-0013 and DEV-0026/0027: deterministic structured fallback, independent physical proofs, conservative collision AABB filtering, cumulative pair budgets, compatible summary extensions, and opt-in benchmark/CI tooling | Debug, Release and Ninja clang-tidy each pass 192/192; analysis has zero diagnostics; focused 19 cases/746 assertions and the live Python parity oracle pass; 11-case before/after matrix retains 66 reports; actual ten/36-object full-scale STL runs succeed |
| 2026-09-05 | Revalidated the completed Milestone 6 implementation and repaired the generated supported build cache so it no longer referenced the superseded temporary GL2PS overlay | The supported `windows-vs2026` preset re-resolved official GL2PS `1.4.2#5`; its Release build, format gate, and 178/178 CTest suite passed; the exact installed-tree notice audit matched all 39 packages; fresh Debug and Ninja matrices also passed 178/178; the first hosted workflow run remains pending |
| 2026-08-28 | Implemented the remaining Milestone 6 parity and local release-readiness scope: added a live two-sided Python/C++ oracle corpus, dense initializer parity, deterministic rare correction/recovery fixtures, real Windows Ctrl+Break cancellation, source-only hosted workflow and exact dependency-notice staging, clean-checkout instructions, and the PyVista 0.38.4 reference-environment correction in DEV-0025 | Visual Studio Debug, Visual Studio Release, and Ninja clang-tidy each passed 178/178 (174 Catch2 plus four process tests); the C++ parity filter passed seven cases/403 assertions; the live Python 3.10 oracle passed; Python tests passed 141 with one skip and Ruff passed; exact notice audit matched all 39 packages; package build, clean-checkout smoke, and the representative adaptive profile passed locally; first hosted workflow run remains pending |
| 2026-08-27 | Documented the feasible-but-jammed dense full-scale initialization case and explicitly deferred search-quality changes until after Python/C++ parity; retained the configurable one-million-attempt safety bound and current greedy bounding-sphere behavior under DEV-0007 | Exact supplied count-10/scale-1/seed-1918 initialization placed 6 after one million attempts and still 6 after ten million; a 50-seed scan reached at most 8, while constructive ten-sphere and 36-oriented-cylinder layouts show that search exhaustion is not infeasibility; no product behavior changed |
| 2026-08-27 | Corrected already-complete barrier ordering under DEV-0024: a valid target-scale initialization now bypasses resampling, TetGen/CAT construction, local solves, and recovery without changing transforms, RNG state, or history, then proceeds through mandatory full-resolution and binary-STL-quantized validation; added a generated five-cylinder practical regression with a deliberately unusable TetGen point budget | The focused Release regression passed 59 assertions; Visual Studio Debug, rebuilt Visual Studio Release, and the Ninja clang-tidy configuration passed 164/164 tests each; the exact supplied five-object STL command completed at five exact `1.0` scales with zero packing iterations, TetGen attempts/recoveries, CAT builds, or local solves and passed physical/output validation |
| 2026-08-24 | Began Milestone 6 robustness work: corrected additive Euler publication through exact `R_delta * R_current` composition, bounded each scale objective near the active barrier with overflow-safe saturation, added independently postchecked exact-target snapping, recorded ADR-0012 and DEV-0022/0023, and added generated practical box/cylinder/tetrahedron/rod fit, non-fit, rotation-required, and full-scale regressions | Visual Studio Debug, Visual Studio Release, and Ninja clang-tidy builds passed 163/163 tests each; the exact two-object rotation-enabled STL run succeeded at `0.1001` in 78 aggregate Ipopt iterations; the exact `0.999` to `1.0` rotation-disabled run succeeded after one TetGen recovery in two solves and 14 iterations; both published exact target scales and passed physical validation |
| 2026-08-22 | Implemented and verified Milestone 5: added the deterministic bounded packing coordinator, private VTK resampling, strict validated-once collision/containment checks, transactional TetGen/CAT/Ipopt state updates, bounded correction and termination, cooperative cancellation through input preparation, initialization, engine work, and successful pre-commit publication, exact binary-STL output validation, `irop pack`, conditional version-one schemas, atomic success/summary-only failure publication, ADR-0011, and compatibility through COMPAT-0007/DEV-0021 | Visual Studio Debug and Release and Ninja clang-tidy builds passed 154/154 tests each; clang-format and `git diff --check` passed; genuine growth emitted the fixed four-artifact success set; resource exhaustion and infeasibility (exit 6) emitted summary-only; pre-input, initialization, final-attempt precedence, engine-validation, post-success, malformed-input, no-overwrite, adaptive orchestration, collision-budget, sub-ULP quantization, and schema regressions passed; emitted success/failure/placements JSON validated and misleading success was rejected |
| 2026-08-22 | Implemented and verified Milestone 4: added the exact official Ipopt 3.14.19 MD/MDD vcpkg binary overlay and ADR-0010 distribution gate; isolated the C ABI and MUMPS behind project-owned contracts; ported the Python-compatible seven-variable NLP with exact derivatives; added exhaustive status translation, bounded reusable workspace, and independent solver/applied-geometry acceptance checks; synchronized compatibility through COMPAT-0006/DEV-0016 | Cache-bypassed isolated vcpkg install passed `--enforce-port-checks`; Visual Studio Debug and Release and Ninja clang-tidy builds passed 114/114 tests each; clang-format check passed; hostile option-file, genuine seven-variable, infeasible, clamping/postcheck, malformed-input, and work-limit coverage passed; the final 2,048-row Release derivative benchmark measured 150.222 us analytic versus 1.32158 ms forward difference (about 8.8x) |
| 2026-08-22 | Implemented and verified Milestone 3: approved and pinned TetGen 1.6.0 under the AGPL path; replaced the guard with a patchless vcpkg overlay; added a private serialized adapter, project-owned tetrahedral/CAT types, all CAT split cases, bounded validation/work accounting, and optional VTU/VTP diagnostics; recorded ADR-0009 and synchronized compatibility through COMPAT-0005/DEV-0014 | Isolated vcpkg port checks passed; Visual Studio Debug and Release builds passed 102/102 tests each; Ninja clang-tidy build passed with 102/102 tests; clang-format check passed; focused 22/22 tetrahedralization/CAT/diagnostic tests include Python goldens, point-union parity, dependency failure translation, concurrency, malformed geometry, limits, and split-wide normal invariants |
| 2026-08-22 | Implemented and verified Milestone 2: transforms, centering/geometry queries, adaptive sampling count policy, per-run NumPy-compatible MT19937 initialization, strict/bounded validation, exact one-object surface containment, `irop initialize`, versioned placements/run-summary schemas, direct private Eigen use, and atomic artifact-set publication; recorded ADR-0008 and synchronized compatibility through DEV-0012 | Scoped official-GL2PS Visual Studio Debug and Release builds passed 80/80 tests each; Ninja clang-tidy build passed with 80/80 tests; clang-format check passed; full seed-12345 Python golden, Unicode CLI, schema, resource-exhaustion, winding, surface-intersection, numeric-boundary, and no-publication tests passed |
| 2026-08-22 | Scoped only GL2PS to Microsoft's official vcpkg registry at `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`, resolving `1.4.2#5` while retaining the default baseline and unmodified VTK overlay; documented a hash-verified vcpkg download-cache fallback for the unavailable upstream endpoint; marked Milestone 1 Verified | Fresh Visual Studio configure resolved GL2PS port tree `51e4c4e828efb0b32efd657df71929bb9ba521d5`; Debug and Release builds passed 30/30 tests each; fresh Ninja clang-tidy build passed 30/30; format check passed |
| 2026-08-21 | Implemented the Milestone 1 Windows build foundation and secure STL inspection vertical slice, including stable CLI outcomes, normalized STL, versioned JSON, dependency isolation, Unicode paths, and a disabled TetGen overlay | Visual Studio 2026 Debug and Release builds; 30/30 tests in each; Ninja clang-tidy build and 30/30 tests; clang-format check. Canonical GL2PS downloads timed out, so the local restore used an ignored build-only source-mirror overlay; the resulting verification gate was closed by the official `#5` registry work on 2026-08-22 |
| 2026-08-21 | Retained BSD-3-Clause and its existing notice; added the authoritative reusable C++ formatting profile and synchronized agent guidance | Existing `LICENSE` reviewed; attached profile accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| 2026-08-18 | Established the C++ project baseline and agent workflow | Documentation cross-check and skill structural validation |
