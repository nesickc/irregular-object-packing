# Build and Dependency Workflow

Use this reference for CMake, presets, vcpkg, targets, third-party integration, or a new dependency.

## Build Baseline

- Target Windows 11 x64 and stable MSVC from Visual Studio 2026.
- Require C++20 without preview/latest language switches.
- Use CMake 4.2 or newer and target-based properties.
- Use checked-in CMake presets; keep machine-specific paths out of shared presets.
- Use vcpkg manifest mode with a pinned baseline and `x64-windows` initially.
- Put non-registry ports under `thirdparty/vcpkg-ports/` and consume them as overlays.

## CMake Rules

- Keep the initial production targets to `irop_core` and the thin `irop` executable.
- Attach include paths, features, definitions, warnings, and link libraries to targets with the narrowest visibility.
- Do not use global `include_directories`, `link_directories`, or compiler-flag mutation when target equivalents exist.
- Keep project warnings strict while treating third-party headers as external/system inputs.
- Keep configure-time feature options few, explicit, and exercised by CI before adding them.
- Make build-tree and install-tree behavior clear before declaring a public library contract.

## Dependency Admission

Before adding a dependency, record:

1. The specialized or substantial behavior it replaces.
2. Why the standard library or a small local implementation is insufficient.
3. Whether it crosses a public module boundary.
4. Its vcpkg availability and version-pinning strategy.
5. Its transitive size, runtime deployment effect, and security/update surface.
6. The test or vertical slice proving the integration.

Prefer the standard library for small utilities. Do not casually implement geometry kernels, nonlinear solvers, mesh parsers, or structured-data parsers.

## Verification

- Configure from a clean build directory with the supported preset.
- Build Debug and the configuration relevant to the task.
- Run CTest through the preset once tests exist.
- Confirm generated or downloaded files stay in build/vcpkg locations rather than source modules.
- Report exact commands and versions when changing the toolchain or baseline.

Add an ADR when replacing a foundational dependency, changing the build/dependency strategy, or changing the supported platform/language baseline.
