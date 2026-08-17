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
| IROP-DEV-0001 | Corrected | `reduce_all_scales()` iterates over the integer object count, causing a failure on the tetrahedralization recovery path. | Iterate over valid object indices and test the recovery path. | The current behavior is an execution defect rather than a meaningful result. | Milestone 5 | Planned |
| IROP-DEV-0002 | Corrected | Optimizer history storage and its index are class-level mutable fields, allowing state to leak between optimizer instances. | Store history and its index per packing-engine instance. | Cross-run state leakage is unintended and unsafe; isolated runs are the evident intent. | Milestone 2 | Planned |
| IROP-DEV-0003 | Corrected | `SimConfig.new_cat` has a trailing comma and therefore defaults to a one-element tuple instead of a Boolean. | Model the option as a Boolean if it remains necessary; otherwise omit dead configuration after reference confirmation. | The annotation and surrounding configuration establish the intended type. | Milestone 2 | Planned |
| IROP-DEV-0004 | Corrected | Collision correction repeats until no selected violation remains, without an iteration or time bound. | Add configurable correction and time limits. Return a structured unsuccessful outcome when exhausted. | Preventing nontermination is required for a safe local tool and does not alter successful runs that converge within the limit. | Milestone 5 | Planned |
| IROP-DEV-0005 | Corrected | The optional grid-spacing objective returns the negative absolute packing-volume error and is minimized, which rewards larger error; the annotated scalar function also returns the optimizer's array. | If grid initialization is ported, minimize the positive absolute error and return a validated scalar spacing. | The function name, target-volume calculation, and caller establish that distance from the target should be minimized. This path is not used by the primary `initialize_state()` flow. | Milestone 2 | Planned |

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
