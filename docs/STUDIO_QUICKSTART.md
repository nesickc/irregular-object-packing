# IROP Studio quickstart

IROP Studio is the optional Windows application for previewing an object and
container, running packing, and inspecting saved results. It uses the same
`irop_core` implementation and artifact contract as the CLI.

## Build and launch

Use the Windows toolchain and pinned dependencies described in
[Windows C++ quickstart](WINDOWS_CPP_QUICKSTART.md). From the repository root,
with `VCPKG_ROOT` configured:

```powershell
cmake --preset windows-vs2026 -DIROP_BUILD_UI=ON
cmake --build --preset windows-vs2026-release
& ./build/windows-vs2026/app/irop_studio/Release/irop_studio.exe
```

The UI is off by default. `-DIROP_BUILD_UI=OFF` retains the normal CLI/core build.
No additional vcpkg dependency is required; Studio uses Win32 controls and the
existing VTK rendering modules. A Windows desktop and working OpenGL graphics
driver are required for the viewport.

Startup arguments can prefill the two mesh fields or open an existing result:

```powershell
& ./build/windows-vs2026/app/irop_studio/Release/irop_studio.exe `
  --object ./tests/fixtures/tetra_ascii.stl `
  --container ./tests/fixtures/cube_ascii.stl

& ./build/windows-vs2026/app/irop_studio/Release/irop_studio.exe `
  --open ./runs/example/run-summary.json
```

`--object` and `--container` prefill the form; select **Preview inputs** or
**Run packing** to proceed. `--open` also accepts a run directory containing
`run-summary.json`.

## Preview and run

1. Choose the source object and container STL files. **Preview inputs** shows
   one source copy centered at the origin and scaled to the initial volume scale,
   together with the container. A preview is not an initialized or packed scene.
2. Set copies, random seed, initial/target volume scale, scale steps and the time
   limit. Toggle adaptive sampling and structured initialization fallback as
   needed. Volume scale is the volume ratio: `0.125` gives half the linear size.
   All meshes must use consistent coordinate units; Studio performs no conversion.
3. Choose a **Runs folder** once, or keep the default
   `%LOCALAPPDATA%\IROP\Runs`. Studio remembers this parent across launches and
   shows the next child name: `run-000001`, `run-000002`, etc. Every Run allocates
   a fresh child automatically. Existing run folders are never overwritten.
4. Select **Run packing**. Progress distinguishes input preparation, bounded
   initialization, the current barrier/iteration and the current object solve.
   Local iteration/time limits are shown separately from the engine time budget.
   The saved outcome, diagnostics and artifact location
   appear when the run ends. Successful runs publish the same STL/JSON artifacts
   as `irop pack`; unsuccessful engine runs publish their summary without packed
   geometry. An opted-in captured failure can also publish `failed-local-solve.json`.

The **Next run** line updates as you edit the scale fields. Equal initial and
target scales mean **direct placement at that volume scale**, with no growth
solves; this is full-size placement only when both values are `1.0`. A larger
target means **growth from the initial scale to the target**. For example,
`0.1 -> 1.0` asks the packing engine to grow small initialized copies to full size.
This label describes the requested run, not a promise that its search will succeed.

Saved packing results show **Recorded direct placement** or **Recorded growth**
from the saved scales, separately from the current Next run settings. The outcome
and recorded physical validation still determine whether the requested packing
completed. Initialization-only summaries remain labeled **Initialized scene**.

Select **Run packing** again without editing any output path. A cancelled or
failed attempt also consumes its number, even if it fails before creating an
artifact directory; gaps are expected. Two Studio instances using the same Runs
folder receive different names. **Open result folder** opens the latest published
or opened result's actual location, separately from the next-run destination.

Enable **Capture solver diagnostics** before a diagnostic run to retain a bounded
failing local-problem snapshot (`failed-local-solve.json`) and solver trace for replay. Studio requests up to
64 local-solve records and 128 trace records per solve. This is off by default;
packing settings and solver behavior are unchanged by collection.

The remembered parent uses bounded per-user settings in
`%LOCALAPPDATA%\IROP\Studio`. An optional `--settings-dir ABSOLUTE_DIRECTORY`
overrides this location and puts the default Runs folder inside that directory;
this is useful for isolated test sessions. Malformed settings fall back to the
default with a visible message. Unavailable preference storage does not block
packing: a newly selected Runs folder remains active for the current session,
with a settings warning until it can be remembered. Run uses the active parent
directly and does not rewrite preferences. Unsafe settings links/directories are
left untouched. Automatic numbering stores a small private
`.irop-run-sequence` ledger beside results; retain it to preserve numbers consumed
by unpublished attempts. Allocation scans at most 100,000 entries, retries at most
100 occupied candidates, and accepts up to 100,000 recorded attempts per Runs
folder. If a limit is reached or the ledger is damaged, choose another Runs folder.

Only one preview, load or packing job runs at a time. Geometry preparation and
packing run on one worker; rendering and controls remain on the window thread.
This keeps the window responsive and does not parallelize the packing algorithm.

**Cancel** requests cooperative cancellation. Closing an active window requests
cancellation and waits for the worker before destroying the viewport. An active
VTK, TetGen, solver, collision or file operation can delay completion until it
returns. The time-limit setting bounds engine elapsed work; initialization has
its own bounded candidate and geometry budgets. Pre-engine cancellation creates
no run directory. Engine cancellation can publish a summary-only cancelled run;
an outcome already committed before a late cancellation remains authoritative.

## A verified growth run for the supplied meshes

For `rc/input_models/ulamok_2kg_simplified.stl` inside
`rc/containers/10_kg_np.stl`, these settings completed genuine growth with the
updated packing engine on 2026-09-06:

| Setting | Value |
| --- | --- |
| Copies / seed | 10 / 1918 |
| Initial / target volume scale | 0.1 / 1.0 |
| Scale steps / time limit | 9 / 300 seconds |
| Adaptive mesh sampling | On |
| Structured initialization fallback | Off |

Choose a Runs folder and select **Run packing**. The verified run grew all ten
objects to exactly `1.0`, occupied 16.97% of the container volume, and passed both
full-resolution physical validation and validation of the float32 coordinates
written to the output STL files. Engine time was 265.8 seconds in the initial
acceptance run; three serial benchmark repetitions took 216.3-227.3 seconds total
on the baseline Windows machine; runtime depends on the machine and inputs. The retained result is
`build/tranche2-real10-step-retry-experiment/run-summary.json`; see
[implementation status](STATUS.md) for the complete evidence and current limits.

Current builds start each local solve from the existing placement and use exact
derivatives to help the solver converge. They automatically refine coarse object
samples or retry smaller growth steps when needed, while preserving the original
container boundary. Recovery shares the same 300-second engine budget, and a
successful result still requires final physical and output validation.

Direct full-size placement remains an alternative for these meshes: use initial
and target scales `1.0 / 1.0`, one scale step, adaptive sampling off and structured
initialization fallback on. With the same ten copies and seed, this previously
verified mode places full-size objects without growth solves. The **Next run**
line makes this distinction visible before starting.

The earlier `0.1 -> 1.0` run stopped at an Ipopt iteration limit before reaching
the first barrier. Its displayed 2.64% described a partial state, and **Recorded
physical validation: not recorded** meant final validation had not run. That
summary remains useful diagnostic evidence; it does not describe the updated
default behavior. The CLI's optional `--reference-growth-policy` switch retains
the earlier growth policy for comparisons and reproducing failures. Complete
historical runtime comparisons also require `--solver-openmp-threads 0`.

The current packing library requests one numerical OpenMP thread per local solve
and retains unchanged solver results during immediate physical retries. Studio
uses these defaults automatically. Each retried candidate still receives full
physical checks; final exported geometry has its own validation. These performance
changes do not change the scale settings or establish that arbitrary inputs will
fit. The summary records resolved threading, ambient MKL overrides and avoided
retry work.

## Inspect geometry and saved runs

- Drag to orbit; Shift + drag to pan; scroll to zoom.
- **Fit** frames the scene; **3D**, **X**, **Y** and **Z** select camera views.
- **Container** toggles container visibility; **Wireframe** changes the objects'
  surface presentation. Resize the window to change viewport space.
- **Open saved run...** opens version-one `pack` and `initialize` summaries.
  Combined meshes can contain many disconnected object copies. Failed runs show
  diagnostics with an empty viewport.

Recorded physical validation comes from the saved summary. Loading checks the
supported record and mesh structure; it does not repeat collision validation or
certify a modified artifact set. Initialization summaries do not record the
packing engine's final physical-validation field.

A copied run directory remains usable without the original input files. Studio
reads the fixed object/container STL siblings beside the selected summary; it
never follows saved source paths. Historical absolute output paths in
initialization summaries are treated only as artifact names. Placements and
individual STL paths are metadata for this first viewer; object picking,
placement editing and animation are outside this milestone.

Saved-result display limits default to 16 MiB of JSON, depth 32, 250,000 JSON
values, 100,000 entries per collection, 16,384 bytes per general string and
100,000 reported objects. Diagnostics/warnings have narrower limits. The two
meshes share a 128 MiB input-byte, three-million-vertex and one-million-triangle
budget. These display limits do not change the CLI's packing limits. An oversized
run may still have valid saved artifacts even when Studio cannot display them.

The left-hand settings configure the next run. Opening a saved result leaves
those settings intact; the result panel reports the saved count, seed, metrics
and outcome. Original source paths are not restored from untrusted summaries.

## Verify the desktop workflow

Ordinary tests cover saved-result loading and worker ownership without a desktop:

```powershell
ctest --preset windows-vs2026-release
```

The separate smoke driver opens real windows and exercises preview, genuine
`.1 -> .2` growth, reopening the result, cancellation, closing during active work,
a malformed saved summary, rerunning after success/cancellation/input failure,
remembering the Runs folder and next number across a new process, and rerunning
successfully while preference storage is unusable. It also exercises full-size
and reduced-size direct placement, checking that labels update when scales change
and that successful direct placement performs no growth solves. It supplies
isolated settings directories beneath its work directory, so the smoke never
changes your normal Studio preferences. Use a fresh work directory for each run:

```powershell
cmake `
  "-DIROP_STUDIO_EXECUTABLE=build/windows-vs2026/app/irop_studio/Release/irop_studio.exe" `
  "-DIROP_OBJECT_FIXTURE=tests/fixtures/tetra_ascii.stl" `
  "-DIROP_CONTAINER_FIXTURE=tests/fixtures/cube_ascii.stl" `
  "-DIROP_WORK_DIR=build/studio-smoke-release" `
  -P tests/cpp/studio_smoke.cmake
```

The driver checks bounded process completion, recorded outcomes, output contracts
and nonempty viewport PNGs. It preserves reports/captures and refuses an existing
work directory. The cancellation cases request Cancel immediately after dispatch;
the application still preserves an outcome committed before a late request. Camera controls, visual appearance and resize behavior also need
an interactive inspection; a PNG signature check alone does not establish visual
correctness. This desktop exercise is intentionally separate from normal CTest
and headless hosted CI.

See [ADR-0014](adr/0014-native-windows-visualization-ui.md) for ownership and
saved-run contracts, and [implementation status](STATUS.md) for verification
actually completed on the current build.

## Measured larger runs

Tranche 3 verifies generated 44-face irregular objects at 100 and 300 copies in
larger containers at 5% full-size density: `.1 -> 1.0`, nine steps, seed 1918,
adaptive sampling and structured fallback off. Medians are 33.484 and 125.446 seconds,
with three successes each and actual Studio load/render checks. The thread/retry
improvements apply automatically; no new Studio setting is required.

Those results do not guarantee convergence for every STL. The supplied 768-face
mesh at 100 copies still reaches the physical collision-work limit even in a
proportionally larger container. In the original container, 100 full-size copies
would require 169.67% of its volume. Keep container size, density and mesh detail
explicit when comparing results; see [STATUS](STATUS.md) for the retained failures.
