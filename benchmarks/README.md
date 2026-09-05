# Milestone 7 measurements

The optional `irop_benchmarks` executable runs one bounded case per process and
writes a new JSON report. Enable it through the supported Windows preset:

```powershell
cmake --preset windows-vs2026 -DIROP_BUILD_BENCHMARKS=ON
cmake --build --preset windows-vs2026-release --target irop_benchmarks
build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe init-dense --count 10 --segments 12 --attempts 100000 --seed 1918 --label baseline --output build/benchmarks/baseline-dense10.json
```

Reconfigure before measuring changed source: revision and initialization/collision
source hashes are captured at configure time. Give baseline and changed runs
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

All cases accept `--output`, `--label`, `--count`, `--segments`, `--attempts`,
`--seed`, and `--timeout-ms`. Initialization also accepts `--initial-scale`.
Stages and growth fix count and geometry in the report. Defaults are count 10,
12 cylinder segments, 100,000 sampling attempts, seed 1918, and 10,000 ms.
Harness bounds are count/segments 512, attempts 10,000,000, and timeout 60,000 ms;
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
outcomes, and successful initialization placements. Dependency thread defaults are
retained and labeled; orchestration is deterministic and single-threaded.

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