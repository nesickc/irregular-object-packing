# Windows C++ Quick Start

## Support status

Milestones 1 through 5 are implemented and verified on Windows 11 x64 with
Visual Studio 2026, CMake, MSVC, and the pinned vcpkg manifest. Milestone 6 is
implemented and its local Python/C++ parity, robustness, static-analysis,
source-release, and notice gates pass; the first hosted workflow run remains
pending, so the milestone is not yet marked Verified. The C++ command line
remains experimental. Dense-initializer search-quality and measured scalability
work begin in Milestone 7, while a bounded initializer search ending in resource
exhaustion is not proof that a packing is impossible. A visualization UI remains
deferred.

Public source distribution is supported under the release checklist. Public
binary and hosted-service distribution remain gated by the TetGen and Ipopt
obligations described in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Prerequisites

Install:

- Windows 11 x64;
- Visual Studio 2026 with the Desktop development with C++ workload and x64
  MSVC tools;
- CMake 4.2 or newer;
- Python 3.9 or newer (used by the Windows process-level CTest harnesses);
- Git; and
- a recent vcpkg checkout that supports the installed Visual Studio 2026
  toolchain.

Set VCPKG_ROOT to the vcpkg checkout before configuring:

~~~powershell
$env:VCPKG_ROOT = "C:/src/vcpkg"
& "$env:VCPKG_ROOT/vcpkg.exe" version
cmake --version
~~~

The manifest pins the dependency registry revisions and overlay sources. The
initial configure requires network access and can take substantial time because
VTK is normally built from source. Later restores can use vcpkg's binary cache.

## Clean checkout, build, and test

From a Visual Studio 2026 developer PowerShell:

~~~powershell
git clone https://github.com/MbBrainz/irregular-object-packing.git
Set-Location irregular-object-packing
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release --parallel 2
ctest --preset windows-vs2026-release
cmake --build --preset windows-vs2026-release --target irop-format-check
~~~

The supported preset writes only under build/windows-vs2026. It uses
x64-windows, warnings as errors, the root vcpkg manifest, and the checked-in
overlays. A machine-specific CMakeUserPresets.json is optional and is not part
of the supported path.

For the focused static-analysis build, remain in a Visual Studio developer
shell and run:

~~~powershell
cmake --preset windows-ninja-analysis
cmake --build --preset windows-ninja-analysis --parallel 2
ctest --preset windows-ninja-analysis
~~~

## Reproducible smoke run

The tracked tetrahedron and cube fixtures provide a clean-checkout smoke case.
Choose an output directory that does not already exist:

~~~powershell
build/windows-vs2026/Release/irop.exe pack --object tests/fixtures/tetra_ascii.stl --container tests/fixtures/cube_ascii.stl --count 1 --initial-volume-scale 0.1 --final-volume-scale 0.1001 --scale-steps 1 --max-iterations-per-scale-step 3 --no-adaptive-sampling --output-dir build/smoke-pack
Get-Content build/smoke-pack/run-summary.json
~~~

Success produces packed-objects.stl, container.stl, placements.json, and
run-summary.json. Treat the result as usable only when the summary reports a
success outcome and physical_scene_valid is true. The complete CTest suite runs
the same CLI contract plus bounded failure cases and Unicode paths.

Inspect and initialization can be checked independently:

~~~powershell
build/windows-vs2026/Release/irop.exe inspect tests/fixtures/tetra_ascii.stl --output-dir build/smoke-inspect
build/windows-vs2026/Release/irop.exe initialize --object tests/fixtures/tetra_ascii.stl --container tests/fixtures/cube_ascii.stl --count 3 --seed 123 --output-dir build/smoke-initialize
~~~

## Configuration guide

Run pack --help for every current flag. The main groups are:

- Geometry and output: object, container, count, output-dir, and optional
  individual STLs. Both inputs must be connected, orientable, closed triangular
  surfaces. The output directory must not already exist.
- Scale schedule: initial-volume-scale, final-volume-scale, and scale-steps.
  Scale is a volume ratio; the linear mesh multiplier is its cube root.
- Local movement: maximum-rotation-delta-radians,
  maximum-translation-per-unit-scale, padding, local-solve-tolerance, and the
  collision/TetGen recovery volume factors. Coordinate-valued settings use the
  same arbitrary unit as both input meshes.
- Sampling: adaptive sampling is enabled by default. Use
  no-adaptive-sampling for a full-resolution diagnostic run or a small coarse
  primitive; alpha, beta, and minimum triangle flags tune the adaptive path.
- Determinism: seed selects the run-owned NumPy-compatible MT19937 stream.
  Reproducibility is expected only for the same build, dependencies, inputs,
  configuration, seed, and thread policy, within numerical tolerances.
- Safety limits: input/output mesh counts, initializer attempts and geometry
  work, correction passes, local solves/iterations/time, history, and total
  elapsed time are explicit bounds. Reaching one reports a structured
  unsuccessful limit outcome; it does not prove geometric infeasibility.

Press Ctrl+C for cooperative cancellation. The command exits 130. Cancellation
before an engine result publishes no output directory; later cancellation
publishes only a cancelled run-summary.json. Exit categories and the complete
artifact contract are summarized in the root README.

## Troubleshooting

### VCPKG_ROOT or the toolchain file is missing

Set VCPKG_ROOT to a vcpkg checkout containing
scripts/buildsystems/vcpkg.cmake, then remove only the failed build preset
directory or start from a clean checkout and configure again.

### Visual Studio 18 2026 generator is unavailable

Verify that CMake is at least 4.2 and Visual Studio 2026 has the x64 C++ workload.
Run the commands from its developer PowerShell. Visual Studio 2022 cannot
generate the supported windows-vs2026 preset.

### GL2PS download times out

The official scoped registry must still resolve GL2PS 1.4.2#5. If its declared
upstream endpoint is temporarily unavailable, seed only the byte-identical
SHA-512-verified archive into vcpkg's normal download cache:

~~~powershell
& "$env:VCPKG_ROOT/vcpkg.exe" x-download "$env:VCPKG_ROOT/downloads/gl2ps-1.4.2.tgz" --sha512=46652e1b3825ace61dbd77c4b0bf451e7671c248eb18bbd3369e2fac00056ea4cd5d2578561984313c239e3b02f78b9d9a76d963c935af65a13bc2abfc538620 --url=https://distfiles.gentoo.org/distfiles/04/gl2ps-1.4.2.tgz
~~~

Then rerun configure. Do not add a GL2PS overlay or change the expected hash.

### Ipopt or Intel DLLs are missing at runtime

Run irop.exe from its CMake build output. CMake deploys the complete
configuration-specific recursive DLL closure there; copying only irop.exe or
ipopt-3.dll is insufficient. Debug and Release have different runtime closures.

### invalid_mesh says the mesh must be a closed two-manifold

Every triangle edge must belong to exactly two triangles, with one connected
surface and consistent orientability. Holes, duplicate/nonmanifold faces,
self-joins, loose internal sheets, or separate components are rejected. Repair
and re-export the mesh as a watertight triangular STL before packing.

### Initial placement exhausts attempts

This is a bounded greedy bounding-sphere search, not a packing proof. Record the
seed and work counters from the summary. Raising max-sampling-attempts can be a
diagnostic, but it may repeat heuristic jamming; reducing the initial volume
scale or trying another seed is usually more informative until the post-parity
initializer redesign.

### TetGen recovery or an Ipopt iteration limit occurs

First rerun with no-adaptive-sampling and a small scale increase to separate
input/build problems from aggressive packing. Inspect run-summary.json for the
terminal category, dependency work counters, and limit reached. Do not treat an
unsuccessful summary as a usable placement.

### The output path already exists

Artifact publication is intentionally no-overwrite and atomic. Choose a new
output directory. Remove or archive an earlier output yourself only after
confirming it is no longer needed.
