# Release Checklist

This checklist separates source publication, which is supported, from public
binary or hosted-service distribution, which remains gated.

## Source release

A source release may proceed only when all of the following are true:

- The archive is created from a reviewed commit and contains no local
  `build/`, `artifacts/`, `rc/`, vcpkg downloads, installed packages, DLLs,
  executables, generated meshes, or user data.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `thirdparty/DEPENDENCIES.md`,
  `thirdparty/vcpkg-resolved-x64-windows.json`, `thirdparty/LICENSES/`, the
  vcpkg manifests and overlays, and the applicable ADRs are included.
- The supported Visual Studio 2026 preset configures, builds, and passes CTest
  with warnings as errors.
- The checked-in formatting target and the Ninja clang-tidy preset pass.
- The dependency notice audit passes against the manifest-installed tree.
- The release notes distinguish verified behavior, experimental parity work,
  and deferred scalability/UI work.

A clean source archive can be inspected before publication with:

```powershell
git archive --format=zip --output=build/irop-source.zip HEAD
tar -tf build/irop-source.zip
```

Do not use `git archive` over an uncommitted working tree as evidence for
uncommitted release materials.

## Local notice audit

Configure first so vcpkg resolves the exact manifest:

```powershell
cmake --preset windows-vs2026
pwsh -NoProfile -File scripts/audit-third-party-notices.ps1 `
  -InstalledRoot build/windows-vs2026/vcpkg_installed `
  -BundleOutput build/release-notices
```

The audit compares every non-feature package record against the reviewed
`x64-windows` inventory and requires a nonempty installed copyright file for
every package. The output is a notice-only staging directory. It is not a
redistributable application bundle and must not be presented as one.

## Binary and hosted-service gate

Do not upload or otherwise convey an IROP executable, installer, portable
archive, container, package, or VM image, and do not launch a public hosted
service, until a release owner records a release-specific approval that covers
all of these items:

1. Select the dependency build actually being distributed. Prefer replacing
   the official Intel-linked Ipopt archive with a source-built
   Ipopt/MUMPS/OpenBLAS stack.
2. Inventory every shipped file and recursive DLL import. Exclude Debug/MDD
   Ipopt binaries, PDB-only developer material, and the MSVC Debug CRT.
3. Resolve ADR-0010 for the exact Ipopt, MUMPS, METIS, BLAS, Fortran/runtime,
   and other payload, including license compatibility, notices, and any source
   availability requirements.
4. Resolve ADR-0009 for the linked TetGen combination. Include
   AGPL-3.0-or-later license material and the complete Corresponding Source and
   build/install information required for recipients.
5. For any network-facing use, complete the AGPL network-use review and provide
   the required source access for that exact running version.
6. Rerun the exact dependency audit and include the staged full license/notice
   set next to the distributed artifact.
7. Perform clean-machine installation, runtime-DLL closure, CLI smoke,
   cancellation, and output-contract verification on the final payload.
8. Record hashes for the final artifact and its source/notice bundle.

Passing CI, using DLLs, linking dependencies privately, or keeping the project
non-commercial does not by itself clear these obligations.

## CI policy

The hosted workflow validates the supported `windows-vs2026` preset on the
explicit GitHub `windows-2025-vs2026` image, runs Release CTest and the
formatting gate, then uses a separate generator-specific installed tree for the
Ninja clang-tidy build and tests. Both trees share only the vcpkg binary cache;
sharing one installed tree across generators can trigger incompatible ABI
rewrites. The workflow stages and audits notices but intentionally does not
upload an executable or other binary artifact.

External actions are pinned to immutable commits from their Node 24-capable
generations. The workflow definition and each corresponding gate have passed
local review and execution, but the first hosted run remains pending.

The `build-package` job builds the historical Python wheel and source
distribution as package-validation evidence only. Its output is neither an
approved repository source release nor a substitute for the exact dependency
inventory, source-archive review, and notice audit above; the workflow does not
upload that package.
