# ADR-0017: Measured Solver Thread Control and Scaling

- Status: Accepted; implemented and locally verified for the registered scope
- Date: 2026-09-07
- Deciders: Project maintainers

## Context

The maintainer authorized tranche 3 for practical 100-300-object workloads.
Tranche 2's supplied ten-object growth succeeds at 221.49 seconds median, with
207.97 seconds in local solves and roughly 16 process CPU seconds per wall second.
The pinned Ipopt/MUMPS runtime embeds Intel MKL 2024.1 and uses libiomp5md.
Isolated replays requesting one OpenMP thread roughly halve wall time and retain
all 128 historical trace samples. A complete supplied ten-object pilot succeeds
in 115.117 seconds with both physical gates; its final placements and work differ
from the default-thread run. Floating-point path equivalence is therefore tested
by geometry/outcome invariants for complete packing, not byte-identical placements.

The first known-feasible generated 100-object `.1 ->1.0` pilot completes all nine
barriers in 173.747 seconds, using 65.3 MB peak working set and 72.1 MB peak commit.
That pilot used the preliminary v1 container; controlled scaling comparisons use
v2 containers with constant aspect/shear and uniform count-based scaling. The
[registered measurement plan](../../benchmarks/tranche3-plan.json) fixes the
100/300 runtime, memory and correctness targets before further optimization.

## Decision

- Keep deterministic object-ordered, complete-batch packing. Add no independent
  local-solve workers and no new dependency.
- Expose `solver_openmp_threads` for packing and `openmp_threads` for standalone
  local requests. Packing requests one by default; zero inherits the caller's
  task setting. Requests are bounded at 256. Old snapshots without the field and
  ordinary standalone requests retain zero for historical replay.
- Resolve get/set functions only from the pinned already-loaded OpenMP runtime.
  Set the current task's requested maximum around dependency creation, solve and
  destruction, then restore its previous setting on success, failure and exception.
  Do not mutate process environment or search for a replacement DLL.
- Report requested, inherited and scoped OpenMP maximum settings separately from
  bounded ambient `MKL_NUM_THREADS` and `MKL_DOMAIN_NUM_THREADS` overrides. An
  OpenMP readback is not a measurement of actual MKL workers. Overlong/nonprintable
  override values remain explicitly unavailable; their presence is retained.
- Attribute local wall time to preparation, dependency setup, the inclusive solve
  and independent postchecks. Constraint/Jacobian/Hessian callback times are nested
  subsets of solve time. Record integer nanoseconds, including partial failed work,
  with checked aggregation and additive optional version-one JSON fields.
- Register generated irregular/slender/concave shapes and surface-preserving detail
  variants with independently checked full-size float32 fit witnesses. Keep
  fixed-density container scaling separate from increasing density in one fixed
  container. Successful generated cases do not certify arbitrary user inputs.
- Preserve all original feasibility predicates, exact target scales, full-resolution
  physical validation, serialized-output validation and atomic publication.
  Keep historical performance evidence and every attempted failure. Select further
  copy/workspace/collision changes only when profiles justify their cost.

OpenMP defines its setter in terms of the current task's control variable; see
[the OpenMP specification](https://www.openmp.org/spec-html/5.1/openmpsu120.html).
Intel documents that MKL-specific settings override OpenMP controls; the isolated
probe also confirms four actual MKL workers with `MKL_NUM_THREADS=4` despite an
OpenMP readback of one. See [Intel's thread-control rules](https://www.intel.com/content/www/us/en/docs/onemkl/developer-guide-windows/2024-1/techniques-to-set-the-number-of-threads.html).

## Immediate physical retry reuse

The controlled v2 100-object baseline completes in 155.727 seconds; scoped threads
reduce that to 72.744 seconds with the same 2,559 local solves and 150 iterations.
At 300 objects, inherited threads reach only three of nine barriers before the
300-second engine limit, and scoped threads reach seven. Callback evaluation is
less than 1.8 seconds of the latter run's 266.134 seconds in the dependency solve.
Rejected batches account for 57% of local calls at 100 and 65% in the baseline
300-object partial run. This selects reuse of redundant retry work before a new
collision hierarchy or callback optimization.

DEV-0037 adds `reuse_physical_retry_results`, enabled for default full-input growth
and disabled by `--no-physical-retry-reuse` or reference-growth policy. A single
barrier-local owner moves the rejected trial's TetGen result, CAT result/surfaces
and at most one successful transform per object into the immediately following
retry. The committed scene and solver policy are unchanged. Bounds changes clear
the affected object's result; every new candidate still receives the complete
physical check. Successful commit, correction fallback, resampling, recovery and
barrier transitions discard the retained context. The cache does not persist
across runs or add an unbounded history.

Cached logical object requests preserve object order, progress callbacks, three
RNG draws and cancellation checks. The candidate remains transaction-local until
all acceptance gates pass. `local_solves`, dependency work and timing count actual
execution; `reused_local_solves` and `reused_prepared_batches` separately report
avoided work. The local-solve budget applies to actual calls; iteration, physical
collision and deadline budgets still apply to every retry. Diagnostics contain
actual solves only, so fewer records and different work-limit stopping positions
are intentional. Independent numerical calls are not promised bit-identical under
all dependency settings, but no geometry acceptance rule changes.

## Rejected collision-index experiment

The secondary supplied-STL 100-object case in a proportionally larger container
exhausts the physical triangle-pair allowance after three of nine barriers in
69.517 seconds. Its report accounts for 969,651,140 completed narrow checks before
the interrupted query. This remains a limitation of the delivered policy.

A bounded scene-local triangle index was implemented and evaluated without changing
the exact VTK predicate. Plain triangle AABB pruning was unsuitable for the pinned
VTK tolerance branches; the prototype used conservative same-side plane
certificates and exhaustive fallback for uncertain arithmetic. Independent oracle,
contact/nesting and resource-bound tests passed, but the measurements rejected it:
the primary 100-object pilot slowed from 33.559 to 35.827 seconds, and supplied100
still failed after four barriers at 265.862 seconds, including 132.684 seconds of
collision work and exhaustion of the node-visit allowance. Fewer narrow tests did
not produce a useful end-to-end improvement.

The prototype, its public options/counters and its tests were removed. Both measured
reports remain in the tranche-3 ledger. The measured geometry source is preserved
under `build/tranche3-rejected-triangle-index/source/compiled-index`; its four hashes
match the report. A subsequent container-prepass draft is separately labeled
uncompiled, untested and unmeasured. It is not part of the delivered implementation.
The original physical predicates and budgets remain unchanged. Efficient conservative
collision acceleration for heavier meshes is a priority before generalizing the
100/300-object result or proceeding to 1,000-object performance claims.

The known-fit slender case is a separate convergence limitation: the baseline,
thread-only and cached paths stop with identical poses and physical work at the
0.7 barrier. Retry reuse does not cause that stall.

## Compatibility and verification

DEV-0036 records the intentional packing dependency-thread policy change. The
reference-growth switch retains its mathematical/sampling policies; set explicit
solver threads to zero as well when comparing the previous complete runtime policy.
Historical snapshots remain readable and replay with inherited settings.

Verify scope restoration, malformed requests/metadata, inherited snapshot behavior,
current/reference outcomes, timing accounting on successful/failed/fixed-point
solves, cumulative limits, and all existing geometry/publication/cancellation
regressions. Run supported Debug/Release/Ninja matrices and real Studio workflows.
The primary 100/300 cases require three full-growth successes each, both validation
gates, export/loading/rendering and the registered time/memory envelope. Record
higher-detail and supplied-STL limitations instead of generalizing a small fixture.
STATUS and the final benchmark ledger record actual results and remaining risks.

## Recorded outcome

The final controlled 100-object median is 33.484 seconds versus 155.805 seconds
for tranche 2 (4.65x); 300 objects complete in 125.446 seconds median. Each final
count succeeds in three independent processes with both physical gates, export and
loading, and actual Studio rendering/cancellation checks pass. The three supported
build/test matrices each have 252 passed and one existing symlink skip. See
[STATUS](../STATUS.md) and the [complete ledger](../../benchmarks/results/windows-20260907-tranche3.json)
for provenance, memory/work results and the heavier-mesh and slender-case limits.
Hosted CI acceptance remains a separate pending Milestone 6 gate.
