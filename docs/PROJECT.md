# C++ Project Definition

Status: Approved baseline; Milestones 1 through 4 are verified; Milestone 5 is next.

## Purpose

The C++ project will provide a Windows-native command-line tool for packing repeated copies of one irregular triangular mesh inside an arbitrary closed triangular container. It will reproduce the successful behavior of the Python implementation while creating a maintainable base for later performance improvements, larger workloads, and a basic visualization UI.

The Python package remains the behavioral reference until the C++ parity milestone is complete.

## Initial Product Scope

The first usable release will support:

- One source object mesh repeated a requested number of times.
- One arbitrary closed container mesh.
- The existing continuous packing phase: initialization, adaptive sampling, tetrahedralization, chordal axis transform constraints, per-object nonlinear optimization, collision correction, and scale-barrier iteration.
- A local Windows command-line application.
- STL as the required input and output mesh format.
- JSON configuration and machine-readable run results.
- Tangible output meshes that can be opened in an external mesh viewer.

The first release will not include:

- Object swapping, replacement, hole filling, or a discrete search phase.
- Multiple independent source shapes in one run.
- A graphical UI or embedded interactive renderer.
- Python bindings.
- A network service.
- Aggregation or spatial subdivision for very large populations.

Those omissions are product boundaries, not permanent architectural prohibitions.

## User-Facing Result

The intended command shape is:

```powershell
irop pack `
  --container container.stl `
  --object object.stl `
  --count 10 `
  --output out
```

A successful run should produce:

- `packed-objects.stl`: all transformed object copies combined into one mesh.
- `container.stl`: the corresponding container geometry for inspection.
- `placements.json`: per-object scale, rotation, translation, and transform data.
- `run-summary.json`: resolved configuration, seed, timings, solver outcome, packing metrics, warnings, and dependency versions.
- Optional individual object STL files when explicitly requested.

STL contains geometry but not a reliable scene graph, unit declaration, or instance metadata. JSON is therefore the canonical run record, while STL is the primary interoperable visual result. Input coordinates use arbitrary but consistent units; the application performs no implicit unit conversion.

Additional formats such as OBJ, PLY, and VTK-family files may be added behind the mesh I/O boundary when useful. They are not required for the first parity release.

Milestone 3 provides optional VTU tetrahedral-grid and VTP CAT-face diagnostic
writers behind that same boundary. These are developer inspection artifacts,
not canonical packing results.

Milestone 2 also exposes the initialization phase independently:

```powershell
irop initialize `
  --container container.stl `
  --object object.stl `
  --count 10 `
  --output out-initial
```

The output path must not already exist. A successful command atomically publishes
`initialized-objects.stl`, `container.stl`, canonical `placements.json`, a successful
`run-summary.json`, and optional per-object STLs. The initialization schemas under
`docs/schemas/` define the version-one JSON contracts. Initialization currently
requires each object and container to be one connected, orientable, closed triangular
surface; multi-component cavity and disjoint-solid semantics remain outside the
accepted input domain.

## Behavioral Reference

The reference pipeline is implemented mainly in:

- `irregular_object_packing/packing/optimizer.py`
- `irregular_object_packing/packing/optimizer_data.py`
- `irregular_object_packing/packing/initialize.py`
- `irregular_object_packing/packing/nlc_optimisation.py`
- `irregular_object_packing/cat/`
- `irregular_object_packing/mesh/`

At a high level, a run performs:

1. Load and validate the object and container surface meshes.
2. Estimate or accept the number of copies and create small initial copies inside the container.
3. Increase the current scale barrier in configured steps.
4. Adapt the surface sampling resolution to the current scale.
5. Tetrahedralize object and container surface points.
6. Split relevant tetrahedra and construct chordal axis transform cells and face constraints.
7. Solve a seven-variable local nonlinear program for each object: one volume-scale factor, three rotation angles, and three translation components.
8. Apply the resulting local transforms.
9. Detect object-object, object-container, and object-CAT conflicts.
10. Reduce selected scales when correction is necessary.
11. Continue until every object reaches the barrier or configured termination limits are reached.
12. Write geometry and structured run results.

The C++ port must not silently reinterpret scale semantics. The reference stores a volume scale and applies its cube root as a linear mesh scale.

## Compatibility Policy

Preserve behavior observed during successful Python runs. Correct a clear defect only when its intended correction is evident and no meaningful downstream behavior is expected to rely on it.

Use two stable code markers:

```cpp
// COMPATIBILITY(IROP-COMPAT-0001):
// Intentionally preserves questionable Python behavior.
// See docs/COMPATIBILITY.md.
```

```cpp
// DEVIATION(IROP-DEV-0001):
// Corrects a Python defect with no expected downstream dependency.
// See docs/COMPATIBILITY.md.
```

Every marker must have an entry in `docs/COMPATIBILITY.md` with rationale and verification. Do not use an untracked `TODO` as a substitute.

## Target Platform and Toolchain

- Operating system: Windows 11 x64.
- IDE: Visual Studio 2026.
- Compiler: stable MSVC Build Tools supplied with Visual Studio 2026.
- Language baseline: C++20.
- Build system: CMake 4.2 or newer.
- Dependency manager: vcpkg manifest mode with a pinned baseline.
- Primary triplet: `x64-windows`.
- Primary CMake generator: `Visual Studio 18 2026`.
- Optional fast local generator: Ninja or Ninja Multi-Config using MSVC.

Do not enable `/std:c++latest`, preview modes, or isolated C++23 features by default. Adopt a later language baseline only after verifying every required dependency and recording the decision.

Use checked-in `CMakePresets.json` presets so build, test, and analysis commands are reproducible. Keep user-specific paths in `CMakeUserPresets.json`, which must not be required for CI or normal onboarding.

## Proposed Source Layout

```text
/
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── vcpkg-configuration.json
├── app/
│   └── irop/
├── include/
│   └── irop/
├── src/
│   ├── model/
│   ├── geometry/
│   ├── tetrahedralization/
│   ├── cat/
│   ├── optimization/
│   ├── packing/
│   └── io/
├── tests/
│   └── cpp/
├── benchmarks/
├── cmake/
├── thirdparty/
│   └── vcpkg-ports/
│       └── tetgen/
└── docs/
```

Create directories only when their first real file is introduced. The layout is a boundary guide, not a requirement to scaffold empty modules.

## CMake Targets

The initial target model should remain small:

- `irop_core`: reusable packing library.
- `irop`: command-line executable linked to `irop_core`.
- Focused test executables or one Catch2 test target, based on measured build cost.
- Benchmarks only after a benchmark milestone begins.

Use target-based CMake. Do not use directory-wide include paths, compile flags, or link settings when target-scoped equivalents exist. Third-party warnings must not be promoted to project errors.

## Architecture

```text
CLI and configuration
        |
Packing application service
        |
Packing engine and domain model
   |        |         |        |
Geometry  CAT   Optimization  Run state
   |        |         |        |
 VTK    TetGen      Ipopt   JSON / STL adapters
```

### Domain Model

Project-owned types express stable concepts:

- `TriangleMesh`: vertices and indexed triangular faces.
- `Transform`: volume scale, rotation, translation, and derived matrix.
- `PackingConfig`: validated run configuration and resource limits.
- `PackingState`: object transforms and current iteration state.
- `PackingResult`: final state, metrics, warnings, and outcome.
- CAT constraint structures expressed in project-owned numeric types.

Prefer value types, RAII, explicit ownership, and contiguous storage. Use fixed-size Eigen types for small vectors and matrices where they materially improve correctness and readability.

### Dependency Boundaries

VTK, TetGen, and Ipopt are implementation details behind adapters. Their types must not appear in the public packing API.

Expected boundaries include:

- Mesh reader and writer adapters.
- Geometry query and collision adapter.
- Tetrahedralizer interface with a TetGen backend.
- Nonlinear solver interface with an Ipopt backend.

Interfaces exist to isolate substantial dependencies and enable testing. Do not create an interface for every class or small function.

### Packing Engine

The packing engine owns orchestration and iteration policy. Geometry modules own geometry operations; solver modules own solver callbacks and result translation; the CLI owns user interaction and filesystem presentation.

Keep algorithm state explicit. Avoid hidden global state, class-level mutable histories, implicit random generators, and callbacks that mutate unrelated modules.

### Future Visualization

A later visualization application will link to `irop_core` and consume `PackingResult` or a serialized run record. The core library must not depend on UI event loops or windowing frameworks.

## Dependency Baseline

| Dependency | Initial role | Acquisition |
| --- | --- | --- |
| VTK | STL I/O, mesh processing, geometry queries, later visualization path | vcpkg |
| Eigen3 | Linear algebra and fixed-size transforms | vcpkg |
| Ipopt | Primary nonlinear optimization backend | Official 3.14.19 Windows archives through a local vcpkg overlay; private C adapter |
| TetGen | Python-compatible CAT tetrahedralization; live `O0/0Q` point-union behavior | Patchless local vcpkg overlay; static private backend |
| CLI11 | Command-line parsing and help | vcpkg |
| nlohmann-json | Configuration and run-result serialization | vcpkg |
| spdlog | Structured diagnostics | vcpkg |
| Catch2 | C++ tests | vcpkg development dependency |

Deferred dependencies:

- oneTBB: add only after profiling identifies useful parallel work and deterministic ownership is clear.
- Google Benchmark: add when performance baselines are introduced.
- NLopt: evaluate only if a fallback solver is required for compatibility.

Do not add a dependency for a small, safe utility that can be implemented and tested clearly with the standard library. Do not implement specialized file parsers, nonlinear solvers, computational geometry kernels, or JSON parsers casually.

## Licensing and Third-Party Notices

Repository-owned source, including the C++ port, remains available under the BSD 3-Clause License in the root [LICENSE](../LICENSE) file. Retain the existing Maurits Bos copyright notice, license conditions, and disclaimer. Publishing the repository on GitHub is treated as source redistribution for compliance purposes, even though no binary distribution is currently planned.

Dependencies and copied third-party material retain their own licenses; the repository BSD license does not replace those terms. When adding or updating a dependency:

- Record its exact version, source, and license in the dependency manifest or accompanying third-party documentation.
- Preserve notices and source offers required by that dependency.
- Keep copied dependency code isolated under `thirdparty/` or an overlay port instead of presenting it as project-owned BSD source.
- Review the combined-work and binary-distribution obligations before publishing an executable or hosted service.

TetGen is the most restrictive active dependency. ADR-0009 selects official
v1.6.0 commit `535f9c41f44abc832a7bbf2c9c7af003d1c18f3c` under
AGPL-3.0-or-later and integrates it through a patchless local vcpkg overlay plus
a static, private project adapter. Private linkage isolates the public API; it
is not a licensing exemption. Project-owned files remain BSD-3-Clause, while
any future conveyed combined build or hosted service requires a release-specific
AGPL source, build-material, and notice review. No binary distribution is
currently planned. See [ADR-0009](adr/0009-tetgen-1-6-agpl-overlay-and-adapter.md).

Milestone 4 packages the official Ipopt 3.14.19 Windows DLL archive through a
hash-pinned local vcpkg overlay and consumes it only through a private C adapter.
That archive also contains MUMPS, METIS, embedded oneMKL code, and Intel runtime
DLLs under their own terms. ADR-0010 permits the reproducible local-development
path but gates public distribution of this exact binary combination until a
release-specific license, notice, source-availability, and TetGen-AGPL
compatibility review. A source-built Ipopt/MUMPS/OpenBLAS configuration is the
preferred distribution fallback. See
[ADR-0010](adr/0010-ipopt-3-14-19-official-windows-binary-adapter.md).

## Error Handling

- Use exceptions for unrecoverable setup, parsing, filesystem, and dependency-adapter failures at application boundaries.
- Use explicit result or status values for expected solver failure, invalid geometry, convergence failure, and exhausted resource limits.
- Use assertions for internal invariants that indicate programmer defects.
- Translate third-party errors into project-owned errors before they cross module boundaries.
- Include actionable context without exposing enormous input-derived strings or raw buffers.

Do not add repetitive defensive checks inside proven hot loops. Validate once at the trust boundary and maintain clear invariants internally.

## Security Baseline

Mesh and JSON files are untrusted local input. The application must:

- Enforce configurable limits for input bytes, vertices, triangles, object count, iterations, and generated intermediate data.
- Check integer conversions, multiplication, addition, and allocation sizes before allocating.
- Reject non-finite coordinates and invalid face indices.
- Handle truncated, malformed, degenerate, and contradictory mesh data without memory corruption.
- Bound correction loops and solver work; a malformed input must not create an accidental infinite loop.
- Avoid following input-controlled output paths outside the selected output directory.
- Never execute input-derived commands or load code from a run file.
- Keep normal execution offline.
- Pin dependency revisions and update them deliberately.

Limits are configurable safety controls, not artificial claims about maximum supported scale.

## Performance and Scalability

The design must not contain a fixed product limit on object count. It should use suitable index types, checked conversions at dependency boundaries, and data structures whose costs are visible.

The direct parity algorithm may retain expensive pairwise collision work or per-object solves initially. Record and benchmark those costs before redesigning them.

Likely future improvements include:

- Broad-phase spatial indexing before narrow collision checks.
- Reuse of mesh and solver workspaces.
- Constraint sparsity and analytic derivatives where justified.
- Controlled parallel per-object optimization.
- Regional subdivision of large containers.
- Aggregated repeated structures, such as treating groups of bricks as larger packing units.

These are extension directions, not reasons to introduce unused abstraction today.

## Determinism

Given the same build, dependencies, input, configuration, seed, and thread count, runs should be reproducible within numerical tolerances. Persist the seed and relevant dependency versions in `run-summary.json`.

Do not promise byte-identical results across compiler versions, CPUs, solver versions, or thread counts. Compatibility tests should compare tolerances, transforms, containment, collision state, and other geometric invariants rather than serialized floating-point bytes.

## Testing Strategy

Keep tests smaller and more focused than an exhaustive reimplementation of the product.

Priorities are:

1. Small mathematical and transform invariants.
2. CAT split and constraint cases derived from existing Python tests.
3. Solver adapter and failure translation tests.
4. Mesh validation and malicious-input boundary cases.
5. Compatibility tests for cataloged behavior.
6. End-to-end smoke runs that emit inspectable STL and JSON artifacts.
7. A small number of representative performance cases.

Prefer parameterized tests, compact fixtures, and generated simple geometry. Avoid large duplicated golden files. Use golden data only where it captures behavior that cannot be expressed clearly as invariants.

Project warnings are errors in CI once the baseline is stable. Third-party warnings are not. Static analysis should use a focused repository configuration rather than enabling every available check.

## Coding Principles

- Treat the root `.clang-format` as authoritative. It starts from Google style and intentionally overrides it; format changed C++ files with that exact profile.
- Prefer C++20 standard-library facilities.
- Keep functions and modules cohesive and data flow visible.
- Apply DRY to stable knowledge, not to coincidental repetition.
- Prefer composition over inheritance.
- Reuse proven abstractions, but avoid speculative frameworks and giant templates.
- Make ownership and lifetimes explicit.
- Keep headers narrow and compilation dependencies controlled.
- Comment intent, invariants, and compatibility constraints rather than restating syntax.
- Prioritize correctness and security, then optimize measured important paths.

## Project Memory and Governance

- `AGENTS.md` provides the shortest onboarding path and mandatory update rules.
- `docs/PROJECT.md` is the authoritative project definition.
- `docs/IMPLEMENTATION_PLAN.md` defines milestone scope and gates.
- `docs/STATUS.md` records what is actually implemented and verified.
- `docs/COMPATIBILITY.md` records preserved quirks and deviations.
- `docs/adr/` records durable decisions and their rationale.
- `.agents/skills/develop-irop-cpp/` defines the preferred agent workflow for C++ work.
- `.clang-format` is the maintainer's reusable C++ formatting baseline for this and future C++ repositories.

When project-wide coding practice changes, update the skill and the relevant project documentation together. Record a significant or difficult-to-reverse policy change as an ADR.
