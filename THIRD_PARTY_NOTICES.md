# Third-Party Notices

This repository's own Python and C++ source is licensed under the BSD 3-Clause
License in [LICENSE](LICENSE). That license does not relicense dependencies,
overlay source fetched by vcpkg, or a linked executable.

## Source-distribution boundary

The repository contains vcpkg manifests, registry pins, overlay recipes, and a
narrow VTK compatibility patch. It does not contain the downloaded VTK, TetGen,
Ipopt, or other dependency source/binaries. A source archive must retain this
file, the root project license, [thirdparty/DEPENDENCIES.md](thirdparty/DEPENDENCIES.md),
the vcpkg manifests and overlays, the vcpkg MIT and VTK BSD notices under
[thirdparty/LICENSES](thirdparty/LICENSES), and the licensing ADRs named below.

Dependency resolution is pinned to:

- the builtin vcpkg registry at
  `271a5b8850aa50f9a40269cbf3cf414b36e333d6`;
- Microsoft's official vcpkg registry at
  `62159a45e18f3a9ac0548628dcaf74fcb60c6ff9` for `gl2ps` only; and
- the repository overlays under `thirdparty/vcpkg-ports/`.

The complete verified non-feature package/version closure is checked in as
[thirdparty/vcpkg-resolved-x64-windows.json](thirdparty/vcpkg-resolved-x64-windows.json).
It is release evidence, not a replacement for the license texts installed by
vcpkg.

## Direct dependency notices

| Dependency | Resolved version | License/notice boundary |
| --- | --- | --- |
| VTK | `9.3.0-pv5.12.1#12` | VTK is primarily BSD-3-Clause with module-specific notices. Preserve the complete installed `share/vtk/copyright` file and all notices for the resolved transitive closure. |
| Eigen3 | `3.4.1#1` | MPL-2.0 applies to the headers used by IROP. Preserve the complete installed Eigen notice bundle. |
| CLI11 | `2.5.0` | BSD-3-Clause. |
| nlohmann-json | `3.12.0#1` | MIT. |
| spdlog | `1.16.0` | MIT; the resolved build also uses fmt under MIT. |
| Catch2 | `3.11.0` | BSL-1.0; test/build dependency only. |
| GL2PS | `1.4.2#5` | GNU Library General Public License v2 or the alternative GL2PS License v2. Preserve `README.txt`, `COPYING.LGPL`, and `COPYING.GL2PS` from the installed copyright material. |
| TetGen | `1.6.0` | AGPL-3.0-or-later under the selected open-source path. Preserve the complete upstream license and satisfy the combined-work Corresponding Source/build-material obligations before conveying a linked build. |
| Ipopt | `3.14.19` | Ipopt is EPL-2.0. The selected official Windows archive also contains MUMPS (CeCILL-C), METIS (Apache-2.0), oneMKL, and Intel compiler runtimes under their own terms. Its installed files are not yet a complete public-binary compliance bundle. |

[thirdparty/DEPENDENCIES.md](thirdparty/DEPENDENCIES.md) is the detailed
version, source, hash, runtime-closure, and notice record. In particular,
[ADR-0009](docs/adr/0009-tetgen-1-6-agpl-overlay-and-adapter.md) governs the
TetGen AGPL path and
[ADR-0010](docs/adr/0010-ipopt-3-14-19-official-windows-binary-adapter.md)
governs the current Ipopt/Intel archive.

## Notice audit and staging

After vcpkg manifest installation, audit the exact resolved closure and stage a
notice-only bundle with:

```powershell
pwsh -NoProfile -File scripts/audit-third-party-notices.ps1 `
  -InstalledRoot build/windows-vs2026/vcpkg_installed `
  -BundleOutput build/release-notices
```

The audit fails if the registry pins or any exact package version differ from
the reviewed inventory, if an unexpected package appears, or if an installed
`share/<package>/copyright` file is missing or empty. The staged directory
contains the project/overlay notices, these records, every installed vcpkg
copyright file, VTK's auxiliary module licenses, and Ipopt's upstream
documentation and binary-build inventory.

Passing the audit is necessary release evidence; it does not clear a license
compatibility decision.

## Current distribution gate

Public source distribution is supported when the source-release checklist is
satisfied. Public executable, installer, portable archive, container, package,
or hosted-service distribution is not currently approved:

- TetGen requires a release-specific AGPL notice, Corresponding Source, and
  build-material review for the linked combination and a network-use review for
  hosted use.
- The exact Intel-linked Ipopt Windows bundle remains blocked by ADR-0010 until
  its complete component notices/source-availability obligations and
  compatibility with the selected TetGen AGPL path are cleared.
- Debug/MDD Ipopt binaries and the MSVC Debug CRT are development-only and must
  not be redistributed.

The preferred future binary path is a source-built Ipopt configuration using
MUMPS and OpenBLAS, followed by a fresh component-level review. See
[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md).
