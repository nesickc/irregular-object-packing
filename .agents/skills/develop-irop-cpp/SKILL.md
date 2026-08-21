---
name: develop-irop-cpp
description: Develop and maintain the IROP C++ implementation on Windows. Use when implementing, refactoring, reviewing, debugging, or optimizing C++ code; editing CMake or vcpkg files; writing C++ tests; or assessing security and performance for this repository. Do not use for Python-only research or unrelated documentation work.
---

# Develop IROP C++

Apply the repository's compatibility-first C++ workflow and leave implementation status verifiable for the next maintainer.

## Start With Project State

Resolve the repository root, then read:

1. `AGENTS.md`
2. `docs/PROJECT.md`
3. `docs/STATUS.md`
4. The active milestone in `docs/IMPLEMENTATION_PLAN.md`
5. `docs/COMPATIBILITY.md`
6. Relevant records in `docs/adr/`

Inspect the corresponding Python source and tests before porting behavior. Treat documentation as intended design and `docs/STATUS.md` as the record of what actually exists.

## Load the Relevant Branch

Read only the references needed for the task:

- Implement or refactor C++: [implementation.md](references/implementation.md)
- Change CMake, vcpkg, targets, or dependencies: [build-and-dependencies.md](references/build-and-dependencies.md)
- Add or modify tests: [testing.md](references/testing.md)
- Review C++ or build changes: [review.md](references/review.md)
- Work on mesh parsing, validation, resource limits, or hardening: [security.md](references/security.md)
- Profile, optimize, parallelize, or design for larger workloads: [performance.md](references/performance.md)

Load multiple references only when the task genuinely spans those activities.

## Preserve the Core Contract

- Preserve successful Python behavior unless a defect has an evident safe correction.
- Mark preserved questionable behavior with `COMPATIBILITY(IROP-COMPAT-NNNN)`.
- Mark intentional corrections with `DEVIATION(IROP-DEV-NNNN)`.
- Add or update the matching `docs/COMPATIBILITY.md` entry and focused test.
- Keep dependency-specific types behind adapters and project-owned types at module boundaries.
- Keep the CLI thin and reusable behavior in `irop_core`.
- Prefer simple composition, explicit state, RAII, and direct data flow.
- Treat mesh and configuration inputs as untrusted.
- Add abstraction and concurrency only when the current requirement or evidence justifies them.

## Execute the Change

1. State the milestone outcome and acceptance evidence affected by the task.
2. Inspect current code, Python reference behavior, tests, compatibility items, and ADR constraints.
3. Design the smallest cohesive change that preserves module boundaries.
4. Implement using the root `.clang-format` and repository tools. The checked-in profile is authoritative over generic Google-style defaults.
5. Run focused verification, then the broader affected checks.
6. Review the diff for behavior drift, unsafe input handling, ownership, and dependency leakage.
7. Update `docs/STATUS.md`; update compatibility, plan, ADRs, and this skill when their governed facts change.

## Finish With Evidence

Report:

- The user-visible or architectural outcome.
- Build, test, formatting, analysis, or benchmark commands actually run.
- Compatibility IDs added or affected.
- Status documentation updated.
- Remaining risks, assumptions, and unverified paths.

Do not mark a milestone verified from code inspection alone.
