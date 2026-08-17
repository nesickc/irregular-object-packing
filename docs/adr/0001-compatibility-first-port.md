# ADR-0001: Compatibility-First C++ Port

- Status: Accepted
- Date: 2026-08-18
- Deciders: Project maintainers

## Context

The Python repository contains a research implementation with useful behavior, incomplete end-to-end coverage, and several questionable or defective paths. Rewriting and redesigning simultaneously would make differences difficult to diagnose.

## Decision

Port behavior observed during successful Python runs before redesigning algorithms.

Correct an obvious defect only when the intended correction is clear and meaningful downstream reliance is unlikely. Track every questionable preserved behavior and every intentional deviation in `docs/COMPATIBILITY.md`, using stable linked code markers.

Keep the Python implementation available as the behavioral reference through the compatibility milestone.

## Consequences

- Early C++ code may intentionally reproduce behavior that is not ideal.
- Compatibility decisions become searchable and reviewable rather than implicit.
- Algorithm improvements occur against a working baseline and measurable evidence.
- Some failure-path defects will be corrected immediately for termination, state isolation, or executability.

## Alternatives Considered

### Clean redesign before porting

Rejected because simultaneous language and algorithm changes would obscure regression causes and delay tangible output.

### Literal bug-for-bug port

Rejected because obvious crash paths, shared mutable state, and unbounded execution do not provide useful compatibility and would undermine safety.

## Verification

- Compatibility-sensitive code contains a cataloged `COMPATIBILITY` or `DEVIATION` marker.
- `docs/COMPATIBILITY.md` records rationale, target milestone, status, and evidence.
- Compatibility tests compare successful behavior using tolerances and invariants.
