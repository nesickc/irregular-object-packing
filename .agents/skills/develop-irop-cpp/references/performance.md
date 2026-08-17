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
