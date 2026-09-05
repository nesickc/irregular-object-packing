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
3. Choose a new output folder. The browse button selects a parent and proposes
   a new child folder. Existing run folders are never overwritten.
4. Select **Run packing**. Progress reports the current barrier, iteration and
   objects at the target. The saved outcome, diagnostics and artifact location
   appear when the run ends. Successful runs publish the same STL/JSON artifacts
   as `irop pack`; unsuccessful engine runs publish only their summary.

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
and a malformed saved summary. Use a fresh output directory for each run:

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
