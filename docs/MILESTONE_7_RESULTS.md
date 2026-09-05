# Milestone 7: first measured results

Measured on 2026-09-05. The first Milestone 7 scope adds a bounded structured
initialization fallback and skips exact collision work for objects with strictly
separated mesh bounds. The dense initialization cases now succeed, established
sparse placements remain exact, and the largest separated collision case takes
about 1.13 ms instead of 144.03 ms in this matrix.

The tracked [machine-readable evidence](../benchmarks/results/windows-20260905.json)
retains all 66 total-time/CPU/peak-memory samples, stage medians, input/configuration
metadata, outcomes, invariant work counters, and placement comparison results.
[Benchmark instructions](../benchmarks/README.md) describe the harness and runner.

## Conditions and provenance

- Visual Studio 2026 Release, MSVC 19.51.36252.0, Windows x64.
- AMD Ryzen 9 9950X; 32 logical processors; 66,155,331,584 bytes of physical memory
  reported by Windows. The CPU model was captured by the changed harness; the
  earlier baseline reports contain the matching architecture, processor count,
  and memory fields but no CPU model field.
- VTK 9.3.20231030, TetGen 1.6.0, Ipopt 3.14.19, Eigen 3.4.1.
- Three independent cold processes per case and build, run serially; the baseline
  matrix ran before the changed matrix. Orchestration used one thread and retained
  dependency thread defaults. Process CPU time includes dependency worker threads.
- Baseline: production commit `02961e785c730ad47c810da16df15fc966d835ef`, built with
  the initial benchmark harness before production edits. Its locally retained
  `irop_benchmarks-m6.exe` is a measurement artifact, not a distributed executable.
- Changed: uncommitted Milestone 7 source over that same HEAD. The reports capture
  initialization SHA256 `04ae7eecc926be6274ff7ae96af10d24742d712e68f7efbdac8dac26a9905173`
  and collision SHA256 `4d621aebbde87f4826e409e8beb7ea4396102a084e20126bf426f2bcdf7bff3f`.
  The unchanged Git HEAD field must not be mistaken for unchanged production code.
  Baseline source hashes were not captured by its earlier harness; the aggregate
  identifies its production Git blobs and records this limitation.

Wall times below are median case times, including generated-input setup and
excluding JSON report serialization. Peaks cover each process's lifetime up to the
measurement snapshot, including startup and dependency loading. Very short CPU
measurements often read zero because Windows process accounting is coarse. Three
cold samples support this local comparison; submillisecond differences and small
peak-memory changes are not treated as improvements.

## Initialization

The generated cylinder has diameter 90.46, height 105.507, 12 radial segments,
24 vertices, and 44 triangles. The container is a 350 x 400 x 285 box with 12
triangles. Every run used seed 1918, one million reference sampling attempts, and
a 10,000 ms cooperative timeout. The new fallback allowed 100,000 structured
candidates after the reference attempt limit.

| Case | Before outcome | After outcome | Before wall ms | After wall ms |
| --- | --- | --- | ---: | ---: |
| 10 objects, volume scale 0.1 | Success, 3/3 | Success, 3/3 | 0.0782 | 0.0802 |
| 36 objects, volume scale 0.1 | Success, 3/3 | Success, 3/3 | 0.2459 | 0.2576 |
| 10 objects, volume scale 1.0 | Resource limit, 3/3 | Success, 3/3 | 278.0990 | 278.3951 |
| 36 objects, volume scale 1.0 | Resource limit, 3/3 | Success, 3/3 | 278.2885 | 278.9067 |

All six placement arrays for each sparse case compare exactly after parsing the
JSON: every scale, rotation, translation, and ordering is preserved. Their existing
random draws and geometry work also match: 99 draws/116 pair checks for count 10,
and 819 draws/4,717 pair checks for count 36. Both changed runs report
`random_rejection` with zero structured candidates.

The old dense runs exhausted the attempt limit after placing six objects. The new
runs first consume the same one million attempts and three million random draws,
then restart deterministically with `structured_grid`. Count 10 uses ten structured
candidates and one orientation; count 36 uses 36 candidates and two orientations.
The latter rotates the cylinders by pi/2 about y and produces a 3 x 4 x 3 layout.
The returned initialized states pass the library's strict geometry validation.
These are initialization measurements; final binary-STL publication is covered by
the packing application's separate acceptance checks.

This is a search-success improvement at approximately the same elapsed time. The
reference search still accounts for almost all of the roughly 278 ms. Its failure
was a limit of that search, not geometric infeasibility. Baseline failure reports
have no partial-state work object because the original API throws; the reported
six-object prefix comes from its diagnostic, not invented counters.

## Collision work

The collision cases place separated cylinders along a row in a surrounding box.
Both versions return physically valid scenes in all three repeats. Segment counts
12, 48, and 96 produce 44, 188, and 380 object triangles respectively.

| Objects | Segments | Before wall ms | After wall ms |
| ---: | ---: | ---: | ---: |
| 10 | 12 | 1.4529 | 0.1631 |
| 36 | 12 | 18.5782 | 0.4485 |
| 100 | 12 | 144.0279 | 1.1271 |
| 10 | 48 | 23.2644 | 0.5297 |
| 10 | 96 | 93.7278 | 1.1332 |

For 100 objects, the measured whole-case improvement is about 128 times. The
collision stage alone changes from 143.8496 to 0.9731 ms. Both versions enumerate
4,950 object pairs, but triangle tests fall from 9,636,000 to 52,800 and containment
triangle visits from 436,800 to 1,200. Remaining exact work includes each
object/container check. Median process peak working set changes from 16,850,944 to
16,871,424 bytes; these measurements show no material memory reduction.

Strict axis-aligned mesh-bound rejection avoids the expensive checks only when
separation is proven. Touching and overlapping bounds, including possible nesting,
continue through the exact predicates. Pair enumeration remains quadratic and now
has its own configurable work limit, including rejected pairs. This result applies
to separated populations; it is not a claim about equally large dense or touching
scenes.

## Other measured stages

The independent stage case resamples one tetrahedron to 16 triangles and its box
to 48, then exercises a local growth solve and writes an STL. It produces the same
107 tetrahedra, 376 CAT constraints, 15,600 repeated constraint/gradient rows, seven
solver iterations, and 884 serialized bytes in both versions.

| Stage | Before wall ms | After wall ms |
| --- | ---: | ---: |
| Transforms | 0.0058 | 0.0063 |
| VTK resampling | 1.1033 | 1.1208 |
| TetGen | 0.1323 | 0.1436 |
| CAT construction | 0.0776 | 0.0764 |
| Constraint and analytic gradient | 2.2442 | 2.2566 |
| Ipopt solve | 13.8597 | 15.1856 |
| Collision | 0.0302 | 0.0394 |
| STL serialization | 1.1978 | 1.2146 |
| Whole stage case | 18.6552 | 20.3143 |

The real adaptive engine case grows one tetrahedron from volume scale 0.1 to 0.2.
It succeeds with identical final placements and work in all six reports: two
resamples, one TetGen call, one CAT build, one local solve, and ten solver
iterations. Its median whole-case time changes from 47.6749 to 52.3470 ms and
median process peak working set from 27,422,720 to 27,299,840 bytes. These cases
establish stage and engine baselines; they do not demonstrate a solver or general
packing speed improvement.

## Supplied STL validation

The actual supplied object, `rc/input_models/ulamok_2kg_simplified.stl`, has 386
vertices and 768 triangles; its container, `rc/containers/10_kg_np.stl`, has eight
vertices and 12 triangles. Their SHA256 hashes and compact run records are retained
in the aggregate separately from the generated benchmark matrix.

`irop pack` with seed 1918, initial/final volume scale 1.0, one scale step, and
adaptive sampling disabled succeeded for both 10 and 36 copies. Each run published
`packed-objects.stl`, `container.stl`, `placements.json`, and `run-summary.json`.
Every object has exact scale 1.0, and full-resolution plus binary-STL-coordinate
validation passed with no contacts or containment violations. The 36-object scene
has packing fraction 0.6108283599. Because initialization already reaches the target,
both runs perform zero packing iterations, TetGen calls, CAT builds, or local solves.

The retained summaries are `build/manual-m7-exact-ten/run-summary.json` and
`build/manual-m7-exact-36/run-summary.json`. A separate `initialize` invocation at
count 10 with only 100 reference attempts also succeeded through the structured
fallback (`build/manual-m7-initialize-ten/run-summary.json`). Disabling fallback
for the full-scale count-10 command retained the reference failure: exit 4 after
one million attempts and six accepted objects, with no output directory; its log
is `build/manual-m7-reference-disabled.log`. These are successful actual-input
smoke checks, not additional repeated performance samples.
## Reproduction

The recorded runs used the checked-in runner with three repeats:

```powershell
./benchmarks/run-windows.ps1 -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks-m6.exe -OutputDirectory build/benchmarks/m7-baseline -Repeats 3 -Label milestone6
./benchmarks/run-windows.ps1 -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe -OutputDirectory build/benchmarks/m7-after -Repeats 3 -Label milestone7
```

Build the current executable with the documented `windows-vs2026` preset and
`IROP_BUILD_BENCHMARKS=ON`; use a new output directory for every matrix. The local
baseline executable is not present in a clean checkout. To recreate that side,
use a separate checkout/worktree at `02961e785c730ad47c810da16df15fc966d835ef` and
apply only the benchmark harness and its CMake target integration. Keep the
baseline production sources unchanged. Adapt only the current harness's measurement boundary for the older public API:

1. In `Options`, replace `PackingConfig::default_max_structured_candidates` with
   the harness literal `100'000`; the old library has no corresponding setting.
2. Remove assignments to `PackingConfig::enable_structured_fallback` and
   `PackingConfig::max_structured_candidates` in both initialization and growth
   case setup. Omit those two configuration entries from baseline report output.
3. In `initialization_work`, omit `initialization_method` and its `to_string`
   call, `sampling_attempts`, `structured_candidates`, `orientations_examined`,
   and `reference_accepted_count`. Retain the existing accepted-object, RNG,
   rejection, geometry-query, pairwise-distance, and surface-intersection counters.
4. Do not pass the new fallback flags to this baseline adapter. Keep geometry,
   seed, limits, stage calls, timer boundaries, and all production files unchanged.


The runner's default flags work with both recorded executables. On the current
build, `-DisableInitializationFallback` measures the preserved reference initializer
alone. It still uses the new collision implementation and therefore cannot
reconstruct the historical collision baseline. Compare release builds using the
same compiler, dependency resolution, machine, thread defaults, and case matrix.
The aggregate records enough source/environment/work information to distinguish
these comparisons from the original measurements.

## Completed scope and remaining limits

This completes the first measured scope of Milestone 7: reproducible timing/memory
and stage baselines, bounded recovery of the demonstrated dense initializer jam,
and a measured collision improvement with preserved successful reference seeds.
[ADR-0013](adr/0013-bounded-structured-initialization-and-collision-broad-phase.md)
records the design and compatibility decisions; current correctness/build evidence
is maintained in [STATUS.md](STATUS.md).

The initializer is a conservative grid heuristic using six fixed right-angle
orientations and shared candidate/geometry/pair-work budgets. It does not provide
continuous orientation search, backtracking, or a guarantee of finding every
feasible layout. Collision candidate enumeration remains quadratic. Sparse solver
derivatives, additional workspace optimization, controlled concurrency, object
aggregation, and regional subdivision remain future measured work. No concurrency
or aggregation mechanism was added by this scope.