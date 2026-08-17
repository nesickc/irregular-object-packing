# ADR-0003: Windows, CMake, and vcpkg Toolchain

- Status: Accepted
- Date: 2026-08-18
- Deciders: Project maintainers

## Context

The preferred environment is Windows 11 x64 with Visual Studio 2026 and MSVC. The project needs reproducible dependency resolution, IDE integration, command-line builds, and a route for a dependency that is not available from the main vcpkg registry.

## Decision

Use:

- C++20 as the language baseline.
- Stable MSVC Build Tools from Visual Studio 2026.
- CMake 4.2 or newer with checked-in presets.
- vcpkg manifest mode with a pinned baseline.
- `x64-windows` as the initial supported triplet.
- A local overlay port under `thirdparty/vcpkg-ports/` for TetGen.

Use target-based CMake and keep platform/compiler logic in small CMake modules or target properties. Do not require preview language switches.

## Consequences

- Visual Studio and command-line builds share one project definition.
- Dependency updates are explicit and reviewable.
- The minimum CMake version is newer because Visual Studio 2026 generator support begins at 4.2.
- Maintainers own the TetGen overlay port and must keep it reproducible.

## Alternatives Considered

### Visual Studio project files as the source of truth

Rejected because dependency integration, CI, and non-IDE builds would diverge.

### Vendoring all dependencies

Rejected because it increases update and maintenance cost without benefit for dependencies already maintained in vcpkg.

### C++23 baseline

Deferred until required dependencies and the selected stable compiler mode provide sufficient support without preview flags.

## Verification

- A clean Windows 11 checkout configures with a checked-in Visual Studio 2026 preset.
- `vcpkg.json` and `vcpkg-configuration.json` pin dependency resolution.
- CMake target properties express project warnings, features, and includes.
- No required build step depends on a developer-specific absolute path.
