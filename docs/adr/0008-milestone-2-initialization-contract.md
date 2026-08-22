# ADR-0008: Milestone 2 Initialization State and Artifact Contract

- Status: Accepted
- Date: 2026-08-22
- Deciders: Project maintainers

## Context

Milestone 2 introduces the first reusable packing state, compatibility-sensitive
transform and random semantics, geometry containment queries, and a multi-file
initialization result. These choices must remain stable when tetrahedralization and
optimization are added. The initialization path also needs bounded work and an output
publication model that cannot leave a success record describing a partial artifact
set.

## Decision

Add a thin `irop initialize` command over project-owned initialization modules. Its
required inputs are one object STL, one container STL, an explicit object count, and a
new output-directory path. `PackingConfig`, `PackingState`, and `Transform` remain free
of VTK and Eigen types.

Preserve these numerical semantics:

- `Transform::volume_scale` is converted to linear scale with `cbrt`.
- Column vectors use rotation order `Ry * Rz * Rx`, followed by translation.
- Input meshes are centered at the unweighted vertex centroid.
- Each run owns a legacy-MT19937-compatible random stream. It draws all accepted and
  rejected candidate coordinates before drawing accepted-object Euler rotations.
- General initialization samples uniformly from the container AABB and uses strict
  bounding-sphere boundary clearance and center spacing.
- The valid one-object Python origin/zero-rotation shortcut is preserved without RNG
  draws. When its bounding sphere is too conservative, strict transformed-vertex
  containment plus a bounded VTK triangle-surface intersection query decides whether
  the actual placement fits. An invalid origin uses the normal bounded sampler.

Candidate attempts, geometry triangle visits, center-distance checks, and exact
surface triangle-pair tests have configurable positive limits. Exhaustion reports the
resource-limit process category and publishes no success artifact set. The
initialization-only command does not create a partial failure `run-summary.json`;
structured unsuccessful packing outcomes remain part of the later packing-result
contract.

The Milestone 2 sampling module owns the Python adaptive ratio and target-triangle
count policy. It performs binary64 multiplication followed by truncation and retains a
safe minimum of four triangles. Actual decimation, subdivision, smoothing, and
iteration-time remeshing are deferred until the packing loop consumes this policy.

Closed geometry queries own a validated mesh snapshot, normalize locally inconsistent
winding, and currently require exactly one connected closed surface component.
Multi-component cavity/disjoint-solid semantics must be designed before that input
domain is accepted.

A successful initialization atomically publishes a new directory containing:

- `initialized-objects.stl`;
- `container.stl`;
- canonical `placements.json` conforming to `placements-v1.schema.json`;
- success `run-summary.json` conforming to
  `initialization-run-summary-v1.schema.json`; and
- optional `objects/object-NNNNNN.stl` files.

All files are prepared in a private sibling staging directory and the directory is
renamed to the requested, previously absent output path only after the success summary
is complete. The run summary records resolved inputs, configuration and limits, seed,
work metrics, warnings, dependency versions, initialization time, and pre-summary
artifact-preparation time. `placements.json` stores matrices from original input
coordinates to world coordinates; placement fields themselves act on the centered
template.

## Consequences

- Seeded successful initialization is directly comparable with the Python reference
  while independent C++ runs do not share mutable RNG state.
- Conservative general placement remains simple, and the one compatibility exception
  pays for an exact surface query only when needed.
- Work is deterministically bounded, although the current linear geometry queries are
  not intended as the final performance design.
- Output paths are artifact-set identities rather than reusable directories. Callers
  must choose a new directory for each run.
- Multi-component meshes are rejected until cavities and disjoint solids have an
  explicit domain model.
- A full structured failure result is deferred; CLI diagnostics and stable exit
  categories are the Milestone 2 failure contract.

## Alternatives Considered

### Use a process-global standard-library distribution

Rejected because standard distributions do not guarantee NumPy's legacy mapping and a
global stream permits cross-run interference.

### Require the bounding sphere for the one-object shortcut

Rejected because it changes successful Python behavior for slender objects whose
actual surface fits at the origin.

### Publish artifacts one at a time in the final directory

Rejected because allocation, write, or replacement failures can leave a partial set or
a success summary describing different geometry.

### Treat every closed component as a positive-volume solid

Rejected because a nested cavity shell would become usable packing volume.

## Verification

- Transform, sampling-policy, RNG-golden, initialization, winding, containment,
  work-limit, schema, matrix-to-STL, and atomic no-publication tests pass.
- The initialization CLI smoke test exercises success plus usage, input, resource, and
  output exit categories.
- Visual Studio Debug and Release builds and the Ninja clang-tidy build pass the full
  affected CTest suite with the checked-in format gate.

## Amendments

None.
