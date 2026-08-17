# Testing Workflow

Use this reference when adding tests or deciding how much verification a change needs.

## Prioritize by Risk

Test, in order:

1. Public behavior and milestone acceptance.
2. Compatibility items and intentional deviations.
3. Mathematical and geometry invariants.
4. Unsafe input, resource exhaustion, and termination boundaries.
5. Third-party error translation.
6. Representative end-to-end artifact generation.
7. Performance only through dedicated benchmarks.

## Keep Tests Proportional

- Prefer small parameterized Catch2 cases over repeated test bodies.
- Reuse compact fixtures and generate boxes, tetrahedra, and simple meshes in code.
- Port existing Python cases that define algorithm behavior; do not mechanically port every assertion.
- Use golden files only when invariants cannot express the behavior clearly.
- Keep large meshes out of ordinary unit tests.
- Avoid tests of private implementation detail unless the numerical kernel cannot be observed reliably through a stable boundary.
- Do not chase an arbitrary coverage percentage.

## Test Layers

- Unit: transforms, scale semantics, tetrahedron splits, constraint values, validation helpers.
- Adapter: VTK/TetGen/Ipopt conversion, callbacks, and failure translation.
- Compatibility: cataloged preserved or corrected behavior.
- Integration: initialization, CAT construction, local optimization, and correction loops.
- Smoke: invoke the CLI on a tiny case and validate STL/JSON artifacts and outcome.

## Numerical Assertions

- Use explicit absolute and relative tolerances tied to the operation.
- Compare containment, collision state, orientation, volume, and convergence invariants.
- Do not require byte-identical floating-point serialization across toolchain versions.
- Record seed and thread count for nondeterministic algorithms.

## Verification Discipline

Run the narrowest test first, then the affected suite. Use repository CMake/CTest presets once they exist; do not invent undocumented build commands. Update `docs/STATUS.md` with exact evidence when a module or milestone status changes.
