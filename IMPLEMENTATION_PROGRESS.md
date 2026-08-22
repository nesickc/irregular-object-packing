# C++ Migration Implementation Progress

Last updated: 2026-08-22

This working report tracks execution of `docs/IMPLEMENTATION_PLAN.md`. The concise,
authoritative project status remains in `docs/STATUS.md`; this file records the more
detailed implementation trail requested for the migration.

## Current Position

- Active milestone: Milestone 3 — Tetrahedralization and CAT Constraints.
- State: Milestones 1 and 2 are Verified; Milestone 3 has not started.
- Current outcome: `irop_core` and the thin CLI now inspect STL meshes and emit a
  deterministic, bounded, atomically published initialized placement scene with
  combined/optional individual STL, canonical placements, and a successful run
  summary. The scoped official GL2PS `1.4.2#5` graph passes Debug, Release, and
  clang-tidy analysis gates.
- Blocking ambiguities: none for the completed work. Milestone 3 begins with the
  explicit TetGen license/source-model decision already required by ADR-0006.

## Decisions Applied

- Preserve the Python implementation as the behavioral reference through parity.
- Keep the command-line application thin and dependency-specific VTK types inside the
  mesh I/O and geometry adapters.
- Represent meshes and results with project-owned types at core boundaries.
- Treat input meshes and output paths as untrusted and enforce preflight plus post-load
  resource and geometry validation.
- Use STL as the inspectable mesh output and JSON as the canonical structured record.
- Fix inspection artifact leaves as `normalized.stl` and
  `inspection-summary.json`, publish without overwrite, and publish the JSON success
  record only after the geometry artifact is complete.
- Use stable process outcomes 0 (success), 2 (usage), 3 (input), 4 (resource limit),
  5 (output), and 70 (internal).
- Preserve coincident-point merging used by the Python/PyVista read path. Rejecting
  malformed and unsafe inputs is new trust-boundary behavior, not a compatibility
  deviation from a demonstrated successful Python case.
- Keep the TetGen overlay disabled and unlinked until the Milestone 3 license and
  integration review.
- Keep the reviewed default vcpkg baseline and route only GL2PS to the pinned newer
  Microsoft registry that contains `1.4.2#5`; do not introduce a local GL2PS port.
- Permit a vcpkg download-cache fallback only when the acquired GL2PS archive matches
  the official port's complete SHA-512.
- Use the checked-in `.clang-format` file as the sole formatting authority.
- Store a volume scale and derive linear scale with `cbrt`; apply column-vector
  transforms in `Ry * Rz * Rx` order before translation.
- Center source meshes at the Python-compatible unweighted vertex centroid and persist
  the original-input-to-world matrix separately from centered-template placement
  fields.
- Give each packing run its own NumPy-legacy-MT19937-compatible stream and preserve
  Python's coordinate/rejection/rotation draw order.
- Bound candidate attempts, geometry triangle visits, center-distance checks, and
  exact object/container surface triangle-pair tests.
- Publish every initialization artifact together by renaming a private staging
  directory to a previously absent final path after `run-summary.json` is complete.
- Implement only the adaptive sampling ratio/target-count policy in Milestone 2;
  actual iteration-time remeshing remains packing-loop work.

## Milestone 1 Checklist

- [x] Add target-based CMake configuration and checked-in Windows presets.
- [x] Add pinned vcpkg manifest/configuration and dependency license inventory.
- [x] Add a non-activatable TetGen overlay-port skeleton.
- [x] Configure C++20, MSVC warnings, CTest, formatting, and focused static analysis.
- [x] Add `TriangleMesh` and project-owned inspection/result/error types.
- [x] Add a VTK STL adapter with byte/count limits and post-load validation.
- [x] Add a thin `irop inspect` command with stable exit categories.
- [x] Emit normalized STL and `inspection-summary.json` for valid input.
- [x] Add focused model, adapter, malformed-input, and CLI smoke tests.
- [x] Configure, build, test, format-check, analyze, and review the completed slice.
- [x] Synchronize `docs/STATUS.md` and record exact verification evidence here.

## Milestone 2 Checklist

- [x] Add project-owned volume-scale transforms and Eigen-private matrix composition.
- [x] Port vertex-centroid centering, closed-surface volume/bounds/containment,
  maximum radius, mesh instantiation, and checked mesh combination.
- [x] Port adaptive surface target-count policy with Python binary64 truncation and a
  safe closed-surface minimum.
- [x] Add `PackingConfig`, `PackingState`, and independent deterministic random-state
  ownership.
- [x] Match NumPy legacy MT19937 uniform mapping and complete coordinate rejection then
  rotation draw order against a Python golden.
- [x] Preserve the valid one-object origin shortcut and add an exact bounded surface
  containment fallback when its conservative sphere does not fit.
- [x] Add configurable candidate, geometry-query, pairwise, and surface-intersection
  work limits with focused exhaustion tests and CLI exit 4 coverage.
- [x] Add `irop initialize`, combined and optional individual STL artifacts,
  canonical placements, timings/work metrics, and checked-in version-one schemas.
- [x] Publish initialization outputs atomically as a new directory and verify failure
  paths publish no success set.
- [x] Normalize winding in query-owned snapshots and reject ambiguous multiple closed
  components pending explicit cavity semantics.
- [x] Add ADR-0008, direct Eigen license inventory, and compatibility entries through
  `IROP-DEV-0012`.
- [x] Pass scoped-registry Debug, Release, format, clang-tidy, and full CTest gates.

## Activity Log

### 2026-08-21 — Milestone 1 started

- Read the required project definition, implementation status, full implementation
  plan, compatibility catalog, relevant ADRs, and repository C++ development workflow.
- Confirmed that the repository contains no C++ build or implementation files yet and
  that Milestone 1 is the documented next gate.
- Began inventorying the Python mesh behavior, existing STL samples, repository state,
  and locally available Windows toolchain.

### 2026-08-21 — Build foundation implemented

- Added target-based CMake with `irop_core`, `irop`, and `irop_tests`; Visual Studio
  Debug/Release presets; a Ninja analysis preset; strict MSVC warnings; CTest;
  clang-format targets; and clang-tidy integration.
- Pinned the vcpkg baseline and the Milestone 1 dependency subset. Enabled only the
  required VTK feature set, including Windows UTF-8 path support.
- Added a repository-owned VTK overlay matching the pinned baseline plus a narrow
  MSVC 19.50 compatibility patch for VTK's vendored diy/fmt code. The unpatched pinned
  VTK failed to compile with MSVC 19.51 because it references removed
  `stdext::checked_array_iterator` support.
- Added a hard-disabled TetGen overlay skeleton that fails configuration if activated;
  TetGen is absent from the root manifest and all targets.
- Added a Windows application manifest for UTF-8 command-line arguments and long-path
  awareness.

### 2026-08-21 — Secure STL inspection slice implemented

- Added project-owned errors, `TriangleMesh`, limits, validation summaries,
  the inspection result and function contract, and dependency-free public headers.
- Added bounded STL preflight for regular-file size, encoding, binary count/length and
  finite record components, checked conversions, and configurable byte/triangle
  limits. The ASCII scan bounds line length and facet/vertex records before VTK;
  numeric finiteness is enforced by VTK diagnostics and post-load validation.
- Added VTK read diagnostics, coincident-point merging, and post-load checks for empty
  geometry, merged counts, triangle-only cells, valid indices, finite bounds, repeated
  vertices, and zero-area or numerically unsafe triangles.
- Added binary normalized-STL writing with float-range and float-quantization safety,
  exact output-size verification, transactional no-overwrite publication, and a
  versioned JSON summary whose schema disallows unknown fields.
- Added the thin `irop inspect INPUT --output-dir DIR` interface, stable exit taxonomy,
  bounded diagnostics, fixed output leaves, and UTF-8 Windows path handling.
- Added 29 focused Catch2 tests and one CLI smoke test covering ASCII/binary round
  trips, a real checked-in STL, malformed/truncated input, count and byte limits,
  non-finite and degenerate geometry, binary headers beginning with `solid`, BOM
  rejection required by pinned VTK 9.3, Unicode paths, output collision/failure,
  schema fields, artifacts, and externally inducible CLI exit categories 0/2/3/4/5.
  Exit 70 remains source-mapped for unexpected internal failures without an artificial
  runtime fault seam.

### 2026-08-21 — Review and acceptance pass completed

- Kept VTK, nlohmann JSON, CLI11, and spdlog types private to implementation targets;
  public headers expose only project and standard-library types.
- Confirmed that Python defines successful STL loading but no writer, inspection CLI,
  JSON summary, or unsafe-input contract. No Milestone 1 catalog marker was needed.
- Fixed review findings around binary count overflow, VTK's signed allocation bound,
  merged-vertex limit semantics, writer float quantization, UTF-8 paths,
  transactional no-overwrite publication, close/write errors, and no-throw top-level
  error handling.
- Completed independent source/API review with no remaining Milestone 1
  implementation or API blocker.

### 2026-08-22 — Official GL2PS registry restore verified

- Added a package-scoped Git registry at official vcpkg commit
  `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9` for `gl2ps` only. The root
  manifest's builtin baseline remains
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6`, so unrelated package versions
  did not move.
- Confirmed that fresh resolution selects GL2PS `1.4.2#5` from official port tree
  `51e4c4e828efb0b32efd657df71929bb9ba521d5` and the repository-owned VTK
  overlay from `thirdparty/vcpkg-ports/vtk`. Comparing the old and fresh vcpkg
  status records showed only the intended GL2PS port revision change from `#4`
  to `#5`.
- The official `geuz.org` archive endpoint timed out through both vcpkg and curl.
  The MIT Gentoo distfiles mirror supplied a 301,134-byte copy matching the
  official port's full SHA-512, which was placed in vcpkg's normal download
  cache. No GL2PS overlay, recipe change, or alternative source tree was used.
- Documented the equivalent `vcpkg x-download` cache-seeding command against
  Gentoo's distfiles CDN for repeatable recovery while retaining vcpkg's hash
  enforcement.
- Completed fresh Visual Studio Debug and Release builds, 30/30 tests in each,
  the formatting gate, and a fresh Ninja build with clang-tidy plus 30/30 tests.
  Milestone 1 is now Verified.

### 2026-08-22 — Milestone 2 reference and contract implemented

- Audited the live Python transform, centering, adaptive count, initialization,
  one-object shortcut, collision-validation, and random draw order rather than porting
  dead grid code.
- Added dependency-free public transform, geometry, sampling, configuration, state,
  initialization-service, and result contracts. Eigen3 is direct but private, with
  `EIGEN_MPL2_ONLY`; VTK remains behind STL and geometry adapters.
- Matched volume scale, `Ry * Rz * Rx`, vertex-centroid centering, uniform AABB
  candidate generation, strict radius clearance/spacing, coordinates-before-rotations
  draw order, and NumPy legacy MT19937's 53-bit uniform mapping.
- Added a full Python golden for seed 12345/count 8: six rejected candidates, 66
  logical random draws, and exact first/last translations and rotations.
- Ported the adaptive sampling ratio and binary64-truncated target-count policy. The
  safe four-triangle minimum is intentional; actual remeshing is deferred until the
  packing loop consumes the policy.

### 2026-08-22 — Milestone 2 safety and artifact review completed

- Added positive limits for candidate attempts, container-triangle visits,
  center-distance checks, and exact surface triangle-pair tests; every limit has a
  focused resource-exhaustion regression and geometry-query exhaustion maps to CLI
  exit 4 without publishing output.
- Preserved a valid one-object origin transform even when its bounding sphere is too
  conservative by combining strict transformed-vertex containment with VTK's bounded
  triangle-surface intersection primitive. Invalid origins use the bounded sampler.
- Made closed queries own immutable mesh snapshots, normalize local winding without
  mutating callers, reject open/non-manifold input, and reject multiple connected
  shells until cavity semantics are explicit.
- Strengthened initial-state validation for finite transforms, strict thresholds, and
  wholly outside nonintersecting objects.
- Added `irop initialize` with input/output/work controls, combined and optional
  individual STL, original-input-to-world matrices, separate versioned placements and
  success-summary schemas, timings, work metrics, warnings, and dependency versions.
- Replaced sequential final-path writes with private sibling staging plus one atomic
  directory rename. The requested final directory must be absent; failures leave no
  published success artifact set.
- Recorded ADR-0008 and synchronized compatibility markers through
  `IROP-DEV-0012`, dependency notices, project definition, status, and this report.

### 2026-08-22 — Milestone 2 verified

- The scoped official registry continued to select GL2PS `1.4.2#5` and direct Eigen3
  `3.4.1#1`; no GL2PS or Eigen source overlay was added.
- Visual Studio Debug and Release builds completed with warnings as errors and passed
  all 80 CTest cases in each configuration.
- The Ninja analysis build ran clang-tidy 22.1.3 over the affected targets and passed
  all 80 tests after a clean reconfiguration in the Visual Studio developer shell.
- Final API audit coverage rejects non-finite coordinates created by homogeneous
  division and avoids out-of-range integer conversion at the maximum sampling-count
  endpoint.
- The checked-in clang-format 22.1.3 compliance target passed. Milestone 2 is
  Verified and Milestone 3 is next.

## Verification Evidence

- Toolchain: CMake 4.2.3; Visual Studio Community 2026 18.8.3; MSVC 19.51.36252;
  Windows SDK 10.0.26100.0; default vcpkg baseline
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6`; GL2PS-only registry baseline
  `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`; clang-format/clang-tidy 22.1.3.
- Configure: `cmake --preset windows-vs2026 -B
  build/windows-vs2026-scoped-gl2ps` succeeded from a fresh tree, selected GL2PS
  `1.4.2#5`, and used no temporary GL2PS overlay.
- Debug: `cmake --build build/windows-vs2026-scoped-gl2ps --config Debug`
  succeeded; `ctest --test-dir build/windows-vs2026-scoped-gl2ps -C Debug
  --output-on-failure` passed 80/80 tests.
- Release: `cmake --build build/windows-vs2026-scoped-gl2ps --config Release`
  succeeded; `ctest --test-dir build/windows-vs2026-scoped-gl2ps -C Release
  --output-on-failure` passed 80/80 tests.
- Formatting: `cmake --build build/windows-vs2026-scoped-gl2ps --config Debug
  --target irop-format-check` passed with the checked-in profile.
- Analysis: a fresh `windows-ninja-analysis` configuration at
  `build/windows-ninja-analysis-scoped-gl2ps-vcpkg-root` generated build rules
  invoking clang-tidy 22.1.3, built successfully, and passed 80/80 tests.
- The Release executable embeds `activeCodePage=UTF-8` and `longPathAware=true`; the
  Unicode-path CLI smoke test passed in all three fresh test configurations.

## Risks and Follow-up

- The official GL2PS `geuz.org` download endpoint was unreachable during verification.
  The scoped official port is unchanged, and the fallback archive is byte-identical
  under its complete vcpkg SHA-512. Clean machines affected by the same outage must
  run the documented `vcpkg x-download` cache-seeding command or use an organization
  asset cache before configuring.
- The pinned VTK overlay patch is intentionally narrow and should be removed when a
  reviewed upstream VTK/vcpkg baseline supports MSVC 19.50+ without it.
- VTK's compile-tools package emits a cross-compilation support warning under this
  native MSVC configuration. Both configurations link and all runtime tests pass, but
  the warning should be re-evaluated with the next VTK baseline update.
- Current containment/distance queries are linear in container triangle count and the
  exact compatibility fallback is quadratic in object/container surface triangles.
  Explicit work limits bound them; replace them with measured acceleration only after
  profiling representative inputs.
- Initialization accepts exactly one connected closed surface component. Supporting
  nested cavity shells or disjoint solids requires an explicit domain contract rather
  than summing component magnitudes.
- Initialization resource failures use stable diagnostics/exit categories and publish
  no success set; a persisted structured unsuccessful result is deferred to the later
  packing-result coordinator.
- Each initialization output path is a new atomic artifact-set identity and cannot be
  an existing reusable directory.
- Next implementation work is Milestone 3. TetGen remains license-gated until its
  source/publication model is explicitly approved under ADR-0006.
