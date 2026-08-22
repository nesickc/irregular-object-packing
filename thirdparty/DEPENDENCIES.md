# Direct C++ Dependencies

The default C++ dependency graph is resolved from vcpkg builtin-registry commit
`271a5b8850aa50f9a40269cbf3cf414b36e333d6` using the `x64-windows` triplet.
Only `gl2ps` is routed to the official Microsoft vcpkg Git registry pinned at
`62159a45e18f3a9ac0548628dcaf74fcb60c6ff9`. The table records direct
dependencies through Milestone 3. Port revisions are part of the resolved vcpkg
version even when the upstream version is unchanged.

| Dependency | Resolved version | Role | Upstream source | License and required notice |
| --- | --- | --- | --- | --- |
| VTK | `9.3.0-pv5.12.1#12` | STL I/O, private mesh/geometry adapters, triangle-surface intersection, and optional VTU/VTP diagnostics | [Kitware/VTK at `09a76bc`](https://github.com/Kitware/VTK/tree/09a76bc55b37caad94d0d8ebe865caaed1b438af) | BSD-3-Clause at the project level, with module-specific copyright/license files. Preserve the resolved `share/vtk/copyright` material. The vcpkg port's license metadata is currently `null`, so review the installed notices rather than relying on manifest metadata alone. |
| Eigen3 | `3.4.1#1` | Private fixed-size transform mathematics and Eigen version reporting | [libeigen/eigen `3.4.1`](https://gitlab.com/libeigen/eigen/-/tree/3.4.1) | MPL-2.0 for the used Eigen headers. Preserve `share/eigen3/copyright`; the installed notice bundle also carries Apache-2.0, BSD, and MINPACK texts for other portions of the distribution. |
| TetGen | `1.6.0` | Private tetrahedralization backend for the Python-compatible CAT cell complex | [TetGen/TetGen at `535f9c41`](https://github.com/TetGen/TetGen/tree/535f9c41f44abc832a7bbf2c9c7af003d1c18f3c) | AGPL-3.0-or-later under the selected open-source path. Preserve the complete upstream `LICENSE`, installed as `share/tetgen/copyright`. Any conveyed combined executable/build and any hosted-service release require an AGPL-specific source, build-material, and notice review under ADR-0009. |
| CLI11 | `2.5.0` | Command-line parsing | [CLIUtils/CLI11 `v2.5.0`](https://github.com/CLIUtils/CLI11/tree/v2.5.0) | BSD-3-Clause; retain the upstream `LICENSE` notice. |
| nlohmann-json | `3.12.0#1` | Inspection, placement, and run-summary JSON serialization | [nlohmann/json `v3.12.0`](https://github.com/nlohmann/json/tree/v3.12.0) | MIT; retain `LICENSE.MIT`. |
| spdlog | `1.16.0` | CLI diagnostics | [gabime/spdlog `v1.16.0`](https://github.com/gabime/spdlog/tree/v1.16.0) | MIT; retain the upstream `LICENSE` notice. |
| Catch2 | `3.11.0` | C++ test support | [catchorg/Catch2 `v3.11.0`](https://github.com/catchorg/Catch2/tree/v3.11.0) | BSL-1.0; retain `LICENSE.txt`. Test-only use does not remove source-redistribution notice obligations. |

`vtk` is declared with `default-features: false` and the focused `utf8` feature.
This avoids unrelated VTK features such as CGNS, PDF, Theora, NetCDF, PROJ,
SEACAS, and SQL while keeping the STL modules and UTF-8 Windows path handling
needed by the vertical slice.

`irop_core` links the header-only `Eigen3::Eigen` target privately. Public headers
remain Eigen-free, and the private `EIGEN_MPL2_ONLY` definition rejects accidental
use of Eigen's optional non-MPL2 headers. Promoting Eigen from a VTK-transitive
package to a direct dependency adds no runtime deployment component.

## Repository-owned TetGen overlay

The checked-in `thirdparty/vcpkg-ports/tetgen/` port pins official v1.6.0 commit
`535f9c41f44abc832a7bbf2c9c7af003d1c18f3c`. `vcpkg_from_github` verifies the
full-commit archive SHA-512
`62e5fc640f72e594ad7d7286075f85cb590d4a71b979e0b035d545543e4d80807db26c2f56775032e4a94abbdaf411473273bd304ad77bf1a451c9db435dcfcf`.
This differs from the tag-named archive hash because GitHub serves those archive
identities separately.

Upstream v1.6.0 has no installable CMake package. A port-owned wrapper compiles
the unmodified `tetgen.cxx` and `predicates.cxx` sources as a static library,
exports `TetGen::TetGen`, propagates the required `TETLIBRARY` definition,
installs `tetgen.h`, and installs the complete upstream license. No project
patch is applied to TetGen source. The overlay passed a fresh isolated
`x64-windows` Debug/Release install with vcpkg `--enforce-port-checks` on
2026-08-22.

`irop_core` links TetGen privately and exposes only project-owned points,
tetrahedra, limits, work counters, statuses, and diagnostics. This private
static boundary prevents TetGen types from becoming a public API contract; it
does not avoid or weaken TetGen's AGPL-3.0-or-later conditions. Project-owned
source retains BSD-3-Clause notices, while any future conveyed combined build
or hosted service must complete the release-specific review in ADR-0009. No
binary distribution is currently planned.

The adapter passes the literal `O0/0Q` switch string used by the historical
Python wrapper. Because that nonempty string bypasses the reference's separate
`cdt=True` and `steinerleft=0` keyword arguments, the effective compatibility
path tetrahedralizes the participant point union rather than enabling TetGen's
PLC/CDT mode. This is recorded as `IROP-COMPAT-0005`; it is independent of the
dependency acquisition and license decision.

## Repository-owned VTK overlay

The checked-in `thirdparty/vcpkg-ports/vtk/` port is based exactly on the `vtk`
port at the pinned registry commit above. It retains the registry port identity
`9.3.0-pv5.12.1#12`, the VTK source revision and archive hash, its feature model,
and all baseline patches. Relative to that baseline port, the repository overlay
adds only:

- `msvc-19.50-fmt-checked-iterator.patch`; and
- the corresponding entry in the port's `PATCHES` list.

The patch is limited to VTK's vendored diy2 copy of fmt. MSVC 19.50 and newer no
longer make the legacy `stdext::checked_array_iterator` path usable in the
affected secure-library configuration. The vendored header selected that path
whenever `_SECURE_SCL` was defined even though the VTK build also defines
`_SCL_SECURE_NO_WARNINGS`. The patch changes the guard from `_SECURE_SCL` alone
to `_SECURE_SCL && !_SCL_SECURE_NO_WARNINGS`, causing that configuration to use
fmt's existing raw-pointer fallback. It does not alter the STL adapter, public
project types, VTK version, or package features. The overlay was exercised with
MSVC 19.51.36252.

When the vcpkg baseline changes, regenerate the overlay from the newly selected
baseline port and re-evaluate this one patch. Do not allow the copied port to
accumulate unrelated changes.

## Scoped official GL2PS registry

`gl2ps` is an unconditional transitive dependency of the selected VTK port. It
is resolved as `1.4.2#5` from the package-scoped official registry rather than
moving every dependency to a newer baseline. The selected registry entry uses
port tree `51e4c4e828efb0b32efd657df71929bb9ba521d5` and the upstream archive
`https://geuz.org/gl2ps/src/gl2ps-1.4.2.tgz` with SHA-512
`46652e1b3825ace61dbd77c4b0bf451e7671c248eb18bbd3369e2fac00056ea4cd5d2578561984313c239e3b02f78b9d9a76d963c935af65a13bc2abfc538620`.

GL2PS is offered under the GNU Library General Public License version 2 or its
alternative GL2PS License version 2. Preserve the installed `README.txt`,
`COPYING.LGPL`, and `COPYING.GL2PS` notices for redistribution review.

The upstream `geuz.org` endpoint timed out during local verification on
2026-08-22. A copy from the MIT Gentoo distfiles mirror matched the official
port's complete SHA-512 and was used only to seed vcpkg's download cache. The
checked-in registry entry, port recipe, expected archive identity, and source
declaration were not changed. This replaces the earlier build-only GL2PS `#4`
mirror overlay, whose archive had different provenance and is not part of the
shared dependency strategy.

If the declared upstream is temporarily unavailable, seed the same
SHA-512-validated vcpkg download-cache entry from Gentoo's distfiles CDN and let
vcpkg verify the official port hash before configuring:

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" x-download `
  "$env:VCPKG_ROOT/downloads/gl2ps-1.4.2.tgz" `
  --sha512=46652e1b3825ace61dbd77c4b0bf451e7671c248eb18bbd3369e2fac00056ea4cd5d2578561984313c239e3b02f78b9d9a76d963c935af65a13bc2abfc538620 `
  --url=https://distfiles.gentoo.org/distfiles/04/gl2ps-1.4.2.tgz
```

This is an acquisition-cache fallback, not a package overlay. A hash mismatch
fails before the asset can be consumed by the official GL2PS port.

## Transitive and deferred dependencies

This direct inventory is not a binary-release notice bundle. Before distributing
a binary, inventory every resolved transitive package and preserve the copyright
files installed under `vcpkg_installed/<triplet>/share/`.

- `coin-or-ipopt` is deferred until Milestone 4.
