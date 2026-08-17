# ADR-0002: Reusable Core Library and Thin CLI

- Status: Accepted
- Date: 2026-08-18
- Deciders: Project maintainers

## Context

The first product is a command-line tool, with a simple visualization UI planned later. Geometry, solver, and packing logic must be reusable without tying core behavior to command parsing, terminal output, UI frameworks, or third-party data types.

## Decision

Implement packing behavior in a reusable `irop_core` library and keep the `irop` CLI as a thin application boundary.

Organize the core into cohesive model, geometry, tetrahedralization, CAT, optimization, packing, and I/O modules. Use project-owned domain types across module boundaries. Hide VTK, TetGen, Ipopt, and other dependency-specific types behind narrow adapters.

Prefer composition, explicit state, and direct construction. Add interfaces only for substantial dependency boundaries or genuine interchangeable behavior.

## Consequences

- A later UI can reuse the core without launching or parsing the CLI.
- Third-party updates are localized and unit testing can use project-owned inputs.
- Conversion at adapter boundaries has an implementation cost.
- Maintainers must resist both monolithic orchestration and speculative abstraction layers.

## Alternatives Considered

### CLI-only monolith

Rejected because it would couple future visualization and testing to terminal behavior.

### Framework-style plugin architecture

Rejected because one implementation and a small number of meaningful dependency boundaries do not justify the complexity.

### Expose VTK as the public mesh model

Rejected because it would make VTK ownership, versioning, and representation choices part of every module and future public contract.

## Verification

- The CLI target contains no packing algorithm implementation.
- Public core headers do not expose dependency-specific types.
- Module dependencies follow the direction described in `docs/PROJECT.md`.
