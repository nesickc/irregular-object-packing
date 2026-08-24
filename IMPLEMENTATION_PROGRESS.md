# C++ Migration Implementation Progress

Last updated: 2026-08-24

This working report tracks execution of `docs/IMPLEMENTATION_PLAN.md`. The concise,
authoritative project status remains in `docs/STATUS.md`; this file records the more
detailed implementation trail requested for the migration.

## Current Position

- Active milestone: Milestone 6 — Compatibility, Robustness, and Release Readiness.
- State: Milestones 1 through 5 are Verified; Milestone 6 is In Progress.
- Current outcome: `irop_core` composes bounded initialization, adaptive
  resampling, TetGen, CAT, deterministic Ipopt solves, full-resolution collision
  correction, exact scale barriers, termination, and output-quantized validation.
  Accepted local rotations are now left-composed in the same order as the
  constraint model. Packing-local scale objectives use overflow-safe finite
  near-barrier bounds plus independently postchecked exact-target snapping.
  The thin `irop pack` command atomically emits versioned success artifacts or a
  summary-only structured failure. Public headers expose only project-owned types.
  Visual Studio Debug/Release and the Ninja clang-tidy configuration pass 163/163
  tests.
- Blocking ambiguities: none. ADR-0009 records the approved TetGen AGPL source and
  integration path; ADR-0010 records the exact Ipopt binary path and public-binary
  review gate; ADR-0011 records the packing outcome/artifact contract; ADR-0012
  records the transform-consistent barrier-bounded solve correction. The
  compatibility catalog is synchronized through `IROP-COMPAT-0007` and
  `IROP-DEV-0023`.

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
- Use stable process outcomes 0 (success), 2 (usage), 3 (input), 4 (resource/time
  limit), 5 (output), 6 (other structured packing failure), 70 (internal), and
  130 (packing cancellation).
- Preserve coincident-point merging used by the Python/PyVista read path. Rejecting
  malformed and unsafe inputs is new trust-boundary behavior, not a compatibility
  deviation from a demonstrated successful Python case.
- Pin official TetGen v1.6.0 commit
  `535f9c41f44abc832a7bbf2c9c7af003d1c18f3c` through a patchless local vcpkg
  overlay, build it statically with `TETLIBRARY`, and keep its types behind a private
  adapter. Project-owned source remains BSD-3-Clause; future conveyed combined builds
  and hosted services retain the ADR-0009 AGPL review gate.
- Preserve the Python reference's literal `O0/0Q` TetGen string. Its historical
  wrapper bypasses the accompanying `cdt=True` and `steinerleft=0` keywords, so the
  compatibility path tetrahedralizes the participant point union rather than silently
  enabling the intended PLC/CDT configuration.
- Serialize calls into TetGen because its exact predicates use mutable process-global
  state. Validate closed participant surfaces and all translated output, enforce
  project-controlled input/accepted-output limits, and document that backend peak
  memory/time cannot be hard-capped through the library API.
- Keep CAT outputs contiguous by participant with explicit polygon/constraint ranges,
  bounded work counters, and optional no-overwrite VTU/VTP diagnostic writers.
- Package the source-unmodified official Ipopt 3.14.19 MD/MDD Windows archives
  through a hash-pinned x64-windows vcpkg overlay. Consume only the C ABI inside a
  private adapter, require the exact loaded version, select MUMPS explicitly, and
  disable ambient `ipopt.opt` loading.
- Keep random initialization outside the local-solver contract. Use an explicit
  seven-variable initial guess, exact objective gradient and dense constraint
  Jacobian, and a reusable workspace owned by the caller.
- Accept only successful, acceptable, or feasible Ipopt candidates that satisfy
  variable bounds and independent constraint checks in both solver space and the
  geometry produced by the actually stored transform.
- Left-compose accepted incremental rotations with the current orientation and encode
  the result canonically in the existing `Ry * Rz * Rx` Euler representation.
- Bound packing-local scale objectives near each barrier with fixed representable
  slack, finite saturation for extreme ratios, exact clamping, and an independently
  postchecked near-target snap.
- Bound constraint rows, dense Jacobian entries, callback work, iterations, and
  elapsed time. Time checks are cooperative around callback chunks; an active MUMPS
  factorization and its peak allocation cannot be hard-preempted through this API.
- Do not publicly distribute the exact Intel-linked Ipopt archive closure until the
  ADR-0010 release-specific notice, source-availability, binary-term, and TetGen AGPL
  review is complete.
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
- Run the packing coordinator sequentially in object-ID order, continue the
  run-owned NumPy-compatible MT19937 stream, preserve exact scale barriers and
  cataloged Python quirks, and stop with project-owned outcomes rather than
  silently advancing an unconverged barrier.
- Use private VTK resampling for the Python target-count policy, while physical
  correction and final acceptance operate on full-resolution closed surfaces.
- Bound correction, history, collision work, generated geometry, nested adapter
  work, local solves, and elapsed time; poll cancellation between input,
  initialization, coarse engine, and successful artifact-publication boundaries.
  Preserve an already-final unsuccessful engine outcome if interruption arrives
  only while its summary is being published.
- Resolve an omitted translation base to twice the cube root of full-size object
  volume; retain an explicit positive base and the reference-compatible
  barrier multiplication.
- Publish packing results with a private staging directory. Success contains the
  four fixed artifacts (plus optional individual STLs); expected failure contains
  only `run-summary.json`; input/output failures publish no run directory.
- Revalidate the exact float32-coordinate meshes used by the binary STL writer
  before success and make the version-one schema enforce the success/failure
  artifact and physical-validation invariant.

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

## Milestone 3 Checklist

- [x] Approve and document the exact TetGen 1.6.0 AGPL source/integration path.
- [x] Replace the guard port with a patchless vcpkg overlay that exports
  `TetGen::TetGen` and installs the complete upstream license.
- [x] Add a dependency-private adapter with project-owned points, owners,
  tetrahedra, limits, work counters, statuses, and diagnostics.
- [x] Preserve the effective Python `O0/0Q` point-union behavior and regression-test
  mixed-participant tetrahedra across separated surfaces.
- [x] Validate closed single-component participant surfaces, translated point order,
  ownership, indices, finite values, nondegeneracy, and accepted output counts.
- [x] Serialize TetGen calls and make every nested TetGen input allocation cleanup-safe
  under exceptions.
- [x] Port relevant-cell filtering, stable ownership classification, all four CAT
  split cases, per-owner polygon reuse, constraints, and contiguous ranges.
- [x] Match complete Python split goldens and verify finite unit inward normals and
  polygon coplanarity across every ownership partition.
- [x] Bound tetrahedralization and CAT work with focused exhaustion regressions and
  project-owned failure translation.
- [x] Add validated, no-overwrite VTU tetrahedral and VTP CAT diagnostic writers.
- [x] Pass isolated vcpkg port checks, Visual Studio Debug/Release, format,
  clang-tidy, focused review, and full CTest gates.

## Milestone 4 Checklist

- [x] Audit the live Python objective, constraints, bounds, transform application,
  derivative strategy, initialization boundary, and ignored solver outcomes.
- [x] Approve and document the exact official Ipopt 3.14.19 Windows archive path,
  recursive runtime closure, component licenses, provenance, and public-binary gate.
- [x] Add a hash-pinned x64-windows vcpkg overlay for the source-unmodified MD/MDD
  archives and validate it cache-bypassed with `--enforce-port-checks`.
- [x] Add project-owned local-step, constraint, request, bound, limit, work, status,
  result, and reusable-workspace contracts with no public Ipopt or Eigen types.
- [x] Implement the Python-compatible seven-variable objective and CAT plane
  constraints with exact objective and constraint derivatives.
- [x] Isolate Ipopt's C ABI in a private adapter, require runtime 3.14.19, select
  MUMPS, disable ambient option files, and translate every backend outcome.
- [x] Validate participant ownership/ranges, finite points/normals/bounds/initial
  guesses, positive scale, problem size, and dense-Jacobian arithmetic before Ipopt.
- [x] Enforce preparation, constraint-row, Jacobian-entry, callback, iteration, and
  cooperative elapsed-time limits with structured failure results.
- [x] Initially preserve solve-then-clamp and componentwise Euler addition with
  solver/applied postchecks; retire both paths through ADR-0012 in Milestone 6 after
  practical failures established their downstream harm.
- [x] Add Python numerical goldens, central-difference derivative checks, box,
  asymmetric, genuine seven-variable, fixed/infeasible, hostile-option-file,
  malformed-input, resource-limit, clamping, and workspace-reuse tests.
- [x] Benchmark exact analytic derivatives against Python-compatible forward
  differences on 2,048 constraints.
- [x] Pass the isolated overlay audit, Visual Studio Debug/Release, format,
  clang-tidy, independent review, and full CTest gates.

## Milestone 5 Checklist

- [x] Audit the live Python barrier, resampling, recovery, local-guess,
  collision-correction, random-draw, and termination behavior.
- [x] Add project-owned packing configuration, limit, callback, progress,
  history, work, validation, result, and application-service contracts.
- [x] Implement exact linear volume-scale barriers, adaptive VTK resampling,
  TetGen/CAT rebuilding, deterministic per-object Ipopt solves, transactional
  state updates, and bounded recovery/correction.
- [x] Add strict full-resolution container and object collision/nesting checks
  with cumulative work limits and no repeated structural validation per pair.
- [x] Require final physical validation and repeat it on the exact float32
  coordinates serialized by the binary STL writer.
- [x] Add `irop pack` with one-based progress logging, Ctrl+C cancellation,
  stable exits, input/engine/output controls, and thin CLI orchestration.
- [x] Atomically publish the four fixed success artifacts plus optional
  individual STLs, or a summary-only expected unsuccessful result.
- [x] Add version-one placements/run-summary schemas whose conditional contract
  rejects misleading success records.
- [x] Cover genuine growth, adaptive orchestration, collision predicates and
  budgets, cancellation, resource exhaustion, infeasibility/exit 6, malformed
  input, no-overwrite output, serialization quantization, and schemas.
- [x] Record ADR-0011, synchronize compatibility through
  `IROP-COMPAT-0007`/`IROP-DEV-0021`, and add short README usage/licensing
  guidance.
- [x] Pass Visual Studio Debug/Release, 154/154 CTest cases, clang-format,
  Ninja clang-tidy with 154/154 tests, schema validation, and final review.

## Milestone 6 Checklist

- [x] Reproduce the reported two-object rotation-enabled postcheck failure and
  full-scale iteration-limit failure on the supplied STL pair.
- [x] Retire `IROP-COMPAT-0001` and `IROP-COMPAT-0006`; record ADR-0012,
  `IROP-DEV-0022`, and `IROP-DEV-0023`.
- [x] Persist accepted rotations through exact `R_delta * R_current` composition
  with canonical `Ry * Rz * Rx` extraction and gimbal-lock coverage.
- [x] Bound packing scale objectives near the active barrier, saturate extreme
  ratios below the solver sentinel, and independently postcheck exact-target snaps.
- [x] Generate primitive geometry in test code for obvious cube/cylinder fits,
  separated objects, oversized non-fit, rotation-required rod, full-scale cylinder
  and tetrahedron, and rotation-enabled two-object growth.
- [x] Re-run both exact reported commands; require exact target scales and passing
  full-resolution/output validation.
- [x] Pass Visual Studio Debug/Release and Ninja clang-tidy builds with 163/163
  tests in each configuration.
- [ ] Build the broader representative Python/C++ transform, outcome, collision,
  and artifact parity corpus.
- [ ] Expand hostile-input, rare recovery/correction, and process-level SIGINT
  coverage and profile representative adaptive runs.
- [ ] Add hosted CI/release checks and close ADR-0009/ADR-0010 distribution gates.

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

### 2026-08-22 — TetGen AGPL path and patchless overlay approved

- Recorded the maintainer's selection of official TetGen v1.6.0 under
  AGPL-3.0-or-later in ADR-0009. Static versus dynamic linkage is treated as an
  engineering choice rather than a license workaround; project-owned source remains
  under its BSD-3-Clause notice, and no binary distribution is currently planned.
- Replaced the non-activatable guard with a patchless vcpkg port pinned to commit
  `535f9c41f44abc832a7bbf2c9c7af003d1c18f3c` and full-commit archive SHA-512
  `62e5fc640f72e594ad7d7286075f85cb590d4a71b979e0b035d545543e4d80807db26c2f56775032e4a94abbdaf411473273bd304ad77bf1a451c9db435dcfcf`.
- The port-owned CMake wrapper compiles unmodified `tetgen.cxx` and `predicates.cxx`,
  exports a static `TetGen::TetGen` target with `TETLIBRARY`, and installs
  `tetgen.h` plus the complete upstream license. An isolated x64-windows
  Debug/Release install passed vcpkg `--enforce-port-checks`.

### 2026-08-22 — Tetrahedralization and Python parity implemented

- Added the dependency-private adapter and project-owned tetrahedral mesh, ownership,
  limits, work, result, and status contracts. Public headers remain free of TetGen,
  VTK, and Eigen types.
- Historical PyVista TetGen v0.6.0 source confirms that a nonempty `switches` value
  takes the literal API branch and bypasses keyword behavior. The adapter therefore
  preserves the Python reference's `O0/0Q` point-union cell complex under
  `IROP-COMPAT-0005` instead of silently adding PLC/CDT/zero-index/no-Steiner flags.
- Added closed-surface validation, checked count/index conversions, exact output point
  order/ownership verification, finite/nondegenerate cell checks, accepted-output
  limits, and project-owned dependency-failure diagnostics.
- Serialized the complete TetGen call around its mutable global predicate state.
  Review also hardened nested facet allocation ordering and value-initialized every
  facet before any throwing nested allocation.

### 2026-08-22 — CAT constraints and diagnostics implemented

- Ported relevant-cell filtering, stable descending ownership classification, all
  four split partitions, Python-compatible polygon geometry, per-owner polygon reuse,
  one constraint per source vertex/face pair, and participant-contiguous ranges.
- Added complete Python goldens for 1+1+1+1, 2+1+1, 3+1, and 2+2 splits plus
  split-wide finite unit inward-normal, coplanarity, ownership, range, malformed mesh,
  and every configured work/output-limit regression.
- Added optional VTU tetrahedralization and VTP CAT writers using private VTK XML
  adapters. They validate project geometry and cross-references, reserve a previously
  absent output leaf, remove incomplete files on failure, and never overwrite.
- Independent integration and design review closed findings in exception cleanup,
  process-global concurrency, literal-switch parity, diagnostic cross-references,
  backend-failure translation, and invariant-test breadth.

### 2026-08-22 — Milestone 3 verified

- Visual Studio Debug and Release builds completed with warnings as errors and passed
  all 102 CTest cases in each configuration. The focused tetrahedralization, CAT, and
  diagnostic subset passed 22/22.
- The Ninja analysis build ran clang-tidy 22.1.3 over all affected project targets and
  passed all 102 tests. Explicit non-empty/count invariants made TetGen raw-array
  marshalling analyzable without suppressing the security checks.
- The checked-in clang-format 22.1.3 compliance target passed. Compatibility entries
  `IROP-COMPAT-0005`, `IROP-DEV-0013`, and `IROP-DEV-0014` are verified. Milestone 4
  is next.

### 2026-08-22 — Ipopt binary path and private boundary approved

- Confirmed that both the pinned and current official source ports lack a runnable
  sparse linear solver in this Windows configuration. Selected the source-unmodified
  official Ipopt 3.14.19 MSVC MD/MDD archives with MUMPS through a repository-owned
  vcpkg overlay instead of changing the reviewed global baseline or patching upstream
  source.
- Pinned complete Release and Debug archive SHA-512 values, pruned unused AMPL/Java/
  sIpopt material, installed the exact recursive DLL closure, and exported
  `Ipopt::Ipopt`. A cache-bypassed isolated install passed vcpkg
  `--enforce-port-checks`.
- Recorded the C-ABI, provenance, component-license, Debug nonredistribution, and
  public-binary review decisions in ADR-0010. Local development and source
  publication can proceed; the exact Intel-linked bundle is not release-approved.

### 2026-08-22 — Local nonlinear solve implemented

- Added dependency-free public local-solver contracts and a caller-owned reusable
  workspace. Random draws remain the packing coordinator's responsibility, and each
  solve receives an explicit seven-variable initial guess.
- Ported the Python objective, `cbrt` volume scaling, `Ry * Rz * Rx` incremental
  rotation, translation, and CAT plane constraints. Added exact objective and dense
  constraint derivatives, including analytic rotation derivatives.
- Added a private Ipopt C adapter that checks runtime 3.14.19 and MUMPS availability,
  disables ambient `ipopt.opt`, applies bounded options, uses limited-memory Hessian
  approximation, and translates every C status into project-owned outcomes.
- Hardened the boundary with checked sized spans, ownership/range/finite/unit-normal
  validation, fixed-problem handling, exhaustive work accounting, cooperative time
  checks, and atomic accepted-transform publication.
- Preserved solve-then-clamp and componentwise Euler addition under
  `IROP-COMPAT-0001` and `IROP-COMPAT-0006`, but independently re-evaluated both
  the solver candidate and the geometry produced by the stored transform. Failed or
  infeasible iterates are no longer applied under `IROP-DEV-0015`; unsafe or
  unbounded problem assumptions are corrected under `IROP-DEV-0016`.

### 2026-08-22 — Milestone 4 verified

- Added 12 focused local-solver Catch2 cases, bringing the suite to 112 focused tests
  plus two CLI smoke tests. Coverage includes Python numerical goldens, analytic
  derivatives, representative Ipopt solves with all seven variables available,
  hostile ambient options, invalid inputs, fixed infeasibility, resource exhaustion,
  clamp-induced infeasibility, and workspace reuse.
- Visual Studio Debug and Release builds completed with warnings as errors and passed
  all 114 tests. The Ninja analysis build ran clang-tidy 22.1.3 over the affected
  targets and passed all 114 tests after its buffer-capacity and no-throw cleanup
  findings were resolved without suppressions.
- The checked-in clang-format gate passed. The hidden 2,048-constraint benchmark
  measured 150.222 microseconds for the exact analytic Jacobian versus 1.32158
  milliseconds for forward differences, about an 8.8x speedup.
- Independent production/API review found no remaining Milestone 4 blocker.
  Milestone 5 is next.

### 2026-08-22 — End-to-end packing coordinator implemented

- Audited the Python coordinator and implemented exact barriers, adaptive
  surface resampling, TetGen/CAT rebuilding, deterministic transaction-local
  per-object solves, continued run-owned random draws, bounded recovery and
  collision correction, history/work accounting, and explicit termination.
- Added strict collision and containment/nesting validation over full-resolution
  meshes. Scene comparisons now validate each mesh once and reuse an
  already-validated narrow intersection primitive under cumulative work limits.
- Preserved CAT-diagnostic exclusion and barrier-scaled translation behind
  compatibility markers; corrected threaded state races, silent iteration
  advancement, unsafe surface-only acceptance, unbounded correction, and the
  effectively unbounded default translation setting through cataloged deviations.

### 2026-08-22 — Packing CLI and artifact contract implemented

- Added `irop pack`, cooperative SIGINT handling, one-based interactive
  progress, stable packing exits, CLI-configured resource limits, and early
  project-owned configuration validation.
- Added atomic success publication of `packed-objects.stl`, `container.stl`,
  `placements.json`, and `run-summary.json`, plus optional individual STLs.
  Resource, infeasible, engine-cancelled, and post-success-cancelled outcomes
  publish summary-only; invalid input/configuration, pre-engine cancellation,
  and output failures publish no run directory.
- Added explicit final-validation state and checked-in version-one schemas. The
  run-summary schema conditionally requires fixed artifacts and passed physical
  validation for success, and null/empty success outputs for every failure.
- Factored the binary-STL float quantization into a reusable helper. The
  application validates and writes the same quantized meshes, preventing a
  sub-ULP contact introduced by serialization from being published as success.

### 2026-08-22 — Milestone 5 verified

- The genuine-growth CLI smoke traversed STL loading, TetGen, CAT, Ipopt,
  full-resolution/output-quantized validation, and the four-artifact success
  transaction. It also verified summary-only resource exhaustion and a
  deterministic infeasible exit 6.
- Focused tests cover adaptive orchestration, collision and containment cases,
  cumulative budgets, run-owned random continuation, cancellation during the
  initialization and success-to-publication boundaries, pre-input cancellation
  with no artifacts, malformed input, no-overwrite output, schema invariants,
  and float quantization.
- Visual Studio Debug and Release and the Ninja clang-tidy build each passed
  154/154 tests. The clang-format gate, `git diff --check`, emitted-instance
  schema validation, and misleading-success schema rejection passed.
- Independent implementation and acceptance reviews were incorporated.
  Milestone 6 is next.

### 2026-08-24 - Milestone 6 transform and barrier robustness tranche

- Reproduced the supplied two-object failures and separated their causes. With
  rotation enabled, additive Euler persistence produced a different orientation
  from the solver's `R_delta * R_current` constraint geometry and failed the
  applied-transform postcheck. With rotation disabled at `0.999 -> 1.0`, the
  unconstrained scale objective spent 533 iterations on the first object and
  exhausted the 1,000-iteration limit on the second.
- Replaced additive Euler updates with exact left composition and canonical
  `Ry * Rz * Rx` extraction. Matrix invariants cover non-commuting rotations and
  exact/near positive and negative gimbal lock.
- Added overflow-safe finite scale bounds with fixed representable slack,
  saturation below the adapter's `1e19` sentinel for extreme ratios, and an
  independently checked exact-target snap that retains the unsnapped feasible
  result when exact scale is infeasible.
- Added generated box, cube, faceted-cylinder, tetrahedron, and rod fixtures.
  Geometry oracles cover obvious fits, separated copies, an oversized non-fit,
  and a known rotation-only fit. End-to-end tests cover rotation-enabled
  two-object growth, full-scale cylinder/tetrahedron barriers, and a
  rotation-required rod without external STL fixtures.
- Re-ran the exact supplied meshes. The `0.1 -> 0.1001` rotation-enabled run
  completed two solves in 78 aggregate Ipopt iterations. The
  `0.999 -> 1.0` rotation-disabled run completed two solves in 14 iterations
  after one bounded TetGen recovery. Both wrote exact requested scales and passed
  physical validation.
- Recorded ADR-0012, retired COMPAT-0001/0006, synchronized DEV-0022/0023, and
  added focused practical-test commands to the README.
- Visual Studio Debug, Visual Studio Release, and the Ninja clang-tidy analysis
  build each passed 163/163 tests.

## Verification Evidence

- Toolchain: CMake 4.2.3; Visual Studio Community 2026 18.8.3; MSVC 19.51.36252;
  Windows SDK 10.0.26100.0; default vcpkg baseline
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6`; GL2PS-only registry baseline
  `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`; clang-format/clang-tidy 22.1.3.
- Configure: `cmake --preset windows-vs2026 -B
  build/windows-vs2026-scoped-gl2ps` succeeded, selected GL2PS `1.4.2#5`, restored
  the patchless TetGen `1.6.0` overlay, installed exact Ipopt `3.14.19`, and used
  no temporary dependency overlay.
- Ipopt overlay: a cache-bypassed isolated `vcpkg install
  coin-or-ipopt:x64-windows --classic --enforce-port-checks --no-binarycaching`
  using only the repository overlay passed with ABI
  `37966ff18a9022a7b0575a2e77f52041276a4744a2c9485390900469a893ea07`.
- Debug: `cmake --build build/windows-vs2026-scoped-gl2ps --config Debug`
  succeeded; `ctest --test-dir build/windows-vs2026-scoped-gl2ps -C Debug
  --output-on-failure` passed 154/154 tests.
- Release: `cmake --build build/windows-vs2026-scoped-gl2ps --config Release`
  succeeded; `ctest --test-dir build/windows-vs2026-scoped-gl2ps -C Release
  --output-on-failure` passed 154/154 tests.
- Formatting: `cmake --build build/windows-vs2026-scoped-gl2ps --config Debug
  --target irop-format-check` passed with the checked-in profile.
- Analysis: the `windows-ninja-analysis` configuration at
  `build/windows-ninja-analysis-scoped-gl2ps-vcpkg-root` generated build rules
  invoking clang-tidy 22.1.3, built successfully in the Visual Studio developer
  environment, and passed 154/154 tests.
- Milestone 6 Visual Studio: `cmake --build build/windows-vs2026 --config
  Debug` and `--config Release` succeeded; both matching CTest runs passed
  163/163 tests.
- Milestone 6 analysis: the Ninja clang-tidy configuration rebuilt all changed
  production and test sources and passed 163/163 tests.
- Milestone 6 exact inputs: `build/manual-m6-rotation-final-20260824` succeeded
  with two exact `0.1001` scales, two solves, 78 aggregate Ipopt iterations,
  and physical validity. `build/manual-m6-fullscale-final-20260824` succeeded
  with two exact `1.0` scales, one TetGen recovery, two solves, 14 iterations,
  and physical validity.
- Focused numeric/geometry coverage includes non-commuting and gimbal-lock
  composition, near-tight exact snap, default/small/below-ULP tolerance policy,
  extreme-ratio saturation, obvious primitive fits/non-fits, full-scale
  cylinder/tetrahedron packing, and rotation-required geometry/packing.

- Milestone 5 JSON contracts: emitted success, resource-failure, and placements
  documents passed PowerShell `Test-Json` against the checked-in schemas; a
  mutated success category with null artifacts was rejected.
- Focused Milestone 3: `ctest --test-dir build/windows-vs2026-scoped-gl2ps -C
  Debug -R "TetGen|tetrahedral|CAT|diagnostic writers" --output-on-failure`
  passed 22/22 tests.
- Milestone 4 derivative benchmark:
  `irop_tests.exe "[.m4-derivative-benchmark]"` in Release measured 150.222
  microseconds analytic versus 1.32158 milliseconds forward difference for 2,048
  constraints across 100 samples.
- Application-local deployment contains the validated six-DLL Release and seven-DLL
  Debug Ipopt/MUMPS recursive runtime closures documented in ADR-0010.
- The Release executable embeds `activeCodePage=UTF-8` and `longPathAware=true`; the
  Unicode-path CLI smoke test passed in all three verified test configurations.

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
- TetGen 1.6.0 makes any conveyed combined build and any hosted-service release subject
  to the ADR-0009 AGPL-specific source, build-material, and notice review. Private or
  static linkage does not remove that gate.
- The Python-compatible `O0/0Q` switch string tetrahedralizes the participant point
  union rather than enabling PLC/CDT mode. Changing this after parity may materially
  change CAT constraints and requires comparative packing evidence.
- Exact co-spherical/coplanar point sets in very regular generated primitives can
  make point-union TetGen emit a zero-volume tetrahedron. End-to-end toy fixtures
  use small deterministic geometric perturbations and production retains bounded
  recovery; the current recovery warning does not expose the backend root cause.
- Project limits bound TetGen inputs and accepted outputs but cannot strictly cap the
  backend's peak memory or time. Calls are serialized because TetGen's exact-predicate
  implementation uses mutable process-global state.
- Tetrahedralization accepts one connected closed component per participant; nested
  cavities or multi-solid participant semantics remain undefined.
- The exact official Ipopt archive includes Intel-linked components and is approved
  for local development, not public binary distribution. Clear ADR-0010's
  release-specific component notice, source-availability, binary-term, and TetGen
  AGPL review before publishing a combined executable, installer, container, or
  binary archive; prefer a source-built MUMPS/OpenBLAS configuration for that path.
- Project-controlled elapsed checks run before, between, and after callback chunks,
  but cannot hard-preempt an active MUMPS factorization or strictly cap its peak
  internal allocation. Future parallel local solves also require a measured
  concurrency audit because the selected MUMPS-backed build may serialize internally;
  each concurrent call must use a distinct `LocalSolveWorkspace`.
- Adaptive sampling intentionally preserves the Python target-count formulas and can
  refine coarse toy containers aggressively. Use `--no-adaptive-sampling` for
  full-resolution diagnostic runs; Milestone 7 may optimize only measured workloads.
- Packing cancellation and elapsed checks are cooperative between input,
  initialization, engine-stage, and successful artifact boundaries. An
  already-final unsuccessful engine outcome wins over a later interruption.
  Active VTK reads and remeshing, TetGen, MUMPS, collision queries, and
  individual file-write calls are not hard-preemptible.
- Remaining Milestone 6 work is to build representative Python/C++ parity
  corpora, expand hostile-input and process-level interruption coverage, add hosted
  CI/release checks, and resolve the exact public binary licensing path before any
  distribution.
