# ADR-0010: Ipopt 3.14.19 Official Windows Binary Adapter

- Status: Accepted
- Date: 2026-08-22
- Deciders: Project maintainers

## Context

Milestone 4 requires a runnable Ipopt backend for the seven-variable local
nonlinear program. The builtin `coin-or-ipopt` port at the pinned vcpkg baseline
builds Ipopt without MUMPS, HSL, SPRAL, Pardiso, or another usable sparse
linear solver. Updating to the current official vcpkg port does not resolve that
runtime gap. The project also does not want to patch third-party source.

COIN-OR publishes official Ipopt 3.14.19 Windows x64 archives for the MSVC
2022 dynamic CRT. The archives include a MUMPS 5.8 backend and the release also
contains an Intel oneMKL Pardiso implementation. They therefore provide a
runnable local-development path, but their complete license and provenance
surface is larger than Ipopt's EPL-2.0 license alone.

## Decision

- Package the source-unmodified official COIN-OR archives through the
  repository-owned `coin-or-ipopt` vcpkg overlay. Do not check DLLs into the
  repository.
- Pin `Ipopt-3.14.19-win64-msvs2022-md.zip` with SHA-512
  `15312d94293ccc2e89f11d2355d619116a550ed65a083c9299cc60db379ef6da85984a6fedfd8336f442fb30dff5f507dbc5d4b25d324768d2e18b439eee0e54`.
- Pin `Ipopt-3.14.19-win64-msvs2022-mdd.zip` with SHA-512
  `441c6e13ac95da577f94f975e986c958979c15b7134a9530778baae72b2311967b1200d35967931a2883befe81201f6482a98aef961ab2ff2a6ed80d0a42f1d6`.
- Support Windows x64, dynamic libraries, and the dynamic CRT only. The MDD
  archive and its PDBs are developer artifacts; do not redistribute the Debug
  build or the MSVC Debug CRT.
- Install only the Ipopt headers/import library and this runtime closure:
  - Release: `ipopt-3.dll`, `coinmumps-3.dll`, `libifcoremd.dll`,
    `libiomp5md.dll`, `libmmd.dll`, and `svml_dispmd.dll`.
  - Debug: `ipopt-3.dll`, `coinmumps-3.dll`, `libifcoremdd.dll`,
    `libiomp5md.dll`, `libmmd.dll`, `libmmdd.dll`, and `svml_dispmd.dll`.
- Omit the AMPL executables/interface, Java artifacts, and sIpopt. They are not
  used by the project-owned interface.
- Use `IpStdCInterface.h` only inside a private adapter, select MUMPS explicitly,
  and verify the loaded runtime is exactly 3.14.19. No Ipopt type may appear in
  a public project header.
- Disable Ipopt's ambient option-file loading by setting `option_file_name` to
  an empty string before solving. An untrusted working directory must not be
  able to override limits, select a different solver, or request file output
  through `ipopt.opt`.
- Preserve Ipopt's upstream EPL-2.0 material and inventory the archive's MUMPS
  (CeCILL-C), METIS (Apache-2.0), oneMKL, and Intel compiler-runtime terms.
  The Intel DLLs are Authenticode-signed by Intel; the Ipopt and coinmumps DLLs
  are unsigned and are trusted through the official COIN-OR release URL plus
  the pinned whole-archive hashes.
- Do not publish a combined executable, installer, archive, container, or other
  binary containing this exact Intel-linked bundle until a release-specific
  legal review confirms the complete notice/source obligations and the
  compatibility of the Intel binary terms with the project's selected TetGen
  AGPL path. This is a review gate, not a claim that every possible combination
  is necessarily incompatible.
- If public binary distribution becomes a goal, prefer a source-built Ipopt
  configuration using MUMPS and OpenBLAS without the embedded Intel payload,
  then repeat license, notice, source-availability, and runtime verification.

## Consequences

- Milestone 4 has a reproducible vcpkg-managed backend with an available sparse
  solver and no project patch to Ipopt or its bundled dependencies.
- The C ABI avoids coupling project code to the official archive's C++ ABI.
- The runtime footprint is substantial: the pruned Release closure is about
  122 MiB and the Debug closure about 161 MiB before optional PDBs.
- Local development and source publication can proceed, while public binary
  release remains intentionally gated.
- vcpkg application-local deployment must scan recursive DLL imports; merely
  copying `ipopt-3.dll` is insufficient.

## Alternatives Considered

### Update the vcpkg baseline

Rejected as the immediate solution. The newer official source port still does
not enable a runnable sparse linear solver, and moving the global baseline
would unnecessarily change the rest of the reviewed dependency graph.

### Build Ipopt and MUMPS from source now

Viable and preferred for a future distributable build, but deferred because a
reliable native Windows Fortran/BLAS toolchain and additional build maintenance
would expand this milestone. It remains the documented fallback.

### Treat dynamic linking as a license boundary

Rejected. A DLL is an engineering and deployment boundary, not an automatic
exception from the license analysis for a conveyed combined application.

### Use the Intel Pardiso backend

Rejected for the Milestone 4 runtime path. The official archive embeds that
implementation, but the adapter explicitly selects MUMPS for a stable,
open-source solver choice.

## Verification

- vcpkg must verify both complete archive SHA-512 values and pass
  `--enforce-port-checks` for the overlay.
- Debug and Release solver tests must load runtime 3.14.19, select MUMPS, and
  solve compact box and asymmetric fixtures.
- The executable test directory must contain the complete recursive runtime
  closure for its configuration.
- Public headers must compile without an Ipopt include or type.
- A test working directory containing a hostile `ipopt.opt` must not alter
  configured behavior or create the file-requested output.
- Dependency documentation and future release materials must retain the binary
  distribution gate and the installed upstream notices.

