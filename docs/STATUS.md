# C++ Implementation Status

Last updated: 2026-08-22

## Current Summary

The Milestone 1 C++ vertical slice is verified. The repository now has a C++20 library and CLI, pinned vcpkg dependency graph, Visual Studio and Ninja presets, project-owned mesh/error/result types, a hardened VTK STL adapter, transactional inspection artifacts, a versioned JSON schema, and focused tests. GL2PS `1.4.2#5` is resolved from a package-scoped official vcpkg registry without changing the reviewed default dependency baseline.

Current milestone: Milestone 2 — Transforms, Sampling, and Initialization.

Next outcome: emit a valid initialized placement scene with unit-explicit transforms, deterministic sampling, and bounded initialization.

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
| 2. Transforms, sampling, and initialization | Not started | None | Emit a valid initialized placement scene |
| 3. Tetrahedralization and CAT constraints | Not started | None | Verify tetrahedral and CAT invariants |
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
| vcpkg manifest and overlay ports | Verified | Default baseline pinned; GL2PS-only official registry pinned; dependency inventory recorded; narrow VTK/MSVC compatibility overlay active; TetGen overlay hard-disabled and absent from targets |
| CLI | Implemented | `irop inspect` exposes bounded input controls, fixed artifacts, Unicode Windows arguments, and stable exit categories 0/2/3/4/5/70 |
| Domain model | In progress | Milestone 1 `TriangleMesh`, limits, inspection results, and errors implemented; transforms and placement state remain Milestone 2 work |
| STL I/O and mesh validation | Implemented | ASCII/binary preflight, checked resource boundaries, VTK isolation, post-load validation, safe binary writing, and malformed-input tests pass |
| Geometry operations | Not started | STL conversion is isolated in I/O; transforms, sampling, and general geometry operations remain Milestone 2 work |
| Initialization and sampling | Not started | Python reference identified |
| Tetrahedralization | Not started | Hard-disabled TetGen guard overlay exists; adapter and integration remain Milestone 3 work |
| CAT construction | Not started | Python reference and test cases identified |
| Nonlinear optimization | Not started | Ipopt boundary is designed only |
| Packing engine | Not started | Iteration pipeline is documented only |
| Collision correction | Not started | Bounded correction is planned as a deviation |
| Result serialization | In progress | Versioned inspection JSON and normalized STL are implemented; end-to-end packing result remains Milestone 5 work |
| C++ tests | Implemented | 29 focused Catch2 cases plus one end-to-end CLI smoke test pass in Debug, Release, and the analysis build |
| CI and static analysis | In progress | Local clang-format and clang-tidy gates are implemented and pass; hosted CI remains readiness work |
| Benchmarks | Deferred | Add after parity and profiling |
| Visualization UI | Deferred | Later local application over the core library |

## Known Compatibility Work

The initial catalog is in `docs/COMPATIBILITY.md`. No catalog item targets Milestone 1. The STL slice preserves VTK/PyVista coincident-point merging for successful reference inputs and adds trust-boundary rejection only for malformed or unsafe inputs for which the Python project defines no successful behavior, so no Milestone 1 compatibility marker is required.

## Immediate Next Work

1. Add Milestone 2 transform, placement, configuration, and result types with unit-explicit fields.
2. Port mesh transforms and volume-scale semantics with focused geometry-invariant tests.
3. Port bounded surface sampling and seeded initialization, then emit a valid initialized placement scene.

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
| 2026-08-22 | Scoped only GL2PS to Microsoft's official vcpkg registry at `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`, resolving `1.4.2#5` while retaining the default baseline and unmodified VTK overlay; documented a hash-verified vcpkg download-cache fallback for the unavailable upstream endpoint; marked Milestone 1 Verified | Fresh Visual Studio configure resolved GL2PS port tree `51e4c4e828efb0b32efd657df71929bb9ba521d5`; Debug and Release builds passed 30/30 tests each; fresh Ninja clang-tidy build passed 30/30; format check passed |
| 2026-08-21 | Implemented the Milestone 1 Windows build foundation and secure STL inspection vertical slice, including stable CLI outcomes, normalized STL, versioned JSON, dependency isolation, Unicode paths, and a disabled TetGen overlay | Visual Studio 2026 Debug and Release builds; 30/30 tests in each; Ninja clang-tidy build and 30/30 tests; clang-format check. Canonical GL2PS downloads timed out, so the local restore used an ignored build-only source-mirror overlay; the resulting verification gate was closed by the official `#5` registry work on 2026-08-22 |
| 2026-08-21 | Retained BSD-3-Clause and its existing notice; added the authoritative reusable C++ formatting profile and synchronized agent guidance | Existing `LICENSE` reviewed; attached profile accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| 2026-08-18 | Established the C++ project baseline and agent workflow | Documentation cross-check and skill structural validation |
