# C++ Implementation Status

Last updated: 2026-08-22

## Current Summary

Milestones 1 through 3 are verified. The C++20 library and thin CLI provide hardened STL inspection, deterministic bounded initialization, and project-owned tetrahedralization/CAT constraint structures. TetGen 1.6.0 is pinned through a patchless vcpkg overlay and isolated behind a private adapter; CAT split cases, ownership grouping, plane normals, work limits, and optional VTU/VTP diagnostics are covered without exposing TetGen or VTK types. GL2PS `1.4.2#5` remains resolved from the package-scoped official vcpkg registry without changing the reviewed default dependency baseline.

Current milestone: Milestone 4 — Local Nonlinear Optimization.

Next outcome: solve representative seven-variable local placement cases through a private Ipopt adapter with project-owned statuses and derivative tests.

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
| 4. Local nonlinear optimization | Not started | None | Solve representative seven-variable cases |
| 5. End-to-end packing CLI | Not started | None | Emit a complete packed STL and JSON result |
| 6. Compatibility, robustness, and release readiness | Not started | None | Pass representative parity and safety corpus |
| 7. Measured scalability improvements | Deferred | Requires a verified parity baseline | Approve benchmarks and first measured bottleneck |
| 8. Basic visualization UI | Deferred | Requires a stable CLI and core result model | Define UI acceptance criteria |

## Module Status

| Area | Status | Notes |
| --- | --- | --- |
| Project documentation | Verified | Baseline documents, BSD policy, formatting policy, and ADR process established |
| Agent development skill | Verified | Repository-scoped skill created, structurally validated, and synchronized with the formatter policy |
| C++ formatting profile | Verified | Root `.clang-format` accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| CMake and presets | Verified | Target-based CMake, preset-derived fresh Visual Studio and Ninja configurations, warnings, formatting, and CTest all exercised with GL2PS `#5` |
| vcpkg manifest and overlay ports | Verified | Default baseline pinned; GL2PS-only official registry pinned; narrow VTK/MSVC overlay active; patchless TetGen 1.6.0 overlay hash-verified and consumed privately with its AGPL notice installed |
| CLI | Implemented | Thin `irop inspect` and `irop initialize` commands expose bounded controls, Unicode Windows arguments, atomic fixed artifact sets, and stable exit categories 0/2/3/4/5/70 |
| Domain model | Implemented | Project-owned mesh, transform, packing, tetrahedralization, CAT polygon/constraint/range, work-limit, result, and error types cover the verified Milestone 3 boundary; optimizer and final packing-result types remain later work |
| STL I/O and mesh validation | Implemented | ASCII/binary preflight, checked resource boundaries, VTK isolation, post-load validation, safe binary writing, and malformed-input tests pass |
| Geometry operations | Implemented | Volume-scale transforms, point-centroid centering, volume/radius/bounds, closed-surface queries, winding normalization, bounded VTK triangle-surface intersection, mesh instantiation, and combination are covered |
| Initialization and sampling | Verified | Python-compatible adaptive count policy and NumPy MT19937 draw order; strict containment/spacing; one-object compatibility path; configurable attempt/query/pair work bounds; exact Python rejection golden |
| Tetrahedralization | Verified | Private TetGen 1.6.0 adapter preserves ordered ownership and the reference `O0/0Q` point-union cell complex, validates closed participants/output invariants, serializes process-global backend state, bounds accepted counts, and translates failures |
| CAT construction | Verified | Four-owner, 2+1+1, 3+1, and 2+2 Python goldens; relevant-cell filtering; participant-contiguous polygons/constraints; inward unit-normal and coplanarity invariants; malformed-input and exhaustion coverage |
| Nonlinear optimization | Not started | Ipopt boundary is designed only |
| Packing engine | Not started | Iteration pipeline is documented only |
| Collision correction | Not started | Bounded correction is planned as a deviation |
| Result serialization | In progress | Versioned inspection, placements, and initialization success schemas plus atomic STL/JSON publication and optional no-overwrite VTU/VTP diagnostics are implemented; end-to-end packing outcomes remain Milestone 5 work |
| C++ tests | Verified | 100 focused Catch2 cases plus inspection and initialization CLI smoke tests pass in Debug, Release, and the clang-tidy analysis build |
| CI and static analysis | In progress | Local clang-format and clang-tidy gates are implemented and pass; hosted CI remains readiness work |
| Benchmarks | Deferred | Add after parity and profiling |
| Visualization UI | Deferred | Later local application over the core library |

## Known Compatibility Work

The catalog in `docs/COMPATIBILITY.md` is synchronized through `IROP-COMPAT-0005` and `IROP-DEV-0014`. Milestone 3 preserves the Python wrapper's literal `O0/0Q` point-union tetrahedralization even though its function is named CDT, while correcting unsafe mesh/ownership/output assumptions and adding project-controlled input, accepted-output, and CAT work limits. `IROP-DEV-0002` remains `Implemented` rather than `Verified` until optimizer history itself exists.

## Immediate Next Work

1. Pin and isolate the Ipopt integration behind project-owned local-solve contracts.
2. Port the seven-variable objective, CAT/container constraints, bounds, and analytic derivatives.
3. Add representative Python/C++ solve goldens plus infeasible/failure translation tests.

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
| 2026-08-22 | Implemented and verified Milestone 3: approved and pinned TetGen 1.6.0 under the AGPL path; replaced the guard with a patchless vcpkg overlay; added a private serialized adapter, project-owned tetrahedral/CAT types, all CAT split cases, bounded validation/work accounting, and optional VTU/VTP diagnostics; recorded ADR-0009 and synchronized compatibility through COMPAT-0005/DEV-0014 | Isolated vcpkg port checks passed; Visual Studio Debug and Release builds passed 102/102 tests each; Ninja clang-tidy build passed with 102/102 tests; clang-format check passed; focused 22/22 tetrahedralization/CAT/diagnostic tests include Python goldens, point-union parity, dependency failure translation, concurrency, malformed geometry, limits, and split-wide normal invariants |
| 2026-08-22 | Implemented and verified Milestone 2: transforms, centering/geometry queries, adaptive sampling count policy, per-run NumPy-compatible MT19937 initialization, strict/bounded validation, exact one-object surface containment, `irop initialize`, versioned placements/run-summary schemas, direct private Eigen use, and atomic artifact-set publication; recorded ADR-0008 and synchronized compatibility through DEV-0012 | Scoped official-GL2PS Visual Studio Debug and Release builds passed 80/80 tests each; Ninja clang-tidy build passed with 80/80 tests; clang-format check passed; full seed-12345 Python golden, Unicode CLI, schema, resource-exhaustion, winding, surface-intersection, numeric-boundary, and no-publication tests passed |
| 2026-08-22 | Scoped only GL2PS to Microsoft's official vcpkg registry at `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`, resolving `1.4.2#5` while retaining the default baseline and unmodified VTK overlay; documented a hash-verified vcpkg download-cache fallback for the unavailable upstream endpoint; marked Milestone 1 Verified | Fresh Visual Studio configure resolved GL2PS port tree `51e4c4e828efb0b32efd657df71929bb9ba521d5`; Debug and Release builds passed 30/30 tests each; fresh Ninja clang-tidy build passed 30/30; format check passed |
| 2026-08-21 | Implemented the Milestone 1 Windows build foundation and secure STL inspection vertical slice, including stable CLI outcomes, normalized STL, versioned JSON, dependency isolation, Unicode paths, and a disabled TetGen overlay | Visual Studio 2026 Debug and Release builds; 30/30 tests in each; Ninja clang-tidy build and 30/30 tests; clang-format check. Canonical GL2PS downloads timed out, so the local restore used an ignored build-only source-mirror overlay; the resulting verification gate was closed by the official `#5` registry work on 2026-08-22 |
| 2026-08-21 | Retained BSD-3-Clause and its existing notice; added the authoritative reusable C++ formatting profile and synchronized agent guidance | Existing `LICENSE` reviewed; attached profile accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| 2026-08-18 | Established the C++ project baseline and agent workflow | Documentation cross-check and skill structural validation |
