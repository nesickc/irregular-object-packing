# C++ Implementation Status

Last updated: 2026-08-21

## Current Summary

The C++ implementation has not started. The project baseline, milestone plan, compatibility process, ADR process, agent onboarding guide, project-scoped development skill, BSD licensing policy, and authoritative C++ formatting profile are defined.

Current milestone: Milestone 1 — Build Foundation and STL Vertical Slice.

Next outcome: configure and build a C++20 CLI on Windows 11 with Visual Studio 2026, then securely read and re-emit an STL with a JSON summary.

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
| 1. Build foundation and STL vertical slice | Not started | None | Configure, build, inspect, and re-emit STL |
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
| CMake and presets | Not started | No C++ build files exist |
| vcpkg manifest and overlay ports | Not started | Dependency baseline is documented only |
| CLI | Not started | Intended command and artifacts are documented only |
| Domain model | Not started | `TriangleMesh`, transforms, configuration, and results are designed only |
| STL I/O and mesh validation | Not started | Required in Milestone 1 |
| Geometry operations | Not started | VTK boundary is designed only |
| Initialization and sampling | Not started | Python reference identified |
| Tetrahedralization | Not started | TetGen adapter and overlay are designed only |
| CAT construction | Not started | Python reference and test cases identified |
| Nonlinear optimization | Not started | Ipopt boundary is designed only |
| Packing engine | Not started | Iteration pipeline is documented only |
| Collision correction | Not started | Bounded correction is planned as a deviation |
| Result serialization | Not started | STL and JSON output contract is documented only |
| C++ tests | Not started | Catch2 strategy is documented only |
| CI and static analysis | Not started | Planned for the foundation and readiness milestones |
| Benchmarks | Deferred | Add after parity and profiling |
| Visualization UI | Deferred | Later local application over the core library |

## Known Compatibility Work

The initial catalog is in `docs/COMPATIBILITY.md`. No compatibility item has been implemented yet. Each item must move from `Planned` to `Implemented` and then `Verified` as code and evidence are added.

## Immediate Next Work

1. Add target-based root CMake configuration and Visual Studio 2026 presets.
2. Add a pinned vcpkg manifest and license inventory for the Milestone 1 dependency subset.
3. Integrate the checked-in formatting profile with build checks, then establish warnings, CTest, and focused static-analysis configuration.
4. Add the project-owned `TriangleMesh` and basic result/error types.
5. Implement a resource-limited STL inspection vertical slice and emit normalized STL plus JSON.
6. Update this file with exact build and test evidence.

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
| 2026-08-21 | Retained BSD-3-Clause and its existing notice; added the authoritative reusable C++ formatting profile and synchronized agent guidance | Existing `LICENSE` reviewed; attached profile accepted by Visual Studio 2026 bundled clang-format 22.1.3 |
| 2026-08-18 | Established the C++ project baseline and agent workflow | Documentation cross-check and skill structural validation |
