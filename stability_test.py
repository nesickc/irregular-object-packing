#!/usr/bin/env python3
"""
Stability test for the irregular object packing algorithm.

Runs a configurable set of packing problems multiple times with different seeds,
collecting timing, success/failure, and packing quality metrics.

Designed for parallelism on Apple Silicon (M4 Max) using multiprocessing.

Usage:
    python stability_test.py                    # run with defaults
    python stability_test.py --repeats 5        # 5 repeats per problem
    python stability_test.py --workers 8        # 8 parallel workers
    python stability_test.py --quick            # fast smoke test
"""

import argparse
import json
import logging
import multiprocessing as mp
import os
import sys
import traceback
from dataclasses import asdict, dataclass, field
from pathlib import Path
from time import perf_counter

import numpy as np
import pyvista as pv
from tabulate import tabulate

from irregular_object_packing.mesh.transform import scale_and_center_mesh, scale_to_volume
from irregular_object_packing.packing.optimizer import Optimizer
from irregular_object_packing.packing.optimizer_data import SimConfig


# ---------------------------------------------------------------------------
# Problem definitions
# ---------------------------------------------------------------------------

@dataclass
class ProblemDef:
    """Defines a single packing problem configuration."""
    name: str
    n_objects: int
    coverage_rate: float
    container_volume: float
    n_scale_steps: int
    itn_max: int
    mesh_source: str  # "sphere", "cube", or a file path
    container_source: str  # "sphere" or "cube"
    final_scale: float = 1.0
    alpha: float = 0.1
    beta: float = 0.5


# A diverse set of problems with increasing difficulty
PROBLEMS = [
    ProblemDef(
        name="sphere_in_sphere_3",
        n_objects=3,
        coverage_rate=0.25,
        container_volume=10.0,
        n_scale_steps=5,
        itn_max=50,
        mesh_source="sphere",
        container_source="sphere",
    ),
    ProblemDef(
        name="sphere_in_sphere_5",
        n_objects=5,
        coverage_rate=0.3,
        container_volume=10.0,
        n_scale_steps=7,
        itn_max=80,
        mesh_source="sphere",
        container_source="sphere",
    ),
    ProblemDef(
        name="cube_in_cube_3",
        n_objects=3,
        coverage_rate=0.25,
        container_volume=10.0,
        n_scale_steps=5,
        itn_max=50,
        mesh_source="cube",
        container_source="cube",
    ),
    ProblemDef(
        name="rbc_in_sphere_3",
        n_objects=3,
        coverage_rate=0.25,
        container_volume=10.0,
        n_scale_steps=5,
        itn_max=60,
        mesh_source="data/mesh/RBC_normal.stl",
        container_source="sphere",
    ),
    ProblemDef(
        name="rbc_in_sphere_5",
        n_objects=5,
        coverage_rate=0.3,
        container_volume=10.0,
        n_scale_steps=7,
        itn_max=80,
        mesh_source="data/mesh/RBC_normal.stl",
        container_source="sphere",
    ),
    ProblemDef(
        name="sphere_in_sphere_10",
        n_objects=10,
        coverage_rate=0.3,
        container_volume=10.0,
        n_scale_steps=9,
        itn_max=100,
        mesh_source="sphere",
        container_source="sphere",
    ),
]

QUICK_PROBLEMS = [
    ProblemDef(
        name="quick_sphere_3",
        n_objects=3,
        coverage_rate=0.2,
        container_volume=10.0,
        n_scale_steps=3,
        itn_max=20,
        mesh_source="sphere",
        container_source="sphere",
    ),
    ProblemDef(
        name="quick_cube_3",
        n_objects=3,
        coverage_rate=0.2,
        container_volume=10.0,
        n_scale_steps=3,
        itn_max=20,
        mesh_source="cube",
        container_source="cube",
    ),
]


# ---------------------------------------------------------------------------
# Result data
# ---------------------------------------------------------------------------

@dataclass
class RunResult:
    problem_name: str
    seed: int
    success: bool
    elapsed_sec: float
    n_objects_placed: int
    final_scales: list = field(default_factory=list)
    mean_scale: float = 0.0
    min_scale: float = 0.0
    error_msg: str = ""
    errors_per_step: list = field(default_factory=list)
    fails_per_step: list = field(default_factory=list)


# ---------------------------------------------------------------------------
# Core runner
# ---------------------------------------------------------------------------

def load_mesh(source: str) -> pv.PolyData:
    if source == "sphere":
        return pv.Sphere(theta_resolution=20, phi_resolution=20)
    elif source == "cube":
        return pv.Cube().triangulate().extract_surface(algorithm=None)
    else:
        return pv.read(source)


def build_optimizer(problem: ProblemDef, seed: int) -> Optimizer:
    """Build an Optimizer instance from a problem definition."""
    mesh_volume = problem.container_volume * problem.coverage_rate / problem.n_objects

    shape = load_mesh(problem.mesh_source)
    container = load_mesh(problem.container_source)

    container = scale_to_volume(container, problem.container_volume)
    shape = scale_and_center_mesh(shape, mesh_volume)

    config = SimConfig(
        itn_max=problem.itn_max,
        n_scale_steps=problem.n_scale_steps,
        r=problem.coverage_rate,
        final_scale=problem.final_scale,
        log_lvl=logging.CRITICAL,  # suppress logs in parallel runs
        init_f=0.1,
        max_t=mesh_volume ** (1 / 3) * 2,
        padding=1e-4 * mesh_volume ** (1 / 3),
        alpha=problem.alpha,
        beta=problem.beta,
    )

    return Optimizer(shape, container, config, description=problem.name, seed=seed)


def run_single(problem: ProblemDef, seed: int) -> RunResult:
    """Run a single packing problem with a given seed. Returns a RunResult."""
    t0 = perf_counter()
    try:
        opt = build_optimizer(problem, seed)
        opt.setup()
        opt.run()

        elapsed = perf_counter() - t0
        scales = opt.tf_arrays[:, 0].tolist()
        return RunResult(
            problem_name=problem.name,
            seed=seed,
            success=True,
            elapsed_sec=round(elapsed, 2),
            n_objects_placed=opt.n_objs,
            final_scales=[round(s, 4) for s in scales],
            mean_scale=round(float(np.mean(scales)), 4),
            min_scale=round(float(np.min(scales)), 4),
            errors_per_step=opt.errors_per_step.tolist(),
            fails_per_step=opt.fails_per_step.tolist(),
        )
    except Exception as e:
        elapsed = perf_counter() - t0
        return RunResult(
            problem_name=problem.name,
            seed=seed,
            success=False,
            elapsed_sec=round(elapsed, 2),
            n_objects_placed=0,
            error_msg=f"{type(e).__name__}: {e}\n{traceback.format_exc()[-500:]}",
        )


def _run_single_wrapper(args):
    """Wrapper for multiprocessing (must be top-level picklable)."""
    problem, seed = args
    return run_single(problem, seed)


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------

def run_stability_test(
    problems: list[ProblemDef],
    repeats: int = 3,
    workers: int = None,
    base_seed: int = 42,
) -> list[RunResult]:
    """Run all problems × repeats, optionally in parallel."""
    if workers is None:
        # M4 Max has up to 16 cores; default to performance cores
        workers = min(mp.cpu_count(), 8)

    # Build task list: (problem, seed) pairs
    tasks = []
    for problem in problems:
        for r in range(repeats):
            seed = base_seed + r * 1000 + hash(problem.name) % 10000
            tasks.append((problem, seed))

    total = len(tasks)
    print(f"\nRunning {total} tasks ({len(problems)} problems × {repeats} repeats)")
    print(f"Workers: {workers}")
    print("-" * 70)

    results = []
    if workers == 1:
        for i, (problem, seed) in enumerate(tasks):
            print(f"  [{i+1}/{total}] {problem.name} (seed={seed})...", end=" ", flush=True)
            result = run_single(problem, seed)
            status = "OK" if result.success else "FAIL"
            print(f"{status} ({result.elapsed_sec}s)")
            results.append(result)
    else:
        # Use spawn context for macOS compatibility (fork is unsafe with pyvista/VTK)
        ctx = mp.get_context("spawn")
        with ctx.Pool(processes=workers) as pool:
            for i, result in enumerate(pool.imap_unordered(_run_single_wrapper, tasks)):
                status = "OK" if result.success else "FAIL"
                print(f"  [{i+1}/{total}] {result.problem_name} (seed={result.seed}) "
                      f"-> {status} ({result.elapsed_sec}s)")
                results.append(result)

    return results


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def print_summary(results: list[RunResult]):
    """Print a summary table of results grouped by problem."""
    print("\n" + "=" * 70)
    print("STABILITY TEST SUMMARY")
    print("=" * 70)

    # Group by problem
    problems = {}
    for r in results:
        problems.setdefault(r.problem_name, []).append(r)

    rows = []
    for name, runs in sorted(problems.items()):
        n_runs = len(runs)
        n_ok = sum(1 for r in runs if r.success)
        times = [r.elapsed_sec for r in runs if r.success]
        scales = [r.mean_scale for r in runs if r.success]
        min_scales = [r.min_scale for r in runs if r.success]

        rows.append([
            name,
            f"{n_ok}/{n_runs}",
            f"{np.mean(times):.1f}s" if times else "N/A",
            f"{np.std(times):.1f}s" if len(times) > 1 else "N/A",
            f"{np.mean(scales):.3f}" if scales else "N/A",
            f"{np.std(scales):.4f}" if len(scales) > 1 else "N/A",
            f"{np.mean(min_scales):.3f}" if min_scales else "N/A",
        ])

    headers = ["Problem", "Pass", "Mean Time", "Std Time", "Mean Scale", "Scale Std", "Min Scale"]
    print(tabulate(rows, headers=headers, tablefmt="grid"))

    # Overall
    total = len(results)
    passed = sum(1 for r in results if r.success)
    print(f"\nOverall: {passed}/{total} passed ({100*passed/total:.0f}%)")

    # Print failures
    failures = [r for r in results if not r.success]
    if failures:
        print(f"\nFAILURES ({len(failures)}):")
        for r in failures:
            print(f"  - {r.problem_name} seed={r.seed}: {r.error_msg[:200]}")

    # Consistency check: for successful runs of the same problem with the same seed,
    # results should be identical (determinism)
    print("\nDeterminism check:")
    seed_groups = {}
    for r in results:
        if r.success:
            seed_groups.setdefault((r.problem_name, r.seed), []).append(r)

    all_deterministic = True
    for (name, seed), runs in seed_groups.items():
        if len(runs) > 1:
            scales_sets = [tuple(r.final_scales) for r in runs]
            if len(set(scales_sets)) > 1:
                print(f"  WARNING: Non-deterministic results for {name} seed={seed}")
                all_deterministic = False
    if all_deterministic:
        print("  All runs with same seed produced identical results.")


def save_results(results: list[RunResult], output_dir: str = "results/stability"):
    """Save detailed results to JSON."""
    os.makedirs(output_dir, exist_ok=True)
    out_file = Path(output_dir) / "stability_results.json"
    data = [
        {
            "problem_name": r.problem_name,
            "seed": r.seed,
            "success": r.success,
            "elapsed_sec": r.elapsed_sec,
            "n_objects_placed": r.n_objects_placed,
            "mean_scale": r.mean_scale,
            "min_scale": r.min_scale,
            "final_scales": r.final_scales,
            "errors_per_step": r.errors_per_step,
            "fails_per_step": r.fails_per_step,
            "error_msg": r.error_msg,
        }
        for r in results
    ]
    with open(out_file, "w") as f:
        json.dump(data, f, indent=2)
    print(f"\nDetailed results saved to {out_file}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Stability test for irregular object packing")
    parser.add_argument("--repeats", "-r", type=int, default=3,
                        help="Number of repeats per problem (default: 3)")
    parser.add_argument("--workers", "-w", type=int, default=None,
                        help="Number of parallel workers (default: auto, up to 8)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Base random seed (default: 42)")
    parser.add_argument("--quick", action="store_true",
                        help="Run a quick smoke test with small problems")
    parser.add_argument("--sequential", action="store_true",
                        help="Run sequentially (no parallelism)")
    parser.add_argument("--problems", nargs="+", default=None,
                        help="Run only these problem names (space-separated)")
    parser.add_argument("--output", "-o", default="results/stability",
                        help="Output directory for results (default: results/stability)")
    args = parser.parse_args()

    problems = QUICK_PROBLEMS if args.quick else PROBLEMS

    if args.problems:
        name_set = set(args.problems)
        problems = [p for p in problems if p.name in name_set]
        if not problems:
            print(f"No matching problems found. Available: {[p.name for p in PROBLEMS + QUICK_PROBLEMS]}")
            sys.exit(1)

    workers = 1 if args.sequential else args.workers

    print("Irregular Object Packing - Stability Test")
    print(f"Problems: {[p.name for p in problems]}")

    results = run_stability_test(
        problems=problems,
        repeats=args.repeats,
        workers=workers,
        base_seed=args.seed,
    )

    print_summary(results)
    save_results(results, args.output)


if __name__ == "__main__":
    main()
