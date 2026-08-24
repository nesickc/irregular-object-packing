# ADR-0012: Transform-Consistent Barrier-Bounded Local Solves

- Status: Accepted
- Date: 2026-08-24
- Deciders: Project maintainers

## Context

Two practical two-object runs exposed independent defects in the Milestone 5
local-solve integration.

First, the nonlinear constraints rotate geometry that already has the current
orientation, so the optimized incremental rotation is applied as
`R_delta * R_current`. Result publication instead added the three Euler
components. Euler addition is not matrix composition, and the independently
postchecked stored transform could therefore be infeasible even when Ipopt's
candidate was feasible.

Second, the packing coordinator gave Ipopt no finite upper bound for the scale
multiplier. The objective continued maximizing scale far beyond the active
barrier even though result application would clamp it afterward. A nearly
full-scale two-object run consequently spent 533 iterations on its first solve
and reached the 1,000-iteration limit on its second.

These behaviors were initially retained as compatibility paths in
`IROP-COMPAT-0006` and `IROP-COMPAT-0001`, respectively. Practical evidence now
shows that preserving them prevents known-feasible packings from completing.

## Decision

- Keep the seven-variable incremental nonlinear program and its analytic
  derivatives.
- Apply each accepted incremental rotation by left-composing
  `R_delta * R_current`, exactly as the constraint model does.
- Encode the composed orientation back into the project's `Ry * Rz * Rx` Euler
  representation using a canonical extraction. At gimbal lock, set `x` to zero
  and retain the equivalent orientation in `y`; use the signed half-pi value for
  `z`.
- Give each packing-local solve a finite maximum scale multiplier derived from
  the active barrier. Use a fixed relative allowance of
  `20 * maximum_local_solve_tolerance` so representable slack does not disappear
  when a caller requests a smaller solver tolerance.
- Saturate extreme target/current ratios at the largest representable bound
  below the adapter's `1e19` finite-bound sentinel. This lets very small scales
  advance over multiple bounded packing iterations without arithmetic overflow.
- Retain the exact barrier clamp as a numerical publication guard. If an
  accepted result is just below the barrier, attempt an exact-scale snap,
  independently postcheck that applied transform, and retain the original
  feasible result when the snap is infeasible or its optional work budget is
  unavailable.
- Keep the existing solver-space, applied-transform, full-resolution, and
  serialized-geometry postchecks.
- Retire `IROP-COMPAT-0001` and `IROP-COMPAT-0006`. Record the intentional
  corrections as `IROP-DEV-0023` and `IROP-DEV-0022`, respectively.
- This decision supersedes only ADR-0011's preservation of unbounded
  solve-then-clamp behavior. Its bounded-loop, validation, result, and artifact
  contracts remain unchanged.

## Consequences

- The stored transform now represents the same orientation that Ipopt's local
  constraints evaluated, eliminating representation-only postcheck failures.
- Ipopt stops near the scale needed for the current barrier instead of spending
  work on growth that would be discarded.
- Near-target completion remains exact without accepting an infeasible snap.
- Persisted rotation values can differ from the Python reference and earlier C++
  builds. The rotation order, radians unit, JSON schema, and resulting matrix
  semantics are unchanged.
- Euler values are canonical coordinates, not a history of componentwise local
  increments. Equivalent orientations can have different Euler triples.
- Independent postchecks remain mandatory; neither composition nor a finite
  scale bound weakens physical acceptance.

## Alternatives Considered

### Keep additive Euler updates and disable local rotation

Rejected because it avoids one symptom by removing an intended optimization
degree of freedom and still leaves the stored-transform mismatch in the API.

### Store matrices or quaternions in packing state

Deferred because exact composition can be encoded in the existing project-owned
Euler contract without changing schemas or every transform consumer.

### Raise the Ipopt iteration limit

Rejected because it spends more work optimizing scale that will be clamped and
does not provide a termination guarantee.

### Set the exact barrier as the upper bound

Rejected because solver feasibility tolerances can return just below the barrier,
causing avoidable extra packing iterations. Bounded slack plus an independently
validated exact-scale snap preserves both liveness and feasibility.

## Verification

- Matrix-invariant tests cover non-commuting rotations plus exact and near
  positive/negative gimbal lock.
- Numeric-policy tests cover default, small, and below-binary64-ULP solver
  tolerances plus a target/current ratio above the finite-bound sentinel.
- A near-tight local fixture proves that a below-target solver candidate is
  snapped to an exact, positively feasible barrier only after the applied
  constraints pass.
- Generated primitive tests cover obvious box/cylinder fits, an oversized
  non-fit, two separated objects, a rod whose fit requires a known rotation,
  two-object rotation-enabled growth, and full-scale cylinder and tetrahedron
  cases under a 200-iteration local limit.
- The original two-object cylinder input succeeds from `0.1` to `0.1001` with
  local rotation enabled, exact target scales, and passing physical validation.
- The original two-object full-scale input succeeds from `0.999` to `1.0` with
  rotation disabled in two local solves and 14 aggregate Ipopt iterations.
- Visual Studio 2026 Debug and Release each pass all 163 CTest cases.
