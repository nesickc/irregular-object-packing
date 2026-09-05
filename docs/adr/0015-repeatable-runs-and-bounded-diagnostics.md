# ADR-0015: Repeatable Runs and Bounded Diagnostics

- Status: Accepted; implemented and verified
- Date: 2026-09-05
- Deciders: Project maintainers

## Context

The maintainer authorized tranche 1 of the improvement plan. Studio reuses its
output name on a second Run, and the packing service discovers an unusable output
destination only after computation. A supplied ten-object growth failure has an
iteration-limit outcome but lacks the per-object evidence needed for numerical
diagnosis. Existing tiny benchmarks do not represent many-object growth.

## Decision

- Studio selects and persists a runs parent in bounded per-user settings, resolves
  a stable Windows known-folder default, and allocates a fresh numbered child for
  each Run. Preference-write failure warns while preserving a usable session parent;
  unsafe settings metadata is never replaced. Keep the actual completed result path separate from the next run.
- Reserve names independently of the final artifact directory, with exclusive
  ownership, bounded allocation and durable progress across cancellation/restart.
  Skip occupied/reserved names, tolerate number gaps, and never replace artifacts.
- Prepare the packing service's private staging transaction after configuration
  and initial cancellation checks, before loading meshes or solving. A failure
  cleans private staging; the final run directory remains absent until commit.
  Parent directories may be created while preparing the destination. Writability
  at preparation is not a guarantee of later disk availability.
- Retain the CLI's exact requested output identity and final atomic no-overwrite
  publication. This changes the precedence of simultaneous input/output errors:
  an invalid destination is diagnosed before input loading.
- Add bounded local failure context and TetGen recovery reasons. Detailed local
  records/traces and one failed local problem are opt-in, with explicit limits
  and omission counters. Diagnostic exhaustion must not change solver acceptance.
- Expose a project-owned prepared-local-problem replay API through the same
  validation and Ipopt adapter as the CAT-backed path. Snapshots are bounded,
  versioned JSON with numeric data only. Never follow recorded input paths or
  treat replay success as certification of a complete packing.
- Extend version-one packing summaries with optional diagnostics, configuration,
  timing and snapshot-path fields. Historical records remain readable. The fixed
  optional `failed-local-solve.json` artifact contains a local optimization problem,
  not packed geometry. An unsuccessful run without capture remains summary-only;
  captured failures may publish this diagnostic sibling as well.
- Keep success geometry, placements, full-resolution/float32 acceptance, RNG,
  scale barriers, solver settings and failure categories unchanged. A snapshot
  write failure is reported as omitted diagnostics without losing the failure
  summary. Saved-run viewing does not automatically read replay snapshots.
- Instrument engine stages and application preparation/export separately. Preserve
  the historical summary initialization timing (which includes input preparation)
  and add explicitly named preparation/placement timing fields. Complete
  application time, measured through commit, belongs in the returned result and
  benchmark report rather than a self-referential pre-commit summary timestamp.
- Extend the existing benchmark harness to configurable real-mesh packing and
  saved-result loading; record source/input hashes, complete settings, outcomes,
  stage work/time and process memory. No new geometry/solver dependency is added.

## Consequences

This extends ADR-0011's unsuccessful artifact contract only for explicitly enabled
local diagnostics and ADR-0014's output-selection workflow. It does not implement
tranche 2's convergence fixes or demonstrate 100/300-object growth performance.
Input/initialization failure and pre-engine cancellation retain no final run
directory. Internal Studio numbering/settings metadata are not packing results.

Replay and diagnostic collection have bounded overhead; detailed capture is not
the default throughput path. Timing remains cooperative at dependency boundaries,
and the saved limit values and truncation information must be visible to users.

## Verification

Required evidence: repeated-run desktop smoke and allocator concurrency/restart/
Unicode tests; early output rejection and late collision protection; default
solver equivalence, bounded trace/snapshot parsing and replay; exact supplied
failure capture/replay; representative benchmark success/failure and provenance;
Debug/Release/Ninja analysis, formatter and affected regression suites. STATUS.md
records actual results: Debug/Release/Ninja each pass 219 tests with one documented
symlink skip out of 220; 11 desktop cases pass in both Debug and Release; exact
capture/replay traces and schemas match; six real-input benchmark reports retain
three direct successes and three growth failures. Final analysis and formatting pass.
