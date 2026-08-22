# Python Compatibility and C++ Deviations

This catalog records questionable Python behaviors that the C++ port intentionally preserves and defects it deliberately corrects. It prevents future maintainers from “cleaning up” compatibility code without understanding why it exists.

The Python implementation remains the reference until the parity milestone is complete.

## Policy

Preserve behavior observed during successful Python runs. Correct a defect when the correction is evident and no meaningful downstream behavior is expected to rely on the defect. When impact is uncertain, preserve the behavior first and review it after parity evidence exists.

Every cataloged behavior must use one of these source markers:

```cpp
// COMPATIBILITY(IROP-COMPAT-NNNN):
// Intentionally preserves questionable Python behavior.
// See docs/COMPATIBILITY.md.
```

```cpp
// DEVIATION(IROP-DEV-NNNN):
// Corrects a Python defect with no expected downstream dependency.
// See docs/COMPATIBILITY.md.
```

`COMPATIBILITY` means C++ intentionally mirrors the questionable behavior. `DEVIATION` means C++ intentionally differs. IDs are permanent and must not be reused.

## Initial Catalog

| ID | Kind | Python behavior | Planned C++ handling | Rationale | Target | Status |
| --- | --- | --- | --- | --- | --- | --- |
| IROP-COMPAT-0001 | Preserved | The current scale barrier is passed as `max_scale`, but the local NLP receives no finite upper scale bound. The returned scale is multiplied into the previous scale and clamped after solving. | Preserve the effective solve-then-clamp behavior for parity, behind a named compatibility path. | Enforcing the barrier inside the solve can change optimized rotations and translations, so downstream impact is plausible. | Milestone 4 | Planned |
| IROP-COMPAT-0002 | Preserved | CAT collisions are reported, but only object-object and object-container collisions contribute object IDs to scale correction. | Preserve initially, expose CAT violations in the run result, and mark the correction-selection code. | Adding CAT violations to correction can materially change convergence and final placements. | Milestone 5 | Planned |
| IROP-COMPAT-0003 | Preserved | `PyVista.center_of_mass()` without scalar weights centers the source at its point/vertex centroid rather than its enclosed-volume centroid. | Center at the vertex centroid and persist the original pivot in `placements.json`. | Changing the pivot changes every serialized translation and rotation result. | Milestone 2 | Verified |
| IROP-COMPAT-0004 | Preserved | When the expected object count is one, setup emits volume scale plus zero rotation/translation without consuming random draws. | Preserve the origin transform when the actual transformed surface is strictly contained; use the bounded sampler only when it is invalid. | The successful shortcut is observable in transforms and RNG state. A bounding-sphere-only test would incorrectly randomize slender objects that fit. | Milestone 2 | Verified |
| IROP-DEV-0001 | Corrected | `reduce_all_scales()` iterates over the integer object count, causing a failure on the tetrahedralization recovery path. | Iterate over valid object indices and test the recovery path. | The current behavior is an execution defect rather than a meaningful result. | Milestone 5 | Planned |
| IROP-DEV-0002 | Corrected | Optimizer history storage and its index are class-level mutable fields, allowing state to leak between optimizer instances. | Store configuration, random state, transforms, and future history per packing-engine instance. | Cross-run state leakage is unintended and unsafe; isolated runs are the evident intent. | Milestone 2 | Implemented |
| IROP-DEV-0003 | Corrected | `SimConfig.new_cat` has a trailing comma and therefore defaults to a one-element tuple instead of a Boolean. | Omit the unused option from the project-owned configuration after reference confirmation. | The tuple is defective and the live initialization path does not consume the option. | Milestone 2 | Verified |
| IROP-DEV-0004 | Corrected | Collision correction repeats until no selected violation remains, without an iteration or time bound. | Add configurable correction and time limits. Return a structured unsuccessful outcome when exhausted. | Preventing nontermination is required for a safe local tool and does not alter successful runs that converge within the limit. | Milestone 5 | Planned |
| IROP-DEV-0005 | Corrected | The optional grid-spacing objective returns the negative absolute packing-volume error and is minimized, which rewards larger error; the annotated scalar function also returns the optimizer's array. | Omit the dead grid initializer and port the live uniform-rejection path. | The defective grid path is explicitly unused and has no successful live-run behavior to preserve. | Milestone 2 | Verified |
| IROP-DEV-0006 | Corrected | Initialization seeds and mutates NumPy's process-global legacy MT19937 state. | Give each run a local MT19937 stream with NumPy-compatible 53-bit uniform mapping and persisted seed/draw count. | Independent runs must not perturb each other; the mapping preserves seeded successful results. | Milestone 2 | Verified |
| IROP-DEV-0007 | Corrected | The primary coordinate rejection loop has no intrinsic attempt bound; a timeout wrapper is used only for a size heuristic. | Enforce configurable candidate, geometry-query, pairwise-distance, and exact-surface work limits. | Bounded work prevents nontermination and algorithmic denial of service without changing successful runs inside the limits. | Milestone 2 | Verified |
| IROP-DEV-0008 | Corrected | Initial-state validation checks surface contacts and can miss wholly outside or nested nonintersecting objects. | Require the generated bounding sphere to be strictly inside; for the compatible one-object origin shortcut, require strict vertex containment plus no object/container surface intersection. | A success scene must not place geometry outside the container. | Milestone 2 | Verified |
| IROP-DEV-0009 | Corrected | The one-object shortcut emits the origin transform even when it is invalid and then fails setup. | Fall back to the normal bounded sampler when the actual origin placement is not contained. | A valid alternative placement is preferable to a deterministic invalid scene. | Milestone 2 | Verified |
| IROP-DEV-0010 | Corrected | Adaptive sampling truncation can request fewer than four triangles. | Retain at least four triangles, the minimum for a closed triangular volume. | Smaller targets cannot represent a safe closed surface. | Milestone 2 | Verified |
| IROP-DEV-0011 | Corrected | Manifold meshes with locally inconsistent triangle winding pass the Python gate, but signed proximity can depend on unreliable face normals. | Normalize winding in the geometry query's owned snapshot and leave the caller mesh unchanged. | Deterministic orientation makes volume and containment queries reliable without narrowing successful single-surface inputs. | Milestone 2 | Verified |
| IROP-DEV-0012 | Corrected | Python accepts multi-component closed surfaces without distinguishing disjoint solids from nested cavity shells. | Reject multiple closed surface components until cavity semantics are explicitly modeled. | Treating every component as positive volume can classify a cavity as usable packing space; silent mispacking is unsafe. | Milestone 2 | Verified |

## Scale Semantics

The transform array stores a volume scale. Mesh transformation applies the cube root as the linear scale. This is intentional algorithm behavior, not a deviation. Name C++ fields accordingly so it is not mistaken for a linear scale.

## Adding an Entry

Before adding or changing compatibility behavior:

1. Identify the exact Python source and triggering conditions.
2. State whether the behavior occurs on successful runs or only failure paths.
3. Assess possible downstream effects on transforms, convergence, output geometry, and persisted state.
4. Prefer preservation when effects are uncertain.
5. Assign the next permanent ID.
6. Add the code marker and focused regression test.
7. Update the entry status and cite verification in `docs/STATUS.md`.

## Status Values

- `Planned`: documented before C++ implementation.
- `Implemented`: the C++ behavior and code marker exist.
- `Verified`: focused tests and, when relevant, Python/C++ comparison evidence pass.
- `Retired`: behavior was intentionally removed through a later decision; retain the historical entry and link its ADR.
