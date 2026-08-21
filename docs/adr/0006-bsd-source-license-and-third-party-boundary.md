# ADR-0006: BSD Source License and Third-Party Boundary

- Status: Accepted
- Date: 2026-08-21
- Deciders: Project maintainers

## Context

The repository already contains the Python reference implementation under a BSD 3-Clause license with a Maurits Bos copyright notice. The C++ implementation is a rewrite in the same repository, and the intended publication model is public source on GitHub without a planned binary release.

The C++ design also depends on separately licensed libraries. Most importantly, the planned TetGen release is offered under AGPL-3.0 or a commercial license and can impose obligations on a combined work that are not described by this repository's BSD license.

## Decision

- Keep the repository-owned Python and C++ source under the existing BSD 3-Clause license.
- Preserve the existing copyright notice, conditions, and disclaimer in [LICENSE](../../LICENSE).
- Treat public GitHub publication as source redistribution and keep the license file with redistributed source.
- Treat third-party licenses as an independent dependency boundary. Do not imply that the repository BSD license relicenses dependency source.
- Record dependency versions, sources, licenses, and required notices when dependencies are added.
- Review the exact TetGen version and integration model before enabling its overlay port, and review all combined-work obligations before distributing a binary or operating a hosted service.

No binary distribution is currently planned. A future decision to publish installers, portable executables, containers, or a hosted service must include a release-specific third-party license review.

## Consequences

- The project keeps a permissive, widely understood license for source it owns while honoring the notice already attached to the reference implementation.
- A public source repository can remain simple: the root `LICENSE` covers project-owned source, while dependency notices describe third-party material.
- A resulting executable may be subject to obligations beyond BSD-3-Clause, especially if TetGen is linked into the same program.
- Dependency acquisition and release work must include license evidence instead of assuming that vcpkg availability implies license compatibility.

## Alternatives Considered

### Remove the existing notice because the C++ code is rewritten

Rejected. The repository still contains and derives its behavioral reference from the existing Python project, and the maintainer explicitly chose to retain the BSD license and notice.

### Apply BSD-3-Clause to all vendored dependency code

Rejected because project maintainers cannot replace a dependency author's license.

### Select a different repository-wide license now

Rejected because BSD-3-Clause already matches the desired permissive source publication model. Dependency-specific constraints are handled at their boundary rather than by obscuring them with a different top-level label.

## Verification

- The root `LICENSE` remains unchanged and contains the BSD 3-Clause text and original notice.
- Project documentation distinguishes repository-owned source from dependency licenses.
- Dependency and release milestones include a license review before a combined build is distributed.
