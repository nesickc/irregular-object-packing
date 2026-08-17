# Architecture Decision Records

ADRs record durable decisions and their rationale. They are not a progress tracker; current implementation state belongs in `docs/STATUS.md`.

## Status Values

- `Proposed`
- `Accepted`
- `Superseded by ADR-NNNN`
- `Rejected`
- `Deprecated`

Accepted ADRs are immutable apart from typo fixes and explicit amendment notes. Replace a decision by adding a new ADR and marking the old record superseded.

## Index

| ADR | Status | Decision |
| --- | --- | --- |
| [0001](0001-compatibility-first-port.md) | Accepted | Port successful Python behavior before redesign |
| [0002](0002-core-library-and-cli.md) | Accepted | Build a reusable modular core with a thin CLI |
| [0003](0003-windows-cmake-vcpkg-toolchain.md) | Accepted | Target Windows 11, MSVC, C++20, CMake, and vcpkg |
| [0004](0004-agent-documentation-and-skill.md) | Accepted | Use layered project memory and a repository-scoped skill |
| [0005](0005-stl-first-output-contract.md) | Accepted | Make STL primary and JSON the canonical run record |

Use [0000-template.md](0000-template.md) for new records.

## When an ADR Is Required

Create or supersede an ADR when changing:

- Major module boundaries or ownership.
- A foundational dependency or dependency strategy.
- Supported platform, compiler, language baseline, or build system.
- Public CLI, library, mesh, state, or result contracts.
- Compatibility philosophy.
- Project-wide security, testing, performance, or agent-development policy.

Ordinary implementation details, status changes, and reversible local refactors do not need ADRs.
