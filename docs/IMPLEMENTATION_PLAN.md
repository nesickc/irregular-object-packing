# C++ Implementation Plan

This plan organizes the C++ port into independently verifiable milestones. A milestone is complete only when its acceptance criteria are satisfied and `docs/STATUS.md` cites the evidence.

## Planning Principles

- Build vertical slices that produce inspectable results early.
- Port behavior before redesigning algorithms.
- Keep the Python implementation runnable as the reference until parity is verified.
- Record preserved quirks and deviations as they are discovered.
- Introduce performance complexity only after obtaining a correct baseline.
- Keep tests proportional to risk and implementation size.

## Milestone 0: Baseline and Agent Onboarding

Goal: make the intended C++ project understandable without rediscovering the Python repository.

Work:

- Establish the project definition, milestone plan, implementation ledger, compatibility catalog, and ADR process.
- Add concise repository guidance for agents.
- Add and validate the repository-scoped C++ development skill.
- Record the initial architectural and compatibility decisions.

Acceptance criteria:

- A new agent can identify scope, architecture, tools, current status, next work, and known compatibility risks from the documented reading order.
- The project skill passes its structural validator.
- No C++ implementation is implied by documentation status.

## Milestone 1: Build Foundation and STL Vertical Slice

Goal: prove that the Windows toolchain, dependency model, CLI, mesh representation, and output contract work together.

Work:

- Add CMake targets, CMake presets, the vcpkg manifest, and the TetGen overlay-port skeleton.
- Record the source, version, license, and notice requirements for the initial dependency set; keep TetGen out of linked targets until its integration review is complete.
- Configure C++20, MSVC warnings, formatting, focused static analysis, and CTest.
- Add the `TriangleMesh`, basic result/error types, and VTK mesh conversion boundary.
- Implement secure STL loading and writing with preflight resource checks and post-load validation.
- Implement `irop inspect` or an equivalent diagnostic command.
- Write a normalized STL and JSON mesh summary from a valid input.

Acceptance criteria:

- Visual Studio 2026 can configure and build through a checked-in preset on Windows 11 x64.
- The CLI prints useful help and returns stable exit categories.
- A sample STL can be read, validated, and written back as an inspectable STL.
- Malformed/truncated fixtures fail safely.
- `run-summary.json` or an inspection summary records resolved inputs and mesh statistics.
- The dependency baseline has a reviewable license inventory, and the TetGen overlay skeleton cannot be enabled accidentally.

## Milestone 2: Transforms, Sampling, and Initialization

Goal: produce a valid initial placement scene using project-owned state.

Work:

- Port volume-scale, rotation, translation, and transform composition semantics.
- Port centering, volume scaling, bounding calculations, and sampling policy.
- Port seeded initial coordinate generation and initial-state validation.
- Establish `PackingConfig`, `PackingState`, and deterministic random-state ownership.
- Write initialized object copies as combined and optional individual STL files.

Acceptance criteria:

- Transform tests cover identity, volume scaling, rotation, translation, and composition.
- The same seed and configuration reproduce initial transforms within the agreed environment.
- Initialized objects satisfy the intended initial containment and spacing checks on representative fixtures.
- An initialization-only run emits tangible STL and JSON placement artifacts.

## Milestone 3: Tetrahedralization and CAT Constraints

Goal: reproduce the constrained tetrahedralization and chordal axis transform data needed by optimization.

Compatibility clarification (2026-08-22): the live Python CAT path supplies the
explicit TetGen switch string `O0/0Q`. The historical wrapper treats that string
as authoritative and bypasses the accompanying `cdt=True`, `steinerleft=0`, and
other keyword arguments, so successful reference runs use point-union Delaunay
tetrahedralization rather than the PLC/CDT behavior suggested by the function
name. Milestone 3 reproduces that observable behavior under
`IROP-COMPAT-0005`; enabling the intended constrained path is a deliberate
post-parity behavior change, not part of this milestone.

Work:

- Review the pinned TetGen version and integration model, and record the license path before linking it into a project target.
- Complete the TetGen overlay port and adapter.
- Convert project meshes to and from the TetGen boundary without exposing TetGen types.
- Port relevant-cell filtering, tetrahedron classification, split cases, face generation, and normal construction.
- Represent CAT constraints in contiguous project-owned structures.
- Add optional diagnostic export for tetrahedral and CAT geometry.

Acceptance criteria:

- Existing Python split cases have compact equivalent C++ tests.
- Simple object/container fixtures produce valid tetrahedralization output.
- CAT faces and normals satisfy orientation and containment invariants.
- Failures are translated into project-owned status values with useful diagnostics.
- The selected TetGen license path and obligations are documented for source publication and any future combined distribution.

## Milestone 4: Local Nonlinear Optimization

Goal: solve the seven-variable per-object transform problem through Ipopt.

Work:

- Implement constraint evaluation for volume scale, three rotations, and three translations.
- Implement the Ipopt adapter, bounds, callbacks, solver options, and result translation.
- Preserve reference scale semantics and cataloged compatibility behavior.
- Decide through measured evidence whether finite differences are sufficient or analytic derivatives are required.
- Make solver workspace and random initial guesses explicit.

Acceptance criteria:

- Constraint and objective calculations match representative Python cases within tolerance.
- Simple box and irregular fixtures produce feasible transforms.
- Expected solver failures return structured outcomes rather than corrupting state.
- Local solves respect configured resource limits.

## Milestone 5: End-to-End Packing CLI

Goal: run the complete packing loop and emit the first parity-oriented packed result.

Work:

- Implement scale barriers, adaptive resampling, CAT rebuilding, per-object solves, and state updates.
- Implement collision and container validation through the geometry boundary.
- Implement bounded collision correction and failure reporting.
- Add progress and diagnostic logging suitable for an interactive CLI.
- Write final STL and JSON artifacts.

Acceptance criteria:

- `irop pack` completes representative small runs from STL inputs.
- Outputs include `packed-objects.stl`, `container.stl`, `placements.json`, and `run-summary.json`.
- Output objects satisfy documented containment and collision criteria or the run is clearly marked unsuccessful.
- Interrupted, infeasible, and resource-exhausted runs fail cleanly and leave no misleading success result.
- Every known preserved quirk or deviation encountered by this milestone has a catalog entry and linked code marker.

## Milestone 6: Compatibility, Robustness, and Release Readiness

Goal: establish confidence that the C++ tool is a dependable replacement for the supported Python functionality.

Work:

- Build a compact representative Python/C++ comparison corpus.
- Compare initialization, CAT constraints, transforms, final feasibility, and packing metrics within tolerances.
- Exercise malformed STL, extreme numeric values, allocation boundaries, and solver termination.
- Add Windows CI for configure, build, tests, formatting checks, and focused static analysis.
- Verify third-party notices and license obligations against the exact resolved dependency versions.
- Document installation, CLI usage, configuration fields, and troubleshooting.

Acceptance criteria:

- Supported reference cases pass compatibility criteria or have approved deviation records.
- Security-boundary tests cover the highest-risk parser and allocation paths.
- CI validates the supported Windows preset.
- A clean checkout has a reproducible setup and smoke-run procedure.
- Every published source or binary artifact includes the licenses and notices required by its contents.
- Documentation accurately distinguishes implemented, verified, experimental, and deferred behavior.

## Milestone 7: Measured Scalability Improvements

Goal: remove demonstrated bottlenecks without destabilizing parity.

Scope clarification (2026-09-05): implementation proceeds on the recorded local
Milestone 6 parity baseline at the maintainer's request; hosted CI acceptance
remains an independent outstanding Milestone 6 gate. The first measured scope is
reproducible stage/count/mesh-size benchmarks, bounded structured initialization
that preserves successful reference seeds, and conservative collision broad-phase
filtering. See ADR-0013. General backtracking, aggregation, subdivision and
parallelism require evidence beyond this first measured workload.

Work:

- Establish timing and memory baselines for representative object counts and mesh sizes.
- Profile tetrahedralization, CAT construction, constraint evaluation, solving, and collision detection separately.
- Add broad-phase collision filtering, workspace reuse, sparse derivatives, or controlled parallelism only where measurements justify them.
- Preserve deterministic single-thread execution for diagnosis.
- Evaluate object aggregation and regional subdivision as explicit designs, not implicit hacks.

Acceptance criteria:

- Benchmarks are reproducible and record inputs, configuration, build type, and environment.
- Each optimization has before/after evidence and correctness regression coverage.
- Generated ten- and 36-cylinder scenes demonstrate bounded dense initialization,
  while established successful Python/C++ seeds and explicit legacy failure cases
  retain their oracle behavior.
- Collision broad-phase filtering preserves touching, nesting and deterministic
  reports while bounding all enumerated pairs, including skipped candidates.
- Performance changes do not introduce an undocumented compatibility deviation.
- Remaining scaling limits are documented honestly.

## Milestone 8: Basic Visualization UI

Goal: provide a native local Windows application for loading meshes, running the
existing packing service, and inspecting published results and diagnostics.

Scope activated at the maintainer's request on 2026-09-05, after the verified
first measured Milestone 7 scope. Milestone 6's first hosted CI run remains an
independent pending gate. The original first-release CLI-only boundary remains
historical; this milestone implements the previously deferred UI extension.
See [ADR-0014](adr/0014-native-windows-visualization-ui.md).

Work:

- Add optional `irop_studio` under `IROP_BUILD_UI=ON`, using native Win32 controls
  and the pinned VTK rendering/interaction modules privately on the application.
  Add no new vcpkg dependency and keep the default core/CLI build independent.
- Provide object/container STL pickers and previews, count, initial/final volume
  scale, scale steps, seed, timeout and adaptive-sampling controls, and selection
  of a new output directory.
- Invoke `pack_scene` on one background worker with progress and cooperative
  cancellation. Keep UI/VTK updates on the UI thread. Window close requests
  cancellation and defers destruction until the worker is terminal and joined.
- Provide camera orbit/pan/zoom, fit and axis views, container visibility and
  object wireframe; keep the window usable while resizing and packing.
- Add a bounded project-owned `load_run_scene` operation for successful pack and
  initialize version-one summaries and unsuccessful packing diagnostics. Read
  only fixed local artifact leaves after canonical containment checks, respect
  resource limits, and never follow recorded original input paths.
- Display saved validation as recorded state rather than rerunning or claiming
  physical certification. Preserve packing algorithms, random ownership, error
  categories, cancellation and atomic artifact publication semantics.

Acceptance criteria:

- `IROP_BUILD_UI=ON` builds the native application with the documented Visual
  Studio Debug/Release presets; the default option-off CLI/core build remains
  supported. Rendering/UI dependency types do not leak into core public APIs.
- Focused saved-run loader tests cover pack/initialize success, summary-only
  diagnostics, malformed and unsupported versions, contradictory output states,
  summary/mesh resource limits, missing files, traversal and canonical escape,
  and independence from recorded original input paths.
- A real UI smoke exercise previews selected inputs, manipulates the camera,
  completes a bounded pack, opens the published result and an unsuccessful run,
  changes visibility/wireframe, and verifies resize behavior.
- Cancellation and window close during an active run retain a responsive event
  loop and safe worker/render ownership through the terminal outcome. Existing
  no-output or summary-only failure rules remain observable.
- Debug/Release builds and the affected test suites pass; changed project code
  passes the authoritative formatter and the Ninja clang-tidy build/test gate.
- Record the actual UI smoke and matrix evidence in `docs/STATUS.md`; mark the
  milestone Verified only after these checks pass. Compilation alone is not
  visual or interaction verification.

Mesh editing, advanced scene management, per-iteration animated geometry,
cross-platform UI support and changes to packing algorithms are outside this
initial UI scope.

## Post-Milestone 8 Improvement Tranches

Planning scope recorded on 2026-09-05 at the maintainer's request. The
[improvement plan](IMPROVEMENT_PLAN.md) reviews issues raised during implementation,
prioritizes genuine packing-growth reliability and automatic output folders, and
sets the requested scale ladder at 100-300 objects followed by 1,000.

The sequence is: (1) repeatable runs and useful diagnostics, (2) reliable
packing growth, (3) practical 100-300-object performance, (4) extension to 1,000.
Tranche 1 is now Verified under ADR-0015: numbered runs/preflight, bounded
diagnostics/replay and real-STL measurement foundations pass the evidence gates
recorded in STATUS. Tranche 2 is Verified under ADR-0016: current-pose/exact-derivative
growth, bounded sampling/physical recovery and preserved container sampling pass
the supplied ten-object gate in three independent runs under 300 seconds, with
both physical gates and a generated regression corpus. Tranche 3 is Verified under
ADR-0017 for registered known-fit 100/300 growth, with three successes each,
33.484/125.446-second medians and Studio acceptance. Thread control and retry reuse
meet those targets; heavier supplied100 growth still reaches a physical-work limit.
The [pryanik follow-up](PRYANIK_INVESTIGATION.md) now prioritizes constructive
placement and sampling/local-solve robustness on actual meshes, with collision
acceleration where measured. Tranche 4 retains the 1,000-object extension after
that practical follow-up.
Hosted CI is a parallel acceptance item. Historical milestone scope and verification remain unchanged.

## Milestone Change Policy

When changing this plan:

1. Explain the reason in the affected milestone.
2. Update `docs/STATUS.md` and its next actions.
3. Add an ADR when the change alters a durable architectural or project-wide decision.
4. Update the project skill if the preferred implementation workflow changes.
