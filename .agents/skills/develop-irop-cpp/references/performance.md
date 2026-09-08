# Performance and Scalability Workflow

Use this reference for profiling, benchmarks, memory work, parallelism, collision acceleration, solver optimization, aggregation, or subdivision.

## Performance Policy

- Establish correctness and a representative workload before optimizing.
- Measure wall time, CPU time, peak memory, and relevant work counts separately.
- Profile tetrahedralization, CAT construction, constraint evaluation, solving, collision detection, and serialization independently.
- Keep a simple deterministic single-thread path for diagnosis.
- Do not promise an unbounded practical workload; avoid arbitrary product caps while reporting resource limits honestly.

## Likely Important Paths

- Repeated mesh transformation and conversion.
- Pairwise collision candidate generation and narrow checks.
- Tetrahedralization size and intermediate allocations.
- CAT face/normal construction.
- Constraint evaluation and derivative calculation.
- Per-object nonlinear solves.

## Optimization Order

1. Remove redundant conversions, copies, and allocations.
2. Reuse workspaces and precompute stable data.
3. Reduce algorithmic work, such as broad-phase collision candidates.
4. Exploit sparsity or analytic derivatives when solver evidence justifies them.
5. Parallelize independent work with explicit ownership and deterministic controls.
6. Consider regional subdivision or aggregated repeated structures as separate designed features.

## Parallelism

- Prove that work is independent and dependency calls are safe before parallelizing.
- Avoid shared mutable progress/state updates from worker threads.
- Make thread count explicit and persist it in run results.
- Compare outputs with the deterministic path using tolerances and invariants.
- Add oneTBB or another concurrency dependency only after standard facilities are demonstrably insufficient.

## Benchmark Evidence

Record input meshes, object count, configuration, seed, thread count, build configuration, compiler, dependency versions, and machine characteristics. Keep representative benchmarks small enough to run deliberately. Do not put long-running performance cases in the ordinary unit-test path.

Update `docs/STATUS.md` with before/after evidence and add an ADR when adopting a new scaling architecture such as aggregation or spatial subdivision.

## Existing Windows Benchmark Workflow

Use the checked-in optional harness and runner before adding measurement tools:

```powershell
cmake --preset windows-vs2026 -DIROP_BUILD_BENCHMARKS=ON
cmake --build --preset windows-vs2026-release --target irop_benchmarks
./benchmarks/run-windows.ps1 -Executable build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe -OutputDirectory build/benchmarks/measurement -Repeats 3 -Label measurement
```

Use a new output directory per matrix. Reconfigure after changing measured source
so the recorded source hashes match the build. Read `benchmarks/README.md` for the
bounded case modes and `docs/MILESTONE_7_RESULTS.md` for the existing baseline.
Preserve baseline production source while building its measurement executable;
disabling initialization fallback in a new binary does not restore old collision
code. Compare outcomes, exact successful seeded placements/RNG, and work alongside
median timing. Retain process peak-memory and CPU-resolution caveats, and keep
performance thresholds out of ordinary correctness tests.

## Tranche 3 comparison controls

Use `benchmarks/fixtures/generate_scaling.py` and its fit-witness tests for
fixed-density 100/300 growth studies; keep supplied inputs and fixed-container
density ladders separate. Register practical targets before selecting changes,
retain failed outcomes and distinguish direct placement from genuine growth.

Packing defaults to one task-scoped OpenMP thread and bounded immediate physical
retry reuse. Compare inherited numerical threading with `--solver-openmp-threads 0`
and recomputation with `--no-physical-retry-reuse`. Reference-growth alone restores
neither the previous complete threading policy nor an old compiled implementation.
Record bounded MKL override metadata; requested OpenMP maximum is not an actual
worker count. Read ADR-0017 before changing scope/restoration or retry ownership.

Use local preparation/setup/solve/postcheck timing to select the next hot path.
Constraint/Jacobian/Hessian callback timing is nested in solve. Actual dependency
calls and avoided retry work have separate counters; physical checks and limits
still cover every candidate. Compare committed poses/history/RNG where deterministic
and always require the full-resolution and serialized-output validation gates.
