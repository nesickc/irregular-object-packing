# ADR-0013: Bounded Structured Initialization and Collision Broad Phase

- Status: Accepted
- Date: 2026-09-05
- Deciders: Project maintainers

## Context

Milestone 6 established a local Python/C++ parity corpus. Dense full-scale
initialization can still jam at six accepted centers for ten requested cylinders,
although explicit layouts fit ten bounding spheres or 36 oriented cylinders.
Repeating the same greedy search cannot relocate its accepted centers. Collision
validation also performs triangle and containment work for clearly separated
object bounds. Milestone 7 proceeds against the recorded local parity baseline;
the outstanding hosted CI acceptance remains separately recorded in Milestone 6.

## Decision

- Preserve the complete existing origin shortcut and random rejection search,
  including successful transforms, random draws, and work accounting.
- After candidate-attempt exhaustion only, enable a deterministic structured
  restart by default. `--no-initialization-fallback` retains the original bounded
  failure behavior. Cancellation or exhaustion of geometry, pair, or surface
  budgets never triggers another search with renewed resources.
- Try six axis permutations of the actual scaled object bounds on centered
  rectangular grids in the container AABB. Validate every actual object against
  the closed container, including surface intersections; an AABB fit alone is
  insufficient for a concave container. Keep strictly disjoint object envelopes.
- Use finite checked grid arithmetic and a margin of eight float32 epsilons times
  the largest axis coordinate magnitude or object extent. It is a conservative
  serialization margin in input units, with no fixed-unit tolerance.
- Bound grid candidates globally across orientations with
  `max_structured_candidates` (default 100,000). Share existing geometry, pair and
  surface budgets with the original search and final validation. Poll cancellation
  throughout project-controlled loops. Preserve RNG state on the grid path.
- Record initialization method, random attempts, accepted reference prefix count,
  grid candidates and orientations. Independent validation reconstructs geometry
  and envelopes rather than trusting method metadata as evidence of feasibility.
- In version-one summaries add optional configuration/work fields, accepting
  historical documents. Structured results use `structured-aabb-grid` and null
  sphere-clearance metrics, since those sphere distances are no longer guarantees.
- In collision validation, reject object pairs only when validated AABBs have a
  strictly positive separating gap. Touching or overlapping bounds retain exact
  surface and nesting checks. Preserve pair enumeration and violation order.
- Add `max_object_pair_checks` (default 100 million) and enforce it cumulatively
  through engine and serialized-output validation. Skipped narrow-phase work must
  not remove the bound on quadratic pair scanning.
- Keep measurements separate from normal tests in an opt-in standard-library
  benchmark executable. Record cold-call wall/CPU time, process peak memory,
  configuration, mesh sizes, source hashes, environment, and work counts; use
  fresh processes and repeated samples. No new dependency or concurrency is added.

## Consequences

The initializer can recover previously unsuccessful dense scenes without changing
successful seeded reference results. A grid failure still does not prove geometric
infeasibility. Six common orientations, conservative envelopes and centered grids
are a bounded heuristic, not general orientation search, backtracking or optimal
packing. Lower random attempt bounds trade reference-search opportunity for earlier
fallback. Dense/high-resolution inputs may still exhaust the shared budgets.

Collision work is reduced for separated scenes while worst-case pair enumeration
remains quadratic and explicitly bounded. TetGen and MUMPS remain serialized or
cooperative as previously documented. Aggregation, regional subdivision and
parallel local solves remain deferred until measurements justify their complexity.

## Verification

Use the Milestone 7 evidence in `docs/STATUS.md` and `docs/MILESTONE_7_RESULTS.md`.
Regressions cover successful reference seeds, ten/36-cylinder layouts, strict
serialized geometry, concave and shifted containers, forged state, numeric limits,
interruption, touching/nested collisions and cumulative publication budgets.
`IROP-DEV-0026` and `IROP-DEV-0027` record the changed failure behavior.
