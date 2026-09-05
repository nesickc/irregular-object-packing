# irregular-object-packing
<!-- include image  -->
![irregular-object-packing](/static/tetmesh_of_11_cells0.png)

<!-- ![Documentation Status](https://readthedocs.org/projects/irregular-object-packing/badge/?style=flat)(<https://irregular-object-packing.readthedocs.io/>) -->
<!-- ![Travis CI](https://img.shields.io/travis/MbBrainz/irregular-object-packing.svg)(https://travis-ci.org/MbBrainz/irregular-object-packing) -->
<!-- ![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/MbBrainz/irregular-object-packing?branch=main&svg=true)(<https://ci.appveyor.com/project/MbBrainz/irregular-object-packing>) -->

<!-- [![PyPi version](https://img.shields.io/pypi/v/irregular-object-packing.svg)](https://pypi.python.org/pypi/irregular-object-packing) -->
[![GitHub Actions Build Status](https://github.com/MbBrainz/irregular-object-packing/actions/workflows/github-actions.yml/badge.svg)](https://github.com/MbBrainz/irregular-object-packing/actions)
[![Codacy Code Quality Status](https://app.codacy.com/project/badge/Grade/498833b3aa9447c0a6147088c5c9fabd)](https://www.codacy.com/gh/MbBrainz/irregular-object-packing/dashboard?utm_source=github.com&utm_medium=referral&utm_content=MbBrainz/irregular-object-packing&utm_campaign=Badge_Grade)
[![Commits since latest release](https://img.shields.io/github/commits-since/MbBrainz/irregular-object-packing/v0.0.0.svg)](https://github.com/MbBrainz/irregular-object-packing/compare/v0.0.0...main)
[![Ruff](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/charliermarsh/ruff/main/assets/badge/v2.json)](https://github.com/astral-sh/ruff)

Python package for packing irregularly shaped objects in an arbitrary 3D container.
The implementation is based on the paper ["Packing irregular Objects in 3D Space via Hybrid Optimization"](http://dx.doi.org/10.1111/CGF.13490) by Ma Y, Chen Z, Hu W, Wang W 2018.

* Free software: 3-clause BSD license
<!-- * Documentation: (COMING SOON!) [https://MbBrainz.github.io/irregular-object-packing](https://MbBrainz.github.io/irregular-object-packing) -->

## Features

* Single shape irregular object packing
* only optimisation phase of the algorithm (no swapping/hole filling/...)
* 3D container with arbitrary shape
* packing up to 10 items.
* currently limit is probably due to the faulty implementation of the non-linear constraint optimisation. see issues.

## Experimental Windows C++ CLI

The C++20 port can validate an STL, create a deterministic initial scene, and run
the bounded end-to-end packing loop for repeated copies of one closed object mesh
inside one closed container mesh. Build it from a Visual Studio 2026 developer
shell with a configured `VCPKG_ROOT`:

For a clean checkout, prerequisites, a tracked-fixture smoke run, configuration
semantics, and failure-specific troubleshooting, use the
[Windows C++ quick start](/docs/WINDOWS_CPP_QUICKSTART.md).

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release
```

Run a small packing like this:

```powershell
build/windows-vs2026/Release/irop.exe pack `
  --object path/to/object.stl `
  --container path/to/container.stl `
  --count 1 `
  --initial-volume-scale 0.1 `
  --final-volume-scale 0.1001 `
  --scale-steps 1 `
  --no-adaptive-sampling `
  --output-dir artifacts/my-packing
```

This conservative one-step command is a useful input/build check. For a real
packing attempt, increase `--count` and `--final-volume-scale`, use several
`--scale-steps`, and enable adaptive sampling after the full-resolution path
works for your meshes.

The output directory must not already exist. It is checked before loading inputs;
final atomic publication also rejects a destination claimed during computation.
A successful run atomically writes
`packed-objects.stl`, `container.stl`, `placements.json`, and
`run-summary.json`; add `--individual-stls` for one STL per object. Expected
algorithm failures normally write only an unsuccessful `run-summary.json`. Opt-in
failed-solve capture may also write the numeric `failed-local-solve.json` diagnostic;
it never publishes intermediate packed geometry or placements. Press Ctrl+C for a
clean cancellation. Before an engine result exists—including during input
preparation or initialization—it exits 130 without creating the output
directory. Cancellation reported by the engine or observed after engine success
publishes only a cancelled `run-summary.json`; an already-final unsuccessful
engine result remains authoritative. Adaptive sampling is enabled by default
for reference parity, but its container refinement formula can be expensive for
coarse toy meshes; use `--no-adaptive-sampling` for those inputs and for
full-resolution diagnostic runs. Run `irop.exe pack --help` for limits and
tuning flags.
Dense initialization now tries a bounded deterministic grid after the original
random search exhausts its attempt limit. Successful seeded random placements are
unchanged. Use `--no-initialization-fallback` for the historical bounded search,
`--max-structured-candidates` to limit fallback work, and
`--max-collision-object-pair-checks` to bound packing collision pair enumeration.
The summary records the selected initialization method and separate work counts.
Measured results and limitations are in [Milestone 7 results](/docs/MILESTONE_7_RESULTS.md).

Configuration is currently supplied through CLI flags; JSON files are result
formats, not yet packing configuration inputs. To investigate a local solver
failure, add `--capture-failed-local-solve --local-solve-records 64
--local-solve-trace-records 128` to a `pack` invocation, then replay its captured
problem with:

```powershell
irop.exe replay-local-solve artifacts/my-packing/failed-local-solve.json
```

Capture is optional and bounded. Failure summaries identify the object, barrier,
iteration and local limit, while retained TetGen recovery records preserve the
backend reason. Replay runs one local problem and does not certify a complete
packing. Tranche 1 is verified; build/test, desktop and real-input evidence
is recorded in [STATUS](/docs/STATUS.md). The [improvement plan](/docs/IMPROVEMENT_PLAN.md)
keeps convergence work and measured 100-300/1,000-object scaling as subsequent
tranches. The real-STL [benchmark harness](/benchmarks/README.md) now records
end-to-end stages, input/source hashes, work, outcomes and saved-run loading;
its count range through 1,000 is not a throughput guarantee.

For automation, `pack` returns 0 on success, 2 for CLI usage, 3 for invalid
input/configuration, 4 on a resource/time limit, 5 for output I/O, 6 for another
structured unsuccessful packing outcome, 70 for an internal command failure,
and 130 after cancellation. Treat a run as usable only when
`run-summary.json` reports `outcome.category: "success"` and
`validation.physical_scene_valid: true`.

The related commands are:

```powershell
irop.exe inspect input.stl --output-dir artifacts/inspection
irop.exe initialize --object object.stl --container container.stl --count 3 --output-dir artifacts/initial
```

### Practical C++ packing checks

The practical fixtures are generated directly in C++; no hand-authored STL test
files are required. After a Release build, run the complete suite or the focused
geometry and end-to-end groups:

```powershell
ctest --test-dir build/windows-vs2026 -C Release --output-on-failure
build/windows-vs2026/tests/cpp/Release/irop_tests.exe "[geometry]"
build/windows-vs2026/tests/cpp/Release/irop_tests.exe "[practical-packing]"
```

`[geometry]` checks obvious cube/cylinder fits, separated objects, an oversized
non-fit, and a rod that fits only at a known rotation. `[practical-packing]`
runs actual initialization, TetGen, CAT, Ipopt, barrier completion, and physical
validation for rotation-enabled cylinders, full-scale cylinders/tetrahedra, and
the rotation-required rod.

The transform and scale-barrier corrections are recorded in [ADR-0012](/docs/adr/0012-transform-consistent-barrier-bounded-local-solves.md).

This C++ path is still parity-oriented and experimental. The Python package and
its notes below remain the behavioral reference until the compatibility milestone
is complete. The linked C++ executable is not simply BSD-distributable: TetGen
is used under AGPL-3.0-or-later, and the currently validated Ipopt/Intel binary
bundle is approved for local builds rather than public binary redistribution.
See [ADR-0009](/docs/adr/0009-tetgen-1-6-agpl-overlay-and-adapter.md)
and [ADR-0010](/docs/adr/0010-ipopt-3-14-19-official-windows-binary-adapter.md).
The [third-party notice](/THIRD_PARTY_NOTICES.md) records the exact dependency
inventory and the [release checklist](/docs/RELEASE_CHECKLIST.md) separates the
supported source-publication path from the still-blocked public binary and
hosted-service paths.

## Installation

    pip install irregular-object-packing

You can also install the in-development version with:

    pip install https://github.com/MbBrainz/irregular-object-packing/archive/main.zip

## Documentation

The [thesis report](/static/Thesis-Maurits_Bos-MSc_Computational_Science-Final-19June2023-1-fix.pdf) explains the algorithm and the Python implementation in detail. The notebooks also demonstrate much of the current functionality.

Planning and maintenance documentation for the Windows C++ port starts with [docs/PROJECT.md](/docs/PROJECT.md). Agents and maintainers should follow the reading order in [AGENTS.md](/AGENTS.md).

## Known Issues

**Notebook disconnecting (date: 21-mar-2023)**:
If you run the Optimization with jupyter notebook, you may experience the kernel disconnecting and then trying to reconnect.
This is a known issue and the best way current workaround is to downgrade your `jupyter_client` version to `7.3.2` and `tornado` to `6.1` (like in de ./requirements-dev.txt file).
The issue is discussed here: [https://discourse.jupyter.org/t/jupyter-notebook-zmq-message-arrived-on-closed-channel-error/17869/2](https://discourse.jupyter.org/t/jupyter-notebook-zmq-message-arrived-on-closed-channel-error/17869/2)

**Module not found: irregular-object-packing (date: 29 may 2023)**
Fix by running `pip install -e .` in the root folder of the project. which will install the package in editable mode.

## Native Windows visualization

Milestone 8 adds the optional **IROP Studio** application: load and preview STL
meshes, configure and run packing, follow progress or cancel, and inspect saved
pack/initialize results in an interactive 3D viewport. Enable the existing VTK
rendering modules with `cmake --preset windows-vs2026 -DIROP_BUILD_UI=ON`, then build
`irop_studio` using the Release preset. Run
`build/windows-vs2026/app/irop_studio/Release/irop_studio.exe`.

Choose a **Runs folder** once; Studio remembers it and creates a fresh
`run-000001`, `run-000002`, etc. on every Run. Failed/cancelled attempts consume
numbers too. **Open result folder** uses the actual latest result location.
**Capture solver diagnostics** opts into bounded traces and a failing local
problem snapshot. These run-management changes preserve the core packing settings.

See the [Studio guide](docs/STUDIO_QUICKSTART.md) for controls, saved-result
semantics, display limits, and the separate Windows/OpenGL smoke procedure.
