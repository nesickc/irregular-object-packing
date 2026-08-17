# ADR-0005: STL-First Mesh I/O and Structured Run Results

- Status: Accepted
- Date: 2026-08-18
- Deciders: Project maintainers

## Context

The first CLI must produce geometry that users can inspect with common local tools. STL is the preferred model interchange format, but it cannot represent all placement metadata, run configuration, units, solver outcomes, or object identities.

## Decision

Make STL the required first-class object, container, and packed-geometry format. Keep mesh I/O behind an adapter so additional formats can be added without changing the packing engine.

Make JSON the canonical structured run record:

- `placements.json` records object transforms and identities.
- `run-summary.json` records resolved configuration, seed, timings, metrics, warnings, and outcome.

Treat coordinates as arbitrary consistent units and perform no implicit conversion.

## Consequences

- The first vertical slice and packing release produce tangible results that open in ordinary mesh viewers.
- Scene and run metadata remain recoverable despite STL limitations.
- Users must load the container and packed objects as separate geometry when inspecting containment.
- Additional formats can be added later without becoming a first-release requirement.

## Alternatives Considered

### STL as the only output

Rejected because it would lose transforms, object identity, configuration, and outcome details needed for diagnosis and reproducibility.

### VTK-family format as the required primary output

Rejected because STL is the preferred interoperability baseline. VTK-family diagnostic output may still be added later.

### Custom binary scene format

Rejected because it would reduce inspectability and add unnecessary parser and compatibility burden.

## Verification

- The Milestone 1 vertical slice emits a valid STL and structured JSON summary.
- The end-to-end CLI emits the artifact set documented in `docs/PROJECT.md`.
- Packing modules do not depend on a concrete file format.
