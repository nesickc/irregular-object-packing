# Reproducible scaling fixtures

`generate_scaling.py` uses only Python's standard library. It writes a **new**
directory below the repository's `build/` directory and never runs the packer.
Generator version two uses the version-one manifest schema and records generator/input hashes, settings, seeds, volume
fractions and fit witnesses. Generated artifacts are intentionally kept out of Git;
the checked-in generator reconstructs their float32 STL bytes.

```powershell
python benchmarks/fixtures/generate_scaling.py --output build/scaling-fixtures
python benchmarks/fixtures/test_generate_scaling.py
```

The test command runs nine standard-library checks independently of generation or
packing. It creates `build/` when needed, so it also works on a clean checkout.
The Windows CI job runs the same command with Python 3.10; no Python packages are
needed for these checks, and ordinary C++ builds retain their existing dependencies.
The Python lint job checks this tooling with the repository's Ruff configuration;
format these two Python files with the existing Black settings.

The default matrix has counts **10, 100, 300**, seeds **1918, 0, 12345**, a **5%**
full-size volume fraction and three source families. Primary cases grow from
volume scale **0.1 to 1.0 over nine barriers**. Near-target `0.9 -> 1.0` and direct
`1.0 -> 1.0` modes are separately labeled diagnostics. These settings never replace
one another implicitly. No generated case is labeled as a successful packing run.
Time and memory targets remain unset until the baseline is measured; register them
in the measurement ledger before selecting an optimization.

| Family | Source geometry | Detail 0 / 1 triangles |
| --- | --- | --- |
| `irregular` | Asymmetric twelve-sided cylinder from the existing practical packing tests | 44 / 176 |
| `slender` | The same test cylinder with x/y scaled by 0.3 and z by 1.8 | 44 / 176 |
| `concave` | Alternating-radius star prism with center-fan caps from the physical recovery tests | 48 / 192 |

Detail levels split existing triangles at shared edge midpoints. They preserve the
piecewise linear surface up to float32 serialization, increasing mesh work without
silently changing the intended shape. `--details 0,1,2` additionally includes the
704/768-triangle level. Generated container meshes have twelve triangles and a
small determinant-one shear following the existing practical fixture. Within each
source/detail series, container aspect and shear stay fixed: dimensions scale
uniformly with `count^(1/3)`. A cubic witness grid with
`ceil(cuberoot(count))` cells per axis leaves spare cells when necessary. Density
remains exactly the requested mathematical value before float32 rounding; an
insufficiently spacious grid is rejected rather than changing density. Version one
used count-dependent aspect ratios and its measurements are preliminary only.
`--density`, `--counts`, `--details`, `--shapes` and `--seeds` support small explicit
subsets. Counts are bounded at 300 for this tranche; this tool makes no 1,000-object
performance claim.

Each generated case has a separate full-size fit witness. The generator checks
**serialized float32 vertices** against the actual container triangle halfspaces
and strictly disjoint grid cells in the inverse-shear frame. Each triangle lies in
the convex hull of its vertices, so strict cell separation and containment extend
to the complete mesh. Witness JSON records all translations, margins and the
explicit mathematical source vertex-centroid pivot. Its arithmetic can differ
slightly from C++ accumulation and STL point order; witnesses are geometric fit
certificates, not bitwise runtime transform replays. Witnesses establish that
a layout fits; they are **not passed to initialization** and do not prove that the
packing algorithm will find a layout. Final packing and output validation remain
required. The generated manifold topology follows already-tested mesh constructors;
structural checks are not a general self-intersection validator for arbitrary STL.

Run one primary case through the existing benchmark executable:

```powershell
build/windows-vs2026/benchmarks/Release/irop_benchmarks.exe pack `
  --object build/scaling-fixtures/irregular-detail0.stl `
  --container build/scaling-fixtures/irregular-detail0-100-container.stl `
  --count 100 --seed 1918 --initial-scale 0.1 --final-scale 1 --scale-steps 9 `
  --no-adaptive-sampling --no-initialization-fallback --timeout-ms 300000 `
  --label generated-growth-baseline `
  --output build/scaling-growth100.json --run-output build/scaling-growth100-run
```

Use separate processes for three timing samples. Record dependency thread settings,
resolved limits, binary/source hashes, CPU/wall time, peak memory and outcomes in
the normal benchmark ledger. Preserve failed samples. Generated defaults disable
adaptive sampling to retain the selected low-detail surface and disable structured
fallback to keep initialization policy explicit.

## Supplied object in proportionally larger containers

This separate mode accepts bounded **binary** STL inputs (64 MiB and 200,000
triangles per input, consistently oriented connected genus-zero closed surfaces):

```powershell
python benchmarks/fixtures/generate_scaling.py `
  --object rc/input_models/ulamok_2kg_simplified.stl `
  --container rc/containers/10_kg_np.stl --reference-count 10 `
  --output build/scaling-supplied
```

For count N, container coordinates are multiplied about the origin by
`(N / reference_count)^(1/3)`. Full-size volume fraction stays approximately equal
to the reference count after float32 rounding. Input hashes and unmodified source
STL bytes are retained. The manifest reports **feasibility unproven** for this
family: an unchanged volume fraction does not establish a fit witness.

The original fixed container is a separate control, excluded from the fixed-density
series. If total signed object volume exceeds container volume, the control is
labeled `volume-non-fit-if-valid-solids`; production input validation must establish
that the supplied meshes represent valid solids before interpreting this as a
non-fit certificate. No volume-only check claims that a case below 100% fits.
