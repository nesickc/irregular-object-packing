# ADR-0011: Milestone 5 Packing Outcome and Artifact Contract

- Status: Accepted
- Date: 2026-08-22
- Deciders: Project maintainers

## Context

Milestone 5 composes initialization, adaptive sampling, TetGen, CAT, Ipopt,
collision correction, termination, and multi-file publication. The Python
reference has no bounded or machine-readable end-to-end failure contract: it
can advance past an unconverged barrier, repeat collision correction forever,
apply failed solver iterates, and miss wholly outside or nested intersections
when surfaces do not cross.

The first C++ packing CLI must remain diagnosable without presenting partial or
invalid geometry as a successful packing result.

## Decision

- Keep orchestration in a reusable project-owned packing module. The CLI only
  parses options, reports progress, handles interruption, and maps outcomes to
  process exits.
- Run the parity algorithm deterministically in object-index order. Continue
  the initialization run's owned NumPy-compatible MT19937 stream for each
  local-solve rotation guess.
- Preserve the reference's linear sequence of volume-scale barriers, barrier
  based surface-sampling policy, `0.93` collision scale reduction, `0.99`
  tetrahedralization-recovery scale reduction, solve-then-clamp behavior, and
  exclusion of CAT contacts from scale-correction selection.
- Use VTK privately for adaptive closed-surface resampling. Preserve the
  reference target-count formulas, but validate every resampled mesh and use
  the full-resolution source/container geometry for physical acceptance.
- Treat object/container containment and object/object overlap as physical
  validity requirements. CAT contacts remain diagnostic compatibility data.
- Track explicitly whether final physical validation ran. A run summary reports
  `not_run` with a null physical-validity value until it did; after validation it
  reports `passed`/`true` or `failed`/`false`, even if a later pre-commit
  cancellation changes the overall outcome.
- Before publishing success, quantize the final objects and container through
  the same binary-STL conversion used by the writer and repeat physical
  validation on those exact serialized coordinates. A quantization-created
  contact or containment violation produces a structured unsuccessful summary
  and no geometry or placements artifacts.
- Bound scale-step iterations, correction passes, history, local solves,
  collision work, generated geometry, nested adapter work, and elapsed time.
  Check cancellation between input-preparation stages, bounded initialization
  attempts and validation objects, and every coarse packing-stage boundary.
  Continue polling between staged artifact operations through the final
  pre-commit boundary; if observed after engine success, discard every staged
  success artifact and publish only a cancelled summary. In-process VTK reads
  and remeshing, TetGen, collision queries, MUMPS, and individual file-write
  calls remain non-preemptible while active.
- Preserve an already-final unsuccessful engine outcome if interruption arrives
  only during its summary publication. Late cancellation exists to prevent a
  would-be success artifact set from being committed, not to erase an earlier
  resource, infeasible, or dependency diagnosis.
- Default packing-local Ipopt work to 1,000 iterations per object solve while
  retaining a configurable limit. The preserved unbounded scale objective needs
  527 iterations on the representative tetrahedron/cube growth fixture; the
  standalone local-solver diagnostic default remains 200.
- When the base translation bound is omitted, resolve it to twice the cube root
  of the full-size object volume instead of preserving Python's effectively
  unbounded `None`. An explicit positive bound still wins, and the resolved base
  remains subject to the reference-compatible barrier multiplication recorded
  by `IROP-COMPAT-0007`.
- Return project-owned outcomes for success, cancellation, invalid input,
  resource exhaustion, infeasibility, iteration or correction exhaustion,
  elapsed-time exhaustion, numerical failure, dependency failure, and internal
  failure.
- A successful `irop pack` atomically publishes a new directory containing
  `packed-objects.stl`, `container.stl`, `placements.json`, and
  `run-summary.json`, plus optional individual object STLs.
- An expected unsuccessful algorithm run atomically publishes only
  `run-summary.json`, with a non-success category and null geometry/placement
  output paths. Input, usage, and output-path failures publish no run directory.
- Cancellation before a valid packing state or engine result exists exits 130
  without a run directory. Cancellation once an engine result exists is an
  expected unsuccessful algorithm outcome and publishes only its summary.
- The version-one run-summary schema enforces this distinction conditionally:
  success requires the fixed relative geometry/placement paths and passed
  physical validation, while every non-success outcome requires null
  geometry/placement paths and an empty individual-STL list.
- Use process exit 0 for success, 4 for resource/time exhaustion, 6 for another
  structured unsuccessful packing outcome, and 130 for cancellation. Existing
  usage/input/output/internal exit categories remain unchanged.

## Consequences

- A future UI can consume progress, result, work, validation, and history types
  without invoking the CLI.
- Physical success is stronger than the Python surface-contact test and cannot
  be inferred solely from sampled geometry.
- The success guarantee applies to the float32 coordinates stored in the binary
  STL artifacts, not only to the higher-precision in-memory scene.
- Consumers can distinguish validation that did not run from a performed check
  with no reported violations; a later cancellation does not erase completed
  validation evidence.
- Failure summaries are useful automation artifacts but cannot be mistaken for
  packed geometry because no success STL or placements file is published.
- Deterministic single-thread execution is the initial compatibility baseline.
  Parallel local solves require a later measured concurrency and reproducibility
  decision.
- Exact resampled triangulations can differ from Python's Trimesh-first path,
  while target counts, final full-resolution validation, and output transforms
  remain explicit.

## Alternatives Considered

### Throw for every unsuccessful packing run

Rejected because expected infeasibility, cancellation, and bounded termination
need structured diagnostics and a stable machine-readable result.

### Publish partial geometry on failure

Rejected because automation or users could mistake an intermediate scene for a
valid packed result.

### Validate only resampled surfaces

Rejected because decimation and smoothing can hide intersections or boundary
violations in the full-resolution output geometry.

### Parallelize per-object solves immediately

Deferred because deterministic random-draw order, MUMPS concurrency, workspace
ownership, and throughput have not yet been measured together.

## Verification

- Packing-engine tests cover the first linear barrier and cancellation, derived
  and explicit translation bounds, invalid configuration and nested limits,
  adaptive resampling followed by bounded resource failure, deterministic random
  draw continuation, and genuine TetGen/CAT/Ipopt growth followed by performed
  full-resolution validation.
- Collision tests cover separated, crossing, touching, nested, and
  containment-only scenes, CAT-only diagnostics, deterministic pair order, and
  cumulative collision-work budgets.
- Packing-application tests cover the complete success artifact set,
  summary-only resource failure, summary-only cancellation after engine success,
  cancellation before input resolution with no artifacts, invalid configuration
  before input resolution, malformed-input and no-overwrite behavior, explicit
  final-validation state, and the conditional version-one schema contract.
- The CLI smoke test executes genuine growth, inspects the four success
  artifacts, and verifies a resource-exhausted exit with only a non-success
  summary.
