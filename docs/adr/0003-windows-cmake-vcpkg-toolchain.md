# ADR-0003: Windows, CMake, and vcpkg Toolchain

- Status: Accepted
- Date: 2026-08-18
- Amended: 2026-08-22
- Deciders: Project maintainers

## Context

The preferred environment is Windows 11 x64 with Visual Studio 2026 and MSVC. The project needs reproducible dependency resolution, IDE integration, command-line builds, and a route for a dependency that is not available from the main vcpkg registry.

Milestone 1 also exposed a toolset compatibility boundary in the pinned VTK port. VTK's vendored diy2 copy of fmt selects the legacy `stdext::checked_array_iterator` path from `_SECURE_SCL`, but that path is not usable with the affected MSVC 19.50-and-newer secure-library configuration. This repository needs a narrow, reviewable patch without silently moving the entire dependency graph to a different vcpkg baseline.

The same baseline resolves GL2PS `1.4.2#4`, an unconditional VTK dependency,
from an archive endpoint that repeatedly timed out. Microsoft's official vcpkg
registry now contains GL2PS `1.4.2#5` with a corrected upstream archive
location. The project needs that port revision without upgrading unrelated
direct and transitive packages.

## Decision

Use:

- C++20 as the language baseline.
- Stable MSVC Build Tools from Visual Studio 2026.
- CMake 4.2 or newer with checked-in presets.
- vcpkg manifest mode pinned to registry commit
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6`.
- A package-scoped Git registry pointing to Microsoft's official vcpkg
  repository at commit `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`
  for `gl2ps` only. That registry resolves GL2PS as `1.4.2#5`; all other
  registry packages continue to use the root manifest's builtin baseline.
- `x64-windows` as the initial supported triplet.
- Repository-owned overlay ports under `thirdparty/vcpkg-ports/` for reviewed
  baseline compatibility changes and non-registry dependencies.

Keep the repository VTK overlay identical to the `vtk` port at that baseline,
versioned as `9.3.0-pv5.12.1#12`, except for the checked-in
`msvc-19.50-fmt-checked-iterator.patch` and its `PATCHES` entry. The patch changes
only the vendored diy2/fmt condition so `_SCL_SECURE_NO_WARNINGS` selects fmt's
existing raw-pointer fallback instead of `stdext::checked_array_iterator`. Rebase
the overlay from the exact registry port and reassess the patch whenever the
baseline changes.

Keep the TetGen overlay as a non-activatable guard until the Milestone 3 source,
integration, and license review is complete. TetGen must remain absent from the
root manifest and linked targets until that gate is approved.

Transient download failures do not authorize a permanent provenance change.
Build-only source mirrors may be used to diagnose or complete a local validation
run only from ignored build paths. They are not part of the shared dependency
configuration and must not be committed without a separate dependency, hash,
license, and maintenance decision.

The official GL2PS `#5` port remains unmodified. If its upstream endpoint is
temporarily unavailable, a vcpkg download or asset cache may provide only bytes
matching the port's complete SHA-512. A hash-identical cached asset does not
change the selected source, port recipe, or package version.

Use target-based CMake and keep platform/compiler logic in small CMake modules or target properties. Do not require preview language switches.

## Consequences

- Visual Studio and command-line builds share one project definition.
- Dependency updates are explicit and reviewable.
- GL2PS can advance to the official fixed acquisition recipe without moving the
  rest of the dependency graph or introducing a repository-owned GL2PS port.
- The minimum CMake version is newer because Visual Studio 2026 generator support begins at 4.2.
- Maintainers own the copied VTK overlay and must keep its relationship to the
  pinned baseline auditable.
- The narrow VTK patch is part of the dependency ABI input and must be retested
  when VTK, diy2/fmt, vcpkg, or MSVC changes.
- Maintainers own the future TetGen overlay but cannot activate it before its
  integration and license gates are complete.

## Alternatives Considered

### Visual Studio project files as the source of truth

Rejected because dependency integration, CI, and non-IDE builds would diverge.

### Vendoring all dependencies

Rejected because it increases update and maintenance cost without benefit for dependencies already maintained in vcpkg.

### Move to a newer dependency baseline only to obtain MSVC compatibility

Rejected for Milestone 1 because it would change the entire dependency graph.
The exact-baseline VTK overlay confines the compatibility change to the failing
vendored header and makes the delta directly reviewable.

### Move the complete dependency graph to a newer baseline for GL2PS

Rejected for Milestone 1 because it would also change VTK's transitive graph and
require rebasing the repository-owned VTK overlay. A package-scoped official
registry addresses the one acquisition defect while preserving the reviewed
default baseline.

### Commit the temporary GL2PS mirror overlay

Rejected. The build-only GL2PS `#4` mirror used a different archive provenance.
The official `#5` port plus vcpkg's SHA-512 verification removes the need for a
repository-owned GL2PS recipe or source substitution.

### C++23 baseline

Deferred until required dependencies and the selected stable compiler mode provide sufficient support without preview flags.

## Verification

- The repository VTK overlay differs from the port at baseline
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6` only by the compatibility patch and
  the portfile entry that applies it.
- MSVC 19.51.36252 built the patched VTK port and the project through the Visual
  Studio 2026 Debug and Release presets and the Ninja analysis preset.
- `vcpkg.json` and `vcpkg-configuration.json` pin the default registry, the
  `gl2ps`-only official registry, and the checked-in shared overlays.
- CMake target properties express project warnings, features, and includes; no
  checked-in build step depends on a developer-specific absolute path.

Fresh manifest resolution on 2026-08-22 selected `gl2ps 1.4.2#5` from official
registry port tree `51e4c4e828efb0b32efd657df71929bb9ba521d5` and retained the
existing versions of the direct dependencies and VTK overlay. The upstream
`geuz.org` endpoint timed out in this environment; the archive used to complete
the restore came from the MIT Gentoo distfiles mirror and matched the official
port's full SHA-512
`46652e1b3825ace61dbd77c4b0bf451e7671c248eb18bbd3369e2fac00056ea4cd5d2578561984313c239e3b02f78b9d9a76d963c935af65a13bc2abfc538620`.
No GL2PS overlay or modified port participated in the restore.

Fresh Visual Studio Debug and Release builds against that resolution each
passed 30/30 tests. A separate fresh Ninja build invoked clang-tidy 22.1.3 and
passed the same 30/30 tests; the checked-in clang-format gate also passed.

## Amendments

### 2026-08-22 — TetGen guard replaced after approval

ADR-0009 completed the source, integration, and license review required by this
decision. The historical non-activatable guard was replaced with a patchless
overlay for official TetGen v1.6.0, and the root manifest now consumes its
static `TetGen::TetGen` target privately through the project-owned adapter. The
AGPL-3.0-or-later combined-distribution and hosted-service gates in ADR-0009
remain active.
