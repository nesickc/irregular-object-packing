# Local vcpkg overlay ports

This directory is the checked-in, repository-owned overlay root selected by
`vcpkg-configuration.json`. It contains one active compatibility overlay and one
deliberately disabled guard port.

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

`tetgen/` remains a hard-disabled guard. Its contradictory platform support
expression rejects every target, and its portfile fails explicitly as a second
line of defense. No project target or root-manifest dependency can link TetGen.

Completing the TetGen port requires the Milestone 3 integration and license
review. Replace the guard only after recording the selected source revision,
hash, patches, license path, and verification evidence.

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
