# ADR-0004: Layered Agent Documentation and Repository Skill

- Status: Accepted
- Date: 2026-08-18
- Deciders: Project maintainers

## Context

Future agents and maintainers should understand the project cheaply without rereading the entire Python and C++ codebases. Durable decisions, current progress, compatibility details, and task workflows change at different rates and should not be mixed into one large document.

## Decision

Use layered project memory:

- `AGENTS.md` for short onboarding and mandatory maintenance rules.
- `docs/PROJECT.md` for the authoritative project definition.
- `docs/IMPLEMENTATION_PLAN.md` for milestone gates.
- `docs/STATUS.md` for mutable implementation state and next work.
- `docs/COMPATIBILITY.md` for preserved behaviors and deviations.
- `docs/adr/` for durable rationale.
- `.agents/skills/develop-irop-cpp/` for the preferred C++ implementation workflow.

Implement the project skill as one concise kernel with activity-specific references. Do not split it into inherited or overlapping skills until real usage shows that independent triggers and workflows are beneficial.

When the project-wide coding approach changes, update the skill and relevant project document together. Record a difficult-to-reverse policy change with an ADR.

## Consequences

- Agents receive a short reading path and load detailed workflow guidance only when needed.
- Progress does not pollute immutable decision history.
- Documentation has an explicit update cost on every implementation change.
- Stale status becomes visible because changes must cite verification evidence.

## Alternatives Considered

### ADRs as the implementation tracker

Rejected because ADRs are durable decision records, while implementation status is frequently mutable.

### One comprehensive maintainer document

Rejected because it would be expensive to load, difficult to keep current, and likely to mix intention with actual status.

### Multiple activity skills immediately

Deferred because a single project workflow with conditional references avoids duplicated policy and premature trigger boundaries. A general C++ skill may be extracted after this project skill has matured through real usage.

## Verification

- `AGENTS.md` names the reading order and update requirements.
- Every implementation change updates `docs/STATUS.md`.
- The repository skill passes structural validation.
- Changes to preferred C++ practice update the skill in the same change.
