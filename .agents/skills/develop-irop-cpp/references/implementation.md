# Implementation Workflow

Use this reference for new C++ behavior and refactors.

## Before Editing

- Identify the active milestone and the narrow acceptance criterion being advanced.
- Locate the Python behavior and its tests. Confirm whether the path is successful behavior, failure recovery, diagnostic code, or dead code.
- Check `docs/COMPATIBILITY.md` before “fixing” surprising behavior.
- Identify the owning module and avoid placing algorithm code in the CLI.

## Design Rules

- Prefer project-owned value types at boundaries.
- Use RAII and explicit ownership; use smart pointers only when ownership cannot be represented by values or references.
- Prefer `std::vector` or another contiguous representation for numerical batches.
- Use fixed-size Eigen vectors and matrices for transforms where this improves clarity.
- Keep VTK, TetGen, Ipopt, and serialization types inside adapters.
- Prefer free functions for stateless cohesive operations and classes for owned state or maintained invariants.
- Prefer composition over inheritance. Avoid a base class until multiple real implementations share a meaningful contract.
- Keep templates local and modest. Do not turn runtime algorithm policy into a metaprogramming framework.
- Deduplicate stable domain knowledge, not incidental similar-looking code.

## Style

- Follow Google C++ Style through the checked-in formatter.
- Use descriptive names that expose scale semantics, units, ownership, and coordinate space.
- Keep headers narrow; include what they use and forward-declare only when safe.
- Use comments for rationale, invariants, numerical conventions, and compatibility constraints.
- Avoid hidden global state and implicit process-wide random generators.

## Errors and Results

- Throw for unrecoverable application-boundary setup, parsing, or filesystem failures.
- Return project-owned status/results for expected invalid geometry, solver failure, infeasibility, timeout, and exhausted limits.
- Assert internal programmer invariants.
- Translate third-party exceptions and codes at their adapter boundary.
- Preserve partial diagnostic evidence without presenting a failed run as successful.

## Change Shape

1. Add or update the project-owned model and invariant.
2. Implement the narrow adapter or algorithm operation.
3. Connect it through the application service or packing engine.
4. Keep CLI presentation and path handling at the outer boundary.
5. Add focused tests and, for a vertical slice, an inspectable artifact.
6. Update implementation status and compatibility records.

Avoid unrelated cleanup during a compatibility-sensitive port. Record useful cleanup as follow-up work when it would obscure the behavioral change.
