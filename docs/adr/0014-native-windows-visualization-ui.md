# ADR-0014: Native Windows Visualization UI

- Status: Accepted
- Date: 2026-09-05
- Deciders: Project maintainers

## Context

The CLI and reusable packing service now produce bounded, inspectable results,
with Milestone 7 verified for its first measured scope. The maintainer has
requested Milestone 8: a basic local application for selecting inputs, configuring
and running packing, and inspecting saved geometry and diagnostics. Milestone 6's
first hosted CI run remains an independent pending acceptance item.

The original first-release boundary intentionally excluded a UI. This decision
activates the later visualization milestone without changing the packing algorithm,
its deterministic ownership, or the CLI's existing artifact contract. The pinned
VTK installation already provides the rendering modules needed for a Windows
viewport; another UI framework or vcpkg dependency is unnecessary for this scope.

## Decision

### Application and dependency boundary

- Add a native Windows C++20 application named `irop_studio`, enabled explicitly
  through `IROP_BUILD_UI=ON`. The normal CLI/core build remains usable with that
  option off.
- Use Win32 controls and native file/directory selection for the application
  shell. Link `irop_core` directly for packing and project-owned scene data.
- Link the existing VTK `RenderingCore`, `RenderingOpenGL2`, `RenderingUI`,
  `InteractionStyle`, and `IOImage` modules privately to the UI target. Keep VTK
  actors, mappers, render windows and interactors inside the application; expose
  no VTK or Win32 types from core APIs. Add no new vcpkg dependency.
- Keep all VTK rendering, interaction and camera ownership on the UI thread.
  Object/container preview and loaded results share the same bounded viewport
  presentation path. Preserve the existing domain coordinate and volume-scale
  conventions; the display performs no implicit unit conversion.

### User workflow

- Select an object STL and container STL and preview their geometry before a run.
- Configure object count, initial/final volume scale, scale steps, seed, elapsed
  timeout and adaptive sampling. Select a new output directory; the existing
  no-overwrite and atomic publication contract remains authoritative.
- Start one background `pack_scene` invocation at a time. Report current progress,
  outcome, diagnostics and output location, and provide cooperative cancellation.
  Do not duplicate initialization, solving, collision or publication policy in
  the UI.
- Provide orbit, pan and zoom, fit-to-scene and axis camera views, container
  visibility and object wireframe controls. Resizing must preserve a usable
  viewport and controls.
- Open supported version-one `pack` and `initialize` run summaries. Show their
  local published geometry on successful runs and their recorded outcome and
  diagnostics on unsuccessful runs. A failed or summary-only run must not be
  represented by stale geometry from a previously opened successful run.
- Label any saved physical-validation state as recorded validation. Loading a
  saved result validates its supported structure and bounded local artifacts; it
  does not rerun the packing solver or certify the scene's physical feasibility.

### Worker lifetime and cancellation

- The worker owns its packing inputs/configuration and result. Transfer
  project-owned progress/result data to the UI thread; worker callbacks must not
  render or mutate VTK objects or controls.
- Keep the window event loop responsive while packing. Prevent overlapping jobs
  and retain worker state until its terminal outcome is received.
- Closing during a run requests cancellation and defers window destruction until
  the worker terminates and is joined. Continue servicing the event loop while
  waiting; do not detach or forcibly terminate a dependency call.
- Cancellation remains cooperative under ADR-0011. Active VTK/TetGen/MUMPS,
  collision and filesystem operations cannot be hard-preempted. Preserve the
  existing pre-engine no-output and post-engine summary-only cancellation rules,
  including the authority of an already-final unsuccessful result.

### Saved-run trust boundary

- Provide a bounded project-owned `load_run_scene` core operation. Accept the
  supported version-one command/outcome forms, enforce summary and mesh resource
  limits, and reject malformed, unsupported or contradictory records with
  actionable project-owned errors.
- Resolve geometry only from the selected run directory's fixed local artifact
  leaves: `packed-objects.stl` or `initialized-objects.stl`, plus `container.stl`,
  as appropriate to the supported successful summary. Canonical path checks must
  prevent traversal and symlink/junction escape before opening an artifact.
- Never load original input paths recorded in a summary or follow arbitrary
  input-derived artifact destinations. Historical initialization summaries may
  contain absolute publication paths; those strings cannot redirect loading
  away from the selected local run directory.
- Unsuccessful packing summaries need no geometry and display recorded
  diagnostics only. Loading is read-only and must not change the source run,
  reconstruct missing artifacts from old inputs, or launch external commands.

## Consequences

The application adds a native local workflow while the CLI and core algorithm
remain the behavioral authority. Existing dependencies suffice, and the rendering
boundary does not introduce a UI event loop into `irop_core`. This application is
Windows-only; cross-platform UI portability is outside the milestone.

The worker improves responsiveness, not packing parallelism. Solver order, random
state, budgets and dependency limitations remain unchanged. Window closure can be
delayed until an active non-preemptible operation reaches a cancellation boundary.
Opening a copied run directory uses its own published artifacts and does not depend
on the original input locations.

This decision adds an application and a saved-artifact reading contract, without
changing Python/C++ packing behavior. No new compatibility ID is required unless
implementation discovers an actual behavioral deviation. Public binary and
hosted-service distribution remain subject to ADR-0009/0010 release review;
implementing the UI does not approve distribution.

## Tranche 1 extension: automatic run destinations

The maintainer activated this extension on 2026-09-05. The UI selects a remembered
Runs parent, defaulting through Windows `FOLDERID_LocalAppData` to `IROP/Runs`,
and allocates a new `run-000001`-style child on every Run. This application policy
does not change the CLI's explicit exact-output path or ADR-0011 publication.
The proposed next name is advisory; allocation always checks again. The result
panel and Open result folder action retain the actual published/opened location.

The application-private allocator holds an exclusive sibling reservation file,
created with `CREATE_NEW` and `FILE_FLAG_DELETE_ON_CLOSE`; it never precreates the
final output directory. The same OS handle removes only its own reservation on
normal completion, cancellation, or process termination. The canonical parent is
held without delete sharing for the attempt. Numbered files, directories, links
and abandoned reservation entries are skipped; existing artifacts are untouched.
A per-parent sequence file is opened without sharing during allocation, and a
fixed-width sequence record is appended and flushed before the attempt starts.
This durable ledger consumes numbers for pre-publication errors/cancellation even
after restart; gaps are intentional. Partial/invalid ledger records fail closed.

Scanning is bounded to 100,000 directory entries, allocation to 100 candidates,
sequence-lock contention to 100 five-millisecond waits, the ledger to 100,000
records (1 MB), and run numbers to 999,999,999. Sequence/settings file handles
reject reparse points, directories and multiply linked files. Known-unusable
parents are rejected before input preparation. The core's final atomic no-overwrite
publication remains authoritative if the destination becomes unavailable later.

The remembered parent uses a versioned bounded UTF-16 settings record (64 KiB)
under `LocalAppData/IROP/Studio`; a unique temporary file and atomic replacement
persist a complete path. Concurrent preference changes are last-writer-wins.
Malformed settings fall back to the stable default with a visible warning.
Persistence is best effort: selecting a valid parent activates it for the current
session even if saving preferences fails. The warning remains visible beside the
next name; Run uses the active parent without rewriting settings. Unsafe linked
or directory metadata is preserved, so failed persistence cannot block otherwise
valid packing or silently replace those entries.
`--settings-dir` injects an isolated settings/default-runs root; desktop smoke
always supplies a directory under its workspace, never the real user's settings.

A Capture solver diagnostics checkbox opts into the core's bounded snapshot/trace
support (64 local-solve records, 128 trace records per solve). Input preparation,
initialization attempts and per-object local limits are identified in progress,
separately from the overall engine time budget. Collection does not change the
packing algorithm or solver settings.
## Alternatives Considered

### Add a web frontend or another desktop framework

Deferred because the requested Windows-only controls and viewport can use Win32
and already installed VTK modules. Another dependency or local service would add
build and lifecycle work without being necessary for this first UI.

### Invoke the CLI as the UI's packing engine

Rejected for this scope because the existing `pack_scene` service already exposes
project-owned progress, cancellation and results. Direct library use avoids a
second process/protocol boundary and keeps policy in one implementation.

### Run packing on the UI thread

Rejected because input preparation, tetrahedralization and solving can take long
enough to prevent repainting and cancellation interaction.

### Reopen recorded original source paths

Rejected because moved runs should remain inspectable and a saved summary is
untrusted data. Only bounded published artifacts within its selected directory
belong to the read-only visualization operation.

## Verification

Milestone 8 is Verified on 2026-09-05; concrete evidence is recorded in
[STATUS.md](../STATUS.md#milestone-8-implementation-and-evidence). Debug, Release
and Ninja matrices each complete 203 tests with 202 passed, one skipped and no
failures, and clang-tidy/format gates pass. Real Debug/Release desktop smoke,
native interaction events, inspected 36-object rendering, unsuccessful saved
diagnostics and a fresh option-off CLI build pass. The file-symlink escape test
is skipped because this account cannot create file symlinks. Manual file-picker
dialog interaction was not automated after the Computer Use runtime failed to
start; actual native desktop events and inspected captures provide the UI
evidence. These limits are retained explicitly; hosted CI remains a separate
Milestone 6 gate. The acceptance contract is:

- Loader tests for successful pack/initialize runs, unsuccessful diagnostics,
  malformed/unsupported/contradictory summaries, resource exhaustion, missing
  artifacts, path traversal and canonical escape, and ignored original paths.
- Supported-preset Debug/Release builds with `IROP_BUILD_UI=ON`, focused loader
  tests and the broader affected suite, checked-in formatting, and a clean Ninja
  clang-tidy build/test matrix.
- A real Windows UI exercise of input preview, camera controls, a completed
  packing run, reopening its saved result, unsuccessful-summary display,
  cancellation, resize and closing during active work without invalid lifetime
  access or misleading output.
- Evidence that saved validation is labeled as recorded state and that UI/VTK
  types remain private to the application. No verification claim follows from
  compilation alone.
