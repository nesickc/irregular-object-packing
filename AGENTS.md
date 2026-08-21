# Agent Guide

This repository contains the Python reference implementation and the planned C++ port of irregular object packing. Preserve the Python implementation as the behavioral reference until the C++ parity milestone is complete.

## Read First

Before changing the C++ implementation, read these files in order:

1. `docs/PROJECT.md`
2. `docs/STATUS.md`
3. The relevant milestone in `docs/IMPLEMENTATION_PLAN.md`
4. `docs/COMPATIBILITY.md`
5. Relevant records in `docs/adr/`

Use the repository skill `$develop-irop-cpp` for C++ implementation, refactoring, review, CMake or vcpkg work, tests, security work, and performance work.

## Working Rules

- Reproduce successful Python behavior unless an obvious defect can be corrected without meaningful downstream effects.
- Mark intentionally preserved questionable behavior with `COMPATIBILITY(IROP-COMPAT-NNNN)` and corrected behavior with `DEVIATION(IROP-DEV-NNNN)`. Keep `docs/COMPATIBILITY.md` synchronized.
- Keep the CLI thin and the packing implementation in reusable library modules.
- Keep VTK, TetGen, Ipopt, and other dependency-specific types behind adapters. Do not expose them from the project-owned domain API.
- Prefer simple composition and explicit data flow. Do not introduce speculative hierarchies, service boundaries, or template frameworks.
- Treat the checked-in `.clang-format` as the authoritative C++ style. It is Google-based with project-specific overrides; do not substitute the unmodified Google preset.
- Treat mesh and configuration inputs as untrusted. Validate resource sizes, arithmetic, indices, finite values, and termination conditions at trust boundaries.
- Add dependencies only when they replace specialized, security-sensitive, or substantial code. Prefer the standard library for small utilities.
- Optimize measured hot paths. Preserve a simple correct implementation when performance is not material.
- Keep tests proportional. Focus on public behavior, compatibility, geometry invariants, unsafe inputs, and end-to-end smoke coverage.

## Required Documentation Updates

Every implementation change must update `docs/STATUS.md` in the same change.

Also update:

- `docs/COMPATIBILITY.md` when behavior differs from or intentionally mirrors a questionable Python behavior.
- An ADR when changing architecture, dependency strategy, public contracts, or project-wide engineering policy.
- `.agents/skills/develop-irop-cpp/` when the preferred C++ development approach changes.
- `docs/IMPLEMENTATION_PLAN.md` when milestone scope or acceptance criteria change.

## Completion Checklist

Before handing work off:

1. Build the affected targets with a documented preset when CMake exists.
2. Run the narrowest relevant tests, then the broader affected suite.
3. Format and statically analyze changed project code when those tools exist.
4. Check for undocumented compatibility behavior.
5. Update implementation status and cite the verification evidence.
6. Report remaining risks or unverified assumptions.
