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
| [0006](0006-bsd-source-license-and-third-party-boundary.md) | Accepted | Retain BSD-3-Clause for project source and track third-party terms separately |
| [0007](0007-authoritative-clang-format-profile.md) | Accepted | Use the checked-in clang-format profile as the C++ style authority |
| [0008](0008-milestone-2-initialization-contract.md) | Accepted | Fix deterministic initialization state, bounded geometry work, and atomic artifact publication |
| [0009](0009-tetgen-1-6-agpl-overlay-and-adapter.md) | Accepted | Pin TetGen 1.6.0 under AGPL and isolate it behind a private vcpkg adapter |
| [0010](0010-ipopt-3-14-19-official-windows-binary-adapter.md) | Accepted | Package official Ipopt 3.14.19 Windows binaries behind a private C adapter and binary-release gate |
| [0011](0011-milestone-5-packing-outcome-and-artifact-contract.md) | Accepted | Bound the end-to-end packing loop and atomically distinguish successful artifacts from structured unsuccessful summaries |
| [0012](0012-transform-consistent-barrier-bounded-local-solves.md) | Accepted | Compose local rotations exactly and bound scale optimization near the active barrier |
| [0013](0013-bounded-structured-initialization-and-collision-broad-phase.md) | Accepted | Preserve successful seeded initialization, add a bounded structured fallback, and skip separated collision pairs with an explicit pair-work bound |

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
