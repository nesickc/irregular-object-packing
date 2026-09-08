# Packing measurements

The optional `irop_benchmarks` executable runs one bounded case per process and
writes a new JSON report. Enable it through the supported Windows preset:

```powershell
cmake --preset windows-vs2026 -DIROP_BUILD_BENCHMARKS=ON
cmake --build --preset windows-vs2026-release --target irop_benchmarks
build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe init-dense --count 10 --segments 12 --attempts 100000 --seed 1918 --label baseline --output build/benchmarks/baseline-dense10.json
```

Reconfigure before measuring changed source: revision and hashes of all project-owned
core sources/headers, benchmark sources, CMake files and manifests are captured at
configure time. Measured source edits also trigger CMake regeneration during a normal build. Give baseline and changed runs
distinct labels and report paths. Run each case at least three times in separate
processes on an otherwise idle machine; compare medians and retain every report.
Timing thresholds do not belong in ordinary CTest correctness tests.

| Case | Workload |
| --- | --- |
| `init-sparse` | Seeded cylinders at volume scale 0.1 in a box; use `--initial-scale` to override. |
| `init-dense` | The dense full-scale cylinder proxy, diameter 90.46 and height 105.507, in a 350 x 400 x 285 box. Run counts 10 and 36. |
| `collision` | Separated cylinders arranged along a row in a larger box; compare counts 10, 36, and 100 and segment counts 12, 48, and 96. |
| `stages` | Independently timed resampling, transforms, TetGen, CAT, constraint/analytic-gradient evaluation, Ipopt, collision, and STL writing for one local growth pipeline. Also writes an STL beside its report. |
| `growth` | The real adaptive packing engine growing one tetrahedron from volume scale 0.1 to 0.2, with physical acceptance and work counters. |
| `pack` | Real object/container STL inputs, configurable multi-object growth or direct placement, atomic artifacts, and the same saved-run loader used by Studio. |

All cases accept `--output`, `--label`, `--count`, `--segments`, `--attempts`,
`--seed`, and `--timeout-ms`. Initialization also accepts `--initial-scale`.
Stages and growth fix count and geometry in the report. Defaults are count 10,
12 cylinder segments, 100,000 sampling attempts, seed 1918, and 10,000 ms.
Harness bounds are count 1,000, segments 512, attempts 10,000,000, and timeout 300,000 ms;
these bound deliberate diagnostic workloads, not the product's input domain.

The initializer timeout is cooperative. Growth and local solves use their normal
time/work bounds. Collision, mesh resampling, and TetGen calls cannot be preempted
inside the dependency; their input sizes remain bounded by the harness and library.
The timeout is not a hard process deadline. Use modest cases first.

Wall and process CPU measurements exclude report formatting and writing. Each stage
is a cold first invocation in that process; repeated constraint evaluation is
explicitly counted. Windows CPU accounting has coarser resolution than the wall
clock, so a very short stage may report zero CPU milliseconds. Peak working set and
peak commit are process-lifetime values including executable startup and dependency
loading, not allocations attributed to a single stage. Reports include compiler,
build configuration, pinned core dependency versions, CPU model, logical processor
count, physical memory, input geometry dimensions/counts, configuration, work,
outcomes, and successful initialization placements. Orchestration remains deterministic
and serial. Packing now requests one scoped OpenMP thread by default; standalone
local requests retain inherited settings. Reports distinguish requested OpenMP
settings from ambient MKL overrides, which may take precedence.

The original initialization API does not return partial state when throwing. Dense
failure reports therefore retain the outcome, diagnostic, limits, and timing and
explicitly mark unavailable work as null; they do not fabricate rejected/accepted
counts. An exhausted greedy search is not evidence that the geometry cannot fit.
A completed benchmark invocation exits zero when its report was written, including
an algorithm resource limit or infeasible outcome. Inspect the JSON `status`;
these are measurements, not pass/fail CTest cases. A harness argument or report-I/O
error exits 2. Exceptions during a case retain completed stage data, total timing,
memory, status, and diagnostic in the report; an interrupted stage is not presented
as a completed timing or as having zero work.
Run the checked-in small matrix sequentially with three separate-process repeats:

```powershell
./benchmarks/run-windows.ps1 -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe -OutputDirectory build/benchmarks/before -Label before
```

The runner retains each JSON and console log, then writes `summary.json` with
status counts and median wall/CPU/peak-memory measurements. Stage summaries count
only completed stage observations. Medians include unsuccessful outcomes, so compare
status and work as well as time; a faster early failure is not an improvement.
The current harness accepts `--no-initialization-fallback` to measure the preserved
random-rejection path and `--max-structured-candidates N` to bound fallback work
(default 100,000, harness maximum 1,000,000). Successful initialization reports its
method, sampling attempts, structured candidates, orientations examined, reference
accepted prefix count, exact placements, and geometry work. The runner accepts
`-DisableInitializationFallback` for a reference-path matrix from the new binary.
Its default invocation uses only flags supported by the saved Milestone 6 baseline
executable, enabling the same case matrix with either executable.
The [2026-09-05 result analysis](../docs/MILESTONE_7_RESULTS.md) and its
[tracked aggregate](results/windows-20260905.json) preserve the first before/after
matrix, including successful-seed equivalence and limits on the measured claims.

## Real STL baseline and count studies

Use the new `pack` case for end-to-end measurements. It calls the production
`pack_scene` service and then `load_run_scene`; it does not substitute a
single-object solve or skip physical and serialized-geometry validation.

```powershell
build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe pack `
  --object tests/fixtures/tetra_ascii.stl --container tests/fixtures/cube_ascii.stl `
  --count 2 --initial-scale 0.999 --final-scale 1 --scale-steps 1 `
  --rotation-delta 0 --no-adaptive-sampling --timeout-ms 10000 `
  --output build/benchmarks/two-growth.json --run-output build/benchmarks/two-growth-run
```

`--output` is a new measurement report file; `--run-output` is a separate new
packing artifact directory. Existing artifacts are preserved. Unicode input,
report and artifact paths are supported. Expected engine failures still publish
the canonical summary-only artifact directory. Pre-engine errors retain a
measurement report with the failure and completed measurement data, while the
packing service preserves its no-run-directory contract.

The `pack` defaults match normal engine settings: count 10, seed 1918, initial
volume scale 0.1, target 1.0, nine scale steps, adaptive sampling and structured
fallback enabled, one million random attempts, 300,000 ms engine time, and
1,000 iterations / 30,000 ms per local solve. Flags are:

| Option | Purpose / bound |
| --- | --- |
| `--object`, `--container` | Required STL paths, subject to normal mesh byte/count/topology limits. |
| `--run-output` | Required new canonical artifact directory. |
| `--count` | 1 through 1,000. Combined mesh arithmetic and engine work remain checked by the production service. This harness range is not evidence of practical 1,000-object performance. |
| `--initial-scale`, `--final-scale` | Finite volume scales in `(0, 1]`, initial no larger than target. Equal values explicitly label a direct-placement workload. |
| `--scale-steps` | 1 through 200. |
| `--rotation-delta` | Maximum rotation delta in radians, 0 through pi. Default is the production pi/12 value. |
| `--timeout-ms`, `--local-timeout-ms` | Separate engine and per-solve cooperative time bounds, each at most 300,000 ms. Preparation and initialization retain their independent resource bounds. |
| `--local-iterations` | Per-solve iteration limit, at most 10,000; default 1,000. |
| `--no-adaptive-sampling`, `--no-initialization-fallback` | Explicitly disable those policies; absence uses production defaults. |
| `--solver-openmp-threads` | Request 1 through 256 OpenMP threads around numerical dependency work; zero inherits the caller setting. Packing defaults to one. Caller state is restored; actual MKL workers may differ when MKL environment overrides are present. |
| `--no-physical-retry-reuse` | Recompute unchanged TetGen/CAT context and local results after physical rejection. Default growth reuses only immediate unchanged trial work; reference growth disables reuse. |
| `--detailed-diagnostics` | Opt into 128 local records with up to 64 trace samples each, and one bounded failed-problem snapshot for replay. Normal measurements retain up to 1,000 lightweight local records with no numerical trace. Dropped counts remain visible. |

Each report contains SHA256 hashes and sizes of the two input files, compiled
source provenance, compiler/dependencies/machine, the requested/resolved settings,
placements, physical validity, exact target completion, aggregate work, and
bounded object/barrier/iteration/limit/work records. The sibling canonical run
summary remains the authoritative record of all resolved defaults, limits,
numerical traces and original TetGen recovery reasons; keep it with the report.
The optional failed-local-problem artifact uses the same replay format as the CLI.
Input files must remain unchanged during a measurement: hashes are streamed before
packing, not a filesystem snapshot. The 256 MiB per-input hash bound matches normal
mesh loading and prevents hashing an unbounded input.

Stages separate input preparation, placement initialization, the whole engine,
resampling, mesh transforms, TetGen, CAT, local solves, correction, final physical
validation, output validation, export, and saved-run loading. These production
stage timers record wall time, with `cpu_ms: null`; process CPU is measured for
the whole service/load/measurement. Engine substages overlap the reported engine
total and must not be added to it. Export includes artifact preparation and writes
but excludes output validation and final summary writing/commit; the service total
includes publication. A failed run can contain partial-stage elapsed work. A stage
that was never reached has zero elapsed work, not evidence that its workload is
free. Physical validation is null when it did not run.

Local timing also separates preparation, dependency setup/destruction, inclusive
solve and independent postcheck in nanoseconds. Constraint, Jacobian and Hessian
callback times are nested within solve; do not add them to its total. Retry reuse
counts avoided local calls and prepared batches separately. Actual solver/TetGen/CAT
work and local records exclude reused requests, while logical retries, physical
checks and deadline/iteration accounting remain active.

SHA256 measurement is timed separately and warms filesystem caches. The report's
total and peak memory include hashing, packing, record formatting and saved-run
loading, but exclude final measurement JSON formatting/writing. Runs are separate
processes, not guaranteed cold filesystem caches. Loading is the Studio model
loader, not viewport construction or GPU/render interaction measurement.

Run repetitions and retain unsuccessful outcomes:

```powershell
./benchmarks/run-stl-windows.ps1 `
  -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe `
  -Object path/to/object.stl -Container path/to/container.stl `
  -Counts 10 -OutputDirectory build/benchmarks/real10 -Repeats 3 -Label baseline
```

The runner preserves each console log, report and artifact directory, records the
executable SHA256, and reports success fractions plus min/median/max wall, CPU and
peak memory. It stops increasing counts after a case fails to pack, validate,
export and load; `-ContinueAfterFailure` deliberately overrides that choice.
Use `-SolverOpenmpThreads 0` for inherited dependency threading or
`-DisablePhysicalRetryReuse` for the uncached comparison. The runner forwards
the thread option only when explicitly specified, retaining compatibility with
saved older benchmark executables.
Repeated counts in one runner invocation use the **same container**, so they test
increasing density. For fixed-density 100/300/1,000 studies, use separately scaled
container inputs and a new invocation/output directory per size. Keep direct
placement and genuine-growth results separate; a fast failure is not throughput.

A short `irop_benchmark_pack_smoke` CTest exercises two-object direct placement,
actual two-object growth, Unicode paths, source/input provenance, export/loading
and argument bounds. Deliberate real-input and large-count measurements remain
outside ordinary CTest; the smoke has no performance threshold.

## Tranche 2 genuine-growth results (2026-09-06)

The [tracked ledger](results/windows-20260906-tranche2.json) records the supplied
`ulamok_2kg_simplified.stl` / `10_kg_np.stl` pair, ten copies, seed 1918,
volume scale `.1 -> 1.0`, nine barriers, adaptive sampling on and structured
initialization fallback off. Three serial Release processes on the recorded
Ryzen 9 9950X machine all reach exact scale 1.0, pass full-resolution and
serialized-float32 physical validation, export and load. Placements, geometry
bytes and work are identical across the three repetitions.

| Measurement | Result |
| --- | --- |
| Complete-run successes | 3/3; original tranche-1 growth baseline 0/3 |
| Total wall time, each process | 216.343 / 221.492 / 227.274 seconds |
| Engine wall time, median | 221.385 seconds |
| Local solves, median | 207.973 seconds, about 94% of engine time |
| Physical correction, median | 8.643 seconds |
| TetGen / CAT / resampling, medians | 3.303 / 1.225 / 0.031 seconds |
| Output validation / export / saved loading, medians | 77.19 / 13.00 / 8.83 milliseconds |
| Lifetime peak working set / commit, medians | 69.75 / 152.33 MiB |
| Growth work | 105 iterations, 328 local solves, 5 sampling refinements, 17 physical step retries |

Engine and local time limits remain 300,000 and 30,000 ms; local iteration limit
remains 1,000. The measured physical triangle allowance is one billion per engine
run (762,607,476 used); standalone scene queries retain 100 million. CAT contact
diagnostics have a separate ten-million allowance (9,738,240 used), and the run
explicitly records incomplete CAT diagnostics. Physical validation is complete.
No dependency threading override was applied: process CPU is roughly 16 times
wall time. Profile local preparation, callbacks, linear algebra and dependency
thread settings before choosing the next optimization.

Reproduce on this checkout with the locally preserved tranche-2 executable and a
fresh output parent. The current executable uses tranche-3 defaults; it is not the
recorded baseline binary:

```powershell
./benchmarks/run-stl-windows.ps1 `
  -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks-tranche2.exe `
  -Object rc/input_models/ulamok_2kg_simplified.stl `
  -Container rc/containers/10_kg_np.stl -Counts 10 -Repeats 3 -Seed 1918 `
  -InitialScale 0.1 -FinalScale 1 -ScaleSteps 9 `
  -DisableInitializationFallback -ContinueAfterFailure `
  -OutputDirectory build/tranche2-benchmark-reproduction -Label tranche2
```

The ledger includes the failed baseline, all timing samples, source/input and
executable hashes, machine/dependencies, work and resolved limits. Its source
hashes describe the measured working tree over the recorded base revision. A
subsequent exception-reporting correction only makes CAT completeness conservative
on aborted physical queries; its source difference is recorded separately and
all supported build/test matrices were rerun. These timings belong to the
explicitly identified measured binary, with unchanged successful-growth policy.
There is no speedup ratio between an early failure and a complete success.
The user meshes live under ignored `rc/`; tracked generated tests cover the
portable regression corpus. This measurement does not establish practical
100/300/1,000-object throughput or convergence for other shapes/seeds.

## Tranche 3 scaling study (2026-09-07)

The [registered plan](tranche3-plan.json) fixes genuine-growth 100/300-object
acceptance before further optimization. [Fixture instructions](fixtures/README.md)
cover generated irregular, slender and concave meshes, surface-preserving detail
variants, constant-density container scaling and independent float32 fit witnesses.
The nine standard-library checks run in CI; generating large timing cases remains
an explicit local action. Supplied-STL scaled containers and fixed-container
non-fit controls are separate workloads.

[ADR-0017](../docs/adr/0017-measured-solver-thread-control-and-scaling.md) records
scoped numerical thread control, nested local timings and bounded physical retry
reuse. The primary exact-target, physical/output, memory and Studio gates remain
unchanged. The [final ledger](results/windows-20260907-tranche3.json) records three
validated/exported/loaded full-growth successes each at 100 and 300, with medians
33.484 and 125.446 seconds. The controlled 100-object baseline is 155.805 seconds
(4.65x improvement); both final counts meet registered memory limits and Studio
acceptance. See [STATUS](../docs/STATUS.md) for settings and verification. The
supplied768-face 100-object scaled-container growth failure and the rejected slower
collision-index prototype remain visible; these results do not certify arbitrary
meshes or establish 1,000-object throughput.
