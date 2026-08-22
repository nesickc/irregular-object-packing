# Local vcpkg overlay ports

This directory is the checked-in, repository-owned overlay root selected by
`vcpkg-configuration.json`. It contains the active VTK compatibility overlay,
the patchless TetGen source/build overlay, and the Ipopt official-Windows-binary
packaging overlay.

## Ipopt

`coin-or-ipopt/` packages the source-unmodified official COIN-OR Ipopt 3.14.19
Windows x64 MSVC 2022 MD and MDD archives. Both complete archives are pinned by
SHA-512 in the portfile. The port forces dynamic libraries and the dynamic CRT,
exports `Ipopt::Ipopt`, and installs only the header/import-library surface and
recursive DLL closure needed by the project's private C adapter.

The adapter explicitly selects the bundled MUMPS 5.8.0 backend. AMPL, Java, and
sIpopt artifacts are deliberately omitted. Release needs six DLLs; Debug needs
seven because it imports both `libmmd.dll` and `libmmdd.dll`. Keep the runtime
lists synchronized with import-table inspection whenever the archive changes.
Do not infer the closure only from CMake's direct imported target.

The overlay applies no source patch, but it is not a source build and its
archive contains components under several licenses. ADR-0010 and
`thirdparty/DEPENDENCIES.md` define the provenance record and prohibit public
binary distribution until a release-specific review clears the exact payload.
The Debug/MDD archive and MSVC Debug CRT are development-only.

## VTK

`vtk/` is a copy of the `vtk` port from pinned vcpkg registry commit
`271a5b8850aa50f9a40269cbf3cf414b36e333d6`. It keeps the baseline port version
`9.3.0-pv5.12.1#12`, source revision, archive hash, features, and patches. Its
only changes from that exact baseline are the addition of
`msvc-19.50-fmt-checked-iterator.patch` and the portfile entry that applies it.

The patch adjusts one preprocessor guard in VTK's vendored diy2/fmt header. When
MSVC 19.50 or newer is built with both `_SECURE_SCL` and
`_SCL_SECURE_NO_WARNINGS`, it avoids the legacy
`stdext::checked_array_iterator` branch and uses fmt's existing raw-pointer
fallback. This is a dependency build compatibility fix, not a new VTK feature or
a project behavior change.

Rebase the complete overlay from the exact selected registry port whenever the
vcpkg baseline changes, then re-evaluate the narrow patch. Do not edit the copied
port for unrelated convenience changes.

## TetGen

`tetgen/` pins official TetGen v1.6.0 commit
`535f9c41f44abc832a7bbf2c9c7af003d1c18f3c` with full-commit archive SHA-512
`62e5fc640f72e594ad7d7286075f85cb590d4a71b979e0b035d545543e4d80807db26c2f56775032e4a94abbdaf411473273bd304ad77bf1a451c9db435dcfcf`.
The port-owned CMake wrapper compiles upstream `tetgen.cxx` and `predicates.cxx`
unchanged as a static library, exports `TetGen::TetGen` with `TETLIBRARY`,
installs `tetgen.h`, and installs the upstream AGPL-3.0-or-later license through
vcpkg's standard copyright path.

The root manifest links this target privately through the project-owned
tetrahedralization adapter. `PRIVATE` is an API/type-isolation boundary, not a
license boundary. ADR-0009 records the selected AGPL path and the review gate
for any future combined binary distribution or hosted service.

## Scoped official GL2PS registry

GL2PS is not a local overlay. `vcpkg-configuration.json` routes only the `gl2ps`
package to a newer, pinned commit of Microsoft's official vcpkg Git registry,
where it resolves as `1.4.2#5`. Every other registry package remains on the
root manifest's builtin baseline, and the checked-in VTK overlay remains active.

The scoped port uses the official upstream archive identity and SHA-512. During
local verification, the `geuz.org` endpoint was unreachable, so a byte-identical
copy from the MIT Gentoo distfiles mirror was placed in vcpkg's download cache.
No mirror port or source substitution is part of this overlay directory or the
shared registry configuration.
