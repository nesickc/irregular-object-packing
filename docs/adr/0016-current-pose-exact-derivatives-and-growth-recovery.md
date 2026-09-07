# ADR-0016: Current Poses, Exact Derivatives and Growth Recovery

- Status: Accepted
- Date: 2026-09-05
- Deciders: Project maintainers

## Context

The maintainer authorized tranche 2 of the improvement plan. Tranche 1 captured
the supplied ten-object growth failure as a standalone local problem. The original
small-scale/random-rotation guess violates 422 of its 1,012 constraints; the current
pose satisfies all of them. Identity-start replay converges in 80 iterations while
the original guess exhausts 1,000. Analytic first derivatives agree with independent
central differences; the evidence does not indicate an incorrect Jacobian.

The full identity-start run exposes a second failure: optimizing slightly above
the barrier and then clamping the volume makes a feasible pose infeasible. These
per-vertex CAT halfspaces need not contain the object center, so shrinking about
that center does not preserve feasibility. An exact upper bound solves that saved
problem with unchanged postchecks. The updated run subsequently exposes repeated
physical correction against coarse adaptive surfaces and another local iteration
limit. Exact-Hessian experiments with the same Ipopt DLL, bounds and tolerances
converge much faster on the isolated problems.

## Decision

- Add an explicit reference-growth switch for reproducibility. It retains the
  historical random start, relaxed barrier bound, limited-memory derivatives,
  per-object solving and adaptive sampling policy. Default growth uses the policy
  below; successful placements may change and are verified by invariants.
- Start local solves from the current pose. Retain already-completed object poses
  while remaining objects grow; preserve complete-batch commits and correction.
  Actual solves consume deterministic object-ordered reference random draws.
- Use an exact target/current scale upper bound with finite-bound saturation.
  Retain exact-target snapping only after its independent applied-geometry check.
- Evaluate the analytic Lagrangian Hessian in the private Ipopt adapter. Only the
  scale/rotation lower block has ten structural entries; translation curvature is
  zero. Weighted row work is charged to the existing constraint-row budget, with
  separate Hessian counters for attribution. Retain the old adapter mode as the
  default for standalone requests and absent historical snapshot fields.
- Refine the adaptive object surface after physical collision correction by
  doubling its triangle target up to the original surface. Retain this floor for
  subsequent barriers and sample the container independently without changing its surface.
  Each refinement uses the existing resampling, iteration and cumulative budgets;
  it does not restart the run. Default container sampling uses double-precision midpoint subdivision
  without smoothing or decimation and reuses the first active barrier's container
  sites through subsequent growth; reference mode retains its Loop schedule. The
  measured reference box proxy loses about 63% of its volume. Independently,
  its final-barrier sampling schedule raises the surface from 12,288 to 196,608
  triangles, enlarging local problems enough to exhaust the 300-second run.
  The container geometry is unchanged while objects grow, so its initial density
  is retained rather than driven by the object's increasing triangle count.
  Full-resolution and float32-output validation remain
  mandatory. An immediate full-surface switch was measured and rejected: it made
  local problems much larger and exposed irrelevant internal TetGen cells.
- Bound CAT contact diagnostics separately from required physical checks. A
  diagnostic query is omitted if its worst-case triangle pairs cannot fit its
  remaining 10-million-pair allowance; summaries report incompleteness explicitly.
  Validate all supplied
  CAT meshes even if diagnostics are omitted; do not swallow physical/input errors.
  An interrupted CAT-bearing query conservatively records incomplete diagnostics
  when an exception prevents its report from reaching the engine.
- Default packing may omit TetGen cells whose finite determinant rounds to zero
  only when all four distinct, range-valid vertices belong to one participant.
  CAT never uses these cells to build inter-participant constraints. Preserve raw
  output work/limits and record omissions. Mixed-owner degeneracy, bad indices,
  nonfinite arithmetic and empty retained output remain failures. Standalone and
  reference-growth calls retain strict output validation; no points are perturbed.
- At full input object resolution, probe candidate physical validity before scale
  correction. A rejected trial can halve the colliding unfinished objects' motion
  bounds, up to four levels per object per barrier. Cap absolute volume gain at
  `0.1 * target * 2^-level` as well as the exact barrier bound. This finite increment
  avoids an asymptotic fraction-of-remaining-gap schedule. Discard the whole rejected
  batch and its RNG draws, then re-solve in the next counted engine iteration.
  Counters, diagnostics, collision and solve work remain cumulative. Exhausted
  backoffs use existing scale correction; cancellation returns the previous valid
  scene. Reference mode retains its correction policy.
- Keep the 300-second engine envelope and explicit local work/time limits.
  Expose the existing collision triangle-pair budget in the CLI for measured
  experiments. The engine physical triangle allowance is one billion; standalone
  queries retain 100 million. A measured run exhausted 100 million at the sixth
  barrier after 76 seconds, with only 1.6 seconds in correction and 74 seconds in
  solves. This raises one measured work ceiling while preserving the original
  wall-time envelope, all per-solve limits and every physical check.
- Extend summaries with optional policy/recovery/work metadata and snapshots with
  an optional exact-Hessian Boolean. Absent fields preserve historical replay.
- Do not accept failed iterates, loosen feasibility tolerances, replace Ipopt, or
  introduce dependency concurrency as part of this decision.

## Compatibility and Verification

DEV-0028 through DEV-0035 describe the deliberate changes. This supersedes the
slack-bound choice in ADR-0012 for default growth only; its transform composition
and independent checks remain. Reference-growth mode preserves earlier solver, sampling and tetrahedralization
policy; separate CAT diagnostic budgets apply to both modes.

Required evidence is the supplied `.1 -> 1.0`, ten-copy, seed-1918 run within the
300-second engine budget, exact target scales, both validation gates, and a compact
generated corpus covering irregular/slender/rotation/regular-point/non-fit cases.
Exact derivatives need finite-difference and hostile-input/work-limit tests. Run
Debug/Release/Ninja, serialization/replay, current/reference policy checks, and the
actual Studio workflow. STATUS records measured outcomes and remaining limitations;
the supplied growth gate and supported matrices pass as recorded there; repeat
measurements and limitations are retained with the evidence.
