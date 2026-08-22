# ADR-0009: TetGen 1.6.0 AGPL Overlay and Private Adapter

- Status: Accepted
- Date: 2026-08-22
- Deciders: Project maintainers

## Context

Milestone 3 requires the tetrahedral cell complex used by the Python CAT path.
Although that function and the migration goal call it constrained Delaunay
tetrahedralization, the live reference supplies explicit `O0/0Q` switches. The
historical Python wrapper treats a nonempty switch string as authoritative and
therefore bypasses the accompanying PLC/CDT keyword configuration. The pinned
builtin vcpkg registry has no TetGen port, and the checked-in local TetGen
overlay was deliberately non-activatable until its exact source, integration
model, and license path were approved.

TetGen 1.6.0 is available from WIAS under AGPL-3.0-or-later or under separate
commercial terms. The project maintainer approved the AGPL path for this
open-source project. Commercial versus non-commercial use does not change the
AGPL conditions; the relevant boundary is whether a covered combined work is
conveyed or offered as a network service.

## Decision

- Pin the official TetGen v1.6.0 release at commit
  535f9c41f44abc832a7bbf2c9c7af003d1c18f3c.
- Acquire it through the repository-owned vcpkg overlay using archive SHA-512
  62e5fc640f72e594ad7d7286075f85cb590d4a71b979e0b035d545543e4d80807db26c2f56775032e4a94abbdaf411473273bd304ad77bf1a451c9db435dcfcf.
  This is the full-commit GitHub archive hash used by `vcpkg_from_github`, not
  the different tag-named archive hash.
- Apply no project patch to TetGen source. Build its static library form with
  TETLIBRARY, install the upstream license, and expose a namespaced
  TetGen::TetGen package target.
- Link TetGen privately inside irop_core. TetGen classes, macros, allocation
  rules, and index types remain confined to the tetrahedralization adapter.
- Preserve the live Python wrapper's literal `O0/0Q` point-union behavior under
  `IROP-COMPAT-0005`. Enabling PLC/CDT switches requires a later, deliberate
  compatibility decision with packing evidence.
- Keep repository-owned Python and C++ files under their existing BSD-3-Clause
  notices. Do not imply that BSD relicenses TetGen or the linked combination.
- Treat any conveyed executable or other combined build containing TetGen as
  subject to the applicable AGPL-3.0-or-later obligations, including license
  notices and Corresponding Source/build material. A hosted or network-facing
  use requires a release-specific AGPL review.
- Continue the existing policy of no binary distribution. Any future installer,
  portable executable, container, package, or hosted service requires a fresh
  third-party license and notice review before publication.

## Consequences

- Milestone 3 can use the direct in-process TetGen API and reproduce the
  effective Python reference path rather than its misleading CDT naming.
- The vcpkg overlay is a maintained dependency recipe even though no TetGen
  source is copied into project-owned modules.
- Static versus dynamic linking is an engineering choice, not a way to avoid
  the AGPL combined-work analysis.
- TetGen does not provide a project-controlled hard memory or time quota around
  its in-process algorithm. Input and output limits reduce exposure but cannot
  strictly cap peak backend work; this residual risk remains documented.

## Alternatives Considered

### Commercial TetGen license

Viable, but no commercial license has been obtained and the maintainer selected
the AGPL path.

### Dynamically linked TetGen DLL

Rejected as a licensing workaround. It would still use direct calls and shared
in-process data and would not provide a reliable AGPL boundary.

### User-supplied TetGen executable

Deferred. A file/process boundary could improve isolation but would change the
accepted overlay-and-library architecture, require additional parsers and
process controls, and would still need license review.

### Replace TetGen with a permissive backend

Deferred because no audited drop-in replacement has established compatible
constrained Delaunay behavior for the Python reference.

## Verification

- vcpkg must verify the pinned archive hash and install the complete upstream
  license as share/tetgen/copyright.
- Public irop headers must compile without including tetgen.h.
- Debug, Release, and analysis builds must link and exercise the private adapter
  on compact object/container fixtures.
- Dependency documentation must identify the exact source, license, notices,
  and combined-distribution gate.
