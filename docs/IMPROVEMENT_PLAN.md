# Improvement plan after Milestone 8

Date: 2026-09-05. Status: tranche 1 is Verified with the evidence in
[STATUS.md](STATUS.md#tranche-1-repeatable-runs-and-bounded-diagnostics). Tranches 2-4 remain planned.
The requested scale target is 100-300 objects first, followed by 1,000.
Historical milestone acceptance remains recorded in [STATUS.md](STATUS.md).
This plan groups follow-up work without retroactively expanding those milestones.

## Recommendation and order

The most important engineering task is reliable growth to full size. Deliver
automatic run folders as a small independent change while gathering the solver
evidence. Establish representative performance baselines during this first work,
then optimize for 100-300 objects and extend to 1,000 on measured evidence.

| Tranche | Related outcome | Priority and dependency | Completion evidence |
| --- | --- | --- | --- |
| 1. Repeatable runs and useful diagnostics | Run again without editing paths; failures explain where and why work stopped | Immediate, small UI slice plus bounded instrumentation; enables tranche 2 | Repeated-run UI checks, destination preflight, replayable failing solve and realistic baseline harness |
| 2. Reliable packing growth | The supplied ten-object case grows from 0.1 to 1.0; bounded recovery handles demonstrated failures | Highest engineering priority; diagnosis starts alongside tranche 1 | Actual full-target success and validation on the regression corpus, with retained unsuccessful cases and documented behavior changes |
| 3. Practical 100-300-object workloads | Faster complete runs with controlled memory and usable Studio results | Main performance tranche; uses successful growth from tranche 2 | End-to-end 100/300 evidence, stage profiles, before/after work/time/memory and unchanged physical guarantees |
| 4. Extension to 1,000 objects | Carry validated throughput, memory and viewing behavior to the next scale | Follows tranche 3; architecture changes depend on profiles | Registered 1,000-object cases complete and load; measured worker/memory limits if concurrency is introduced |

Hosted CI and the missing environment-dependent checks run alongside these
tranches. They should not consume the solver/performance workstream.

## What the review found

Reviewed sources include the project/status/implementation documents, the complete
compatibility catalog, relevant ADRs, the risk ledger in
[IMPLEMENTATION_PROGRESS.md](../IMPLEMENTATION_PROGRESS.md#risks-and-follow-up),
the [Milestone 7 measurements](MILESTONE_7_RESULTS.md), current C++ paths, and the
available history of the task
[Implement Python to C++ migration](thread://01a025c4-9093-7603-ab46-75699136c7ea?hostId=local).
Current code and retained results take precedence over historical diagnoses.

| Issue | Assessment | Treatment |
| --- | --- | --- |
| Ten-object growth stops at a local iteration limit | Critical practical failure; initialization succeeds but full-size growth has no demonstrated fix | Tranches 1-2 |
| Studio reused the previous output name | Confirmed everyday workflow defect; numbered allocation is now implemented | Verified in tranche 1 |
| Output destination was checked after packing | Confirmed wasted-computation risk; early preflight is now implemented | Verified in tranche 1, including authoritative final publication checks |
| Generic solver/TetGen diagnostics; aggregate-only solve metrics | Bounded context, traces and replay are implemented; the numerical cause still needs diagnosis | Capture/replay verified; tranche 2 experiments follow |
| No representative many-object growth measurements | Real-STL multi-object harness is now implemented; 100/300 throughput remains unproven | Baseline starts in tranche 1; acceptance expands in tranches 3-4 |
| All object pairs still enumerated; exact triangle checks can be quadratic | Credible scaling candidates, not yet proven dominant in real growth | Profile in tranche 3, then accelerate the measured work |
| Repeated query construction, validation and geometry copies | Credible CPU/memory cost | Profile and reuse immutable data in tranche 3 |
| Adaptive sampling greatly refines coarse containers | Relevant to both convergence and work; disabling it alone did not fix the reported failure | Compare in tranche 2, introduce measured geometry-aware budgets in tranche 3 if justified |
| Regular points can produce invalid TetGen tetrahedra | Existing bounded recovery is incomplete and hides its original reason | Diagnostics in tranche 1; fixture-driven recovery work in tranche 2 |
| Greedy random initialization and six-orientation grid miss feasible arrangements | Demonstrated grid cases are fixed, but general search quality is incomplete | Use known-feasible failures to justify bounded restart/offset/orientation work; larger search redesign remains later |
| Studio display limits can reject larger valid results | Conditional on mesh detail, not object count alone | Measure load/render memory in tranches 3-4; retain full-quality output |
| Native dependency calls cannot be forcibly interrupted or hard memory-capped | Real architecture limitation, not a promised immediate fix | Record cancellation latency and peak memory; consider process isolation only if measured requirements demand it |
| Pre-engine failures have no saved run summary or partial work counters | Useful follow-up, less important than successful packing and automatic paths | Keep current artifact contract initially; design a separate attempt record if repeated use warrants it |
| Hosted CI, symlink test and manual file-picker checks outstanding | Verification work, not missing algorithms | Parallel acceptance track |
| GL2PS download fallback, narrow VTK patch and native-tool warning | Documented maintenance concerns with working local builds | Revisit at a dependency update or actual clean-machine failure |
| Cavities/multiple solids, editing/animation, cross-platform UI, public packaging | New capability or distribution scope | Deferred; not prerequisites for the selected counts |

The recent ten-object run failed after 19 local solves and 4,499 aggregate Ipopt
iterations. Reproduction took 22.29 seconds. Full-size structured placement of the
same objects succeeds, but performs zero growth solves. These are distinct
capabilities. See [the diagnosis](STATUS.md#supplied-ten-object-growth-diagnosis).

The measured separated 100-object collision case improved from 144.03 to 1.13 ms,
while retaining 4,950 enumerated pairs and showing no material memory improvement.
This does not establish throughput for 100-object growth or crowded geometry.

## Tranche 1: Repeatable runs and useful diagnostics

### Automatic output folders: first independent delivery

The reviewed UI called `new_run_name()` only during startup or browsing and
submitted the same text on later Run clicks. `pack_scene` constructed its output
transaction after `run_packing`, so a stale destination could waste the whole run.
Tranche 1 replaces these paths with numbered allocation and early staging.
Implementation evidence: [Studio main](../app/irop_studio/main.cpp) and
[packing application service](../src/packing/pack_scene.cpp).

Verified user flow:

1. Select a **Runs folder** once. Remember that parent across launches in bounded
   per-user settings; default to a stable user-writable IROP runs location resolved
   through Windows known folders, rather than an executable/build directory.
2. Every **Run packing** chooses a fresh child: `run-000001`, `run-000002`, etc.
   Show the proposed next destination without requiring manual editing. Keep the
   completed result's actual location separately visible.
3. Check the parent and destination before expensive preparation/solving. A
   preflight cannot guarantee later disk availability; keep the final atomic
   no-overwrite check and clear I/O diagnostics.
4. Allocate names with an exclusive reservation separate from the final directory.
   Skip existing files/directories/links and reserved names, with bounded retries.
   Never precreate the final artifact directory. Two Studio instances must choose
   distinct destinations; remove only a reservation owned by the current attempt.
5. Successful and summary-only failed runs retain their folders. Pre-engine
   cancellation/errors still publish no run directory; the next click advances
   automatically. Gaps are acceptable. On restart, continue beyond existing and
   reserved numbers; skip abandoned crash reservations safely.
6. Provide **Open result folder**. Keep the CLI's explicit exact-output contract;
   automatic Studio naming should not silently rename a CLI user's destination.

Acceptance: run twice without editing any path; repeat after failure/cancellation;
restart the application; use Unicode paths; test occupied names, concurrent
allocation and an unwritable parent; preserve existing artifacts; reject known
unusable destinations before the solver is called. Add a real desktop rerun smoke
and focused allocation/publication tests. Document the UI contract extension to
ADR-0014 during implementation and preserve ADR-0011's publication semantics.

### Diagnostics and benchmark foundation

The implementation adds failing object ID, scale barrier, engine iteration, local
limit and backend reason to summaries/UI diagnostics. It retains TetGen's original
reason and bounded recovery history. Progress distinguishes initialization attempts,
local solver iteration/time bounds, and overall engine time.

An opt-in bounded trace now records objective, primal/dual infeasibility and
termination information from the Ipopt intermediate callback. One failing local
problem can be saved and replayed without rerunning earlier objects. The API uses
project-owned types and validates resource bounds. Optional version-one summary
fields and the snapshot schema record the additive contract changes.
The existing [Ipopt C interface](https://coin-or.github.io/Ipopt/INTERFACES.html)
provides intermediate callbacks; no new solver dependency is needed for this work.

The existing harness now has a real-STL `pack` case with configurable multi-object
growth and separate preparation/initialization/solve/correction/final-validation/
export/load timing. Reports retain source hashes across the measured core/build
modules, input SHA256, settings, seed, compiler/dependencies and machine. Detailed
traces remain opt-in and bounded. See [benchmark usage](../benchmarks/README.md).

### Implemented and verified outcome

[ADR-0015](adr/0015-repeatable-runs-and-bounded-diagnostics.md) records the selected
contracts; [ADR-0014](adr/0014-native-windows-visualization-ui.md) records the
application-private allocator/settings details. The CLI continues to require an
exact new destination. The Studio parent defaults to `%LOCALAPPDATA%\IROP\Runs`;
exclusive reservations and a bounded durable sequence ledger preserve numbering
across concurrent instances, failures and restarts.

Unsuccessful runs normally contain only the summary. Enabling diagnostic capture
may also publish `failed-local-solve.json`, a bounded numeric local problem with no
packed geometry or source paths. `irop replay-local-solve SNAPSHOT` uses the same
prepared-problem validation and solver adapter. Saved-run viewing does not read or
execute that artifact. This is the narrow extension to ADR-0011's failure contract.

Integrated Debug/Release/Ninja matrices pass with 219 passed and one skipped out of
220 each; 11 desktop cases pass in Debug and Release. Exact failure capture/replay,
schema checks and three-process real-input success/failure baselines pass.
[STATUS.md](STATUS.md) records the evidence and environment limits. Tranche 1 changes run management and observability; it does not
resolve the supplied ten-object growth failure or establish throughput at 100,
300 or 1,000 objects. Those remain the explicit gates of the following tranches.

## Tranche 2: Reliable packing growth

First capture and replay the exact supplied ten-object failure. Distinguish poor
initial guesses, conditioning, near-active constraints, model/derivative errors,
and genuine local infeasibility from the trace. These are hypotheses, not diagnosed
causes. Do not choose a solver replacement or increase every work limit in advance.

Compare the smallest relevant changes in order:

1. Consistent current-state/feasible initial guesses versus the reference random
   rotation and small multiplicative scale guess. Reuse prior solve information
   only when its variables/constraints remain compatible.
2. Variable/constraint scaling and solver settings on the isolated failing problem.
   The documented [Ipopt scaling options](https://coin-or.github.io/Ipopt/OPTIONS.html#OPT_nlp_scaling_method)
   are experiment candidates, not selected production defaults.
3. A small deterministic recovery policy, if needed: bounded alternative starts
   or smaller growth increments, sharing cumulative time/work/retry budgets.
   Retain complete-batch commit and the previous valid state when an attempt fails.
4. Reproduce regular-geometry TetGen failures and preserve their cause before
   evaluating recovery. Geometry perturbation and a change from point-union to
   PLC/CDT tetrahedralization require separate comparative evidence.

For initialization, expose the distinction between direct full-size placement and
growth in the UI/help. Consider an explicit structured-first mode or bounded
restart/offset trials only when the new corpus demonstrates wasted random work or
known feasible failures. Preserve the historical seeded policy as a reproducible
reference; document deliberate policy changes rather than altering its RNG silently.

Acceptance: the supplied `.1 -> 1.0`, ten-copy, seed-1918 case completes genuine
growth with all ten objects at exact target scale and both physical validation
gates passing. Add a compact fixed corpus of seeds/shapes, including generated
irregular, slender, rotation-required, regular-point and bounded non-fit cases.
Keep the original 300-second cooperative engine budget on the baseline machine
as the first acceptance envelope, with declared per-solve and cumulative retry/work
bounds. If evidence requires a revised envelope, record it before tuning and report
the tradeoff explicitly. Use tracked generated fixtures for CI and hashes/local
paths for user meshes;
`rc/` is not a reproducible clean-checkout fixture. Report successes and failures
across the corpus. Starting at full size or lowering the target does not satisfy
this growth gate. Preserve existing verified cases or document justified deviations.

## Tranche 3: Practical 100-300-object workloads

Build profiles on complete successful workloads, then choose the highest-cost
paths. Implementation candidates, in likely order subject to those profiles:

- Reuse immutable container queries and prepared geometry; eliminate redundant
  deep copies and repeated conversions/validations. Extend workspace lifetime
  where measured: local solve buffers already exist and are reused across objects,
  but the engine recreates the workspace each iteration.
- Replace all-pairs collision candidate enumeration with a deterministic spatial
  index or sweep. If overlapping bounds dominate, accelerate triangle/containment
  queries while retaining the same strict surface/contact/nesting predicates.
- Bound index build/traversal/candidate work and intermediate memory, including
  worst-case overlap. Define new work counters explicitly: accelerated candidate
  counts cannot retain the old meaning of enumerating every possible pair.
- Measure adaptive object/container sampling separately. Avoid excessive container
  refinement and cache repeatable resampling only when geometric behavior is
  understood; final acceptance always uses full-resolution and serialized geometry.
- Measure export and Studio load/render as part of a usable result, including
  cancellation latency. Improve geometry transfer/copy costs before adding a new
  rendering representation or increasing display limits.

Acceptance: registered known-feasible 100- and 300-object scenes complete with
physical/output validation and can be opened in Studio. Require at least one
registered known-feasible genuine-growth success at each of 100 and 300 objects;
a direct-placement-only result must be labeled as that narrower capability.
Retain separate direct
placement and genuine-growth results. For each optimization retain correctness
comparisons, three independent timing samples, stage/work counters and peak memory.
Define practical time/memory targets after the first baseline on the recorded
machine, before selecting the optimization; record them in the benchmark manifest.
A faster early failure or reduced validation is not a performance improvement.

## Tranche 4: Extension to 1,000 objects

Repeat the same baseline and acceptance method at 1,000 before committing to a
larger architecture. Continue serial allocation/query improvements if they remain
the dominant costs.

If independent local solves dominate, audit the exact Ipopt/MUMPS binary for
concurrent use and oversubscription. Benchmark controlled worker counts with
separate workspaces, object-ordered random guesses, bounded memory, stable work
aggregation and complete-batch commit. Preserve a deterministic single-thread mode.
TetGen currently serializes calls; adding threads does not remove that restriction.

Measure peak working set/commit, geometry history, mesh expansion and result
loading. Use bounded display simplification or instancing only if viewing becomes
a measured bottleneck; keep full-quality output and physical checks. For the
supplied 768-face mesh, 1,000 copies plus the 12-face container produce 768,012
triangles (about 38.4 MB binary STL), below current display triangle/byte budgets.
Higher-detail inputs can reach those budgets sooner; memory and JSON limits also
need measurement rather than an unconditional increase.

Acceptance: the registered 1,000-object cases complete, validate, export and open
within the time/memory envelope recorded after baseline, including at least one
known-feasible genuine-growth case at 1,000. Direct-placement-only success does
not close this broader gate. Any parallel mode must
retain outcome/feasibility and cancellation/accounting guarantees against the
serial reference. If global tetrahedralization/CAT memory prevents that target,
write a separate regional-subdivision/aggregation design with cross-region
collision constraints and global validation before implementation.

## Measurement contract

- Counts: 10/36 for regression, then 100, 300 and 1,000. Use a small fixed seed
  corpus for reliability and three cold-process repeats for representative timing.
- Separate fixed-density scaling in larger containers from denser packing in a
  fixed container. The user's ten full-size objects occupy 16.97%; 100 of those
  objects would require 169.67% of the same container's volume. Faster code cannot
  make that particular full-size case fit.
- Include low/moderate-density known-feasible cases, challenging irregular/concave
  layouts, a bounded non-fit case and multiple mesh detail levels. Geometry fit
  witnesses and output validation establish which cases are actually feasible.
- Retain outcomes, success fraction, exact target completion, median/range wall
  time, CPU time, peak working set and commit, stage timings, local solve counts
  and iterations, tetrahedra/CAT rows, candidate/exact tests, export/load time and
  interaction/cancellation latency. Failed outcomes remain visible in statistics.
- Use Release on the recorded machine; keep inputs, policy and dependency settings
  fixed for before/after comparisons. Tranche 1 extends the benchmark harness to
  count 1,000 while retaining production checked mesh arithmetic and work limits.
  The permitted count is a measurement envelope, not a demonstrated throughput claim.
- Runtime budgets are cooperative at dependency boundaries, not hard preemption.
  Configure bounded experiments and stop advancing the scale ladder when results
  already show failure or unreasonable memory growth.
- Keep slow benchmarks outside routine CTest. Per-change verification uses the
  relevant tests, supported Debug/Release presets, Ninja clang-tidy and the checked-in
  formatter; preserve physical/output, cancellation and hostile-input regressions.

## Work to defer or leave closed

Sparse Jacobian storage is not a default next optimization: this is a set of
seven-variable local problems whose rows generally depend on all seven variables,
not a single dense 7N-variable solve. Analytic derivatives and basic workspace
reuse already exist. Measure their actual cost before redesigning them.

General continuous-orientation/backtracking search, interlocking placements,
cavity/multiple-solid semantics, a new solver/backend, GPU computation, regional
subdivision and hard process isolation remain conditional designs. Bring search
quality forward when known-feasible target cases require it; do not confuse it
with throughput at fixed density. Object editing, slicing, animation and
cross-platform UI are lower priority for the stated workflow.

Do not reopen already corrected rotation composition (DEV-0022), unbounded scale
objectives (DEV-0023), redundant completed-barrier work (DEV-0024), unsafe accepted
iterates, shared RNG/state, unawaited Python batches, or missing full-resolution/
float32 validation. Retain their tests. Vertex-centroid pivot, volume-scale semantics,
CAT-only diagnostic contacts, barrier-scaled translation and point-union TetGen
are deliberate contracts; change them only on comparative evidence.

Parallel maintenance: observe hosted CI and fix actual failures; run the skipped
symlink test on a suitable account; manually exercise native file pickers; reconcile
stale catalog status labels against existing evidence. Public packaging remains a
separate release decision under ADR-0009/0010 and is outside this improvement plan.

The original planning-only update changed no production behavior. Tranche 1 is now
implemented under ADR-0015; its final verification belongs in STATUS and the detailed
progress ledger. Add compatibility records for later algorithm changes and ADRs
when selecting new public/architecture contracts.
