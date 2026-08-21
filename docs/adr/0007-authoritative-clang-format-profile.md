# ADR-0007: Authoritative clang-format Profile

- Status: Accepted
- Date: 2026-08-21
- Deciders: Project maintainers

## Context

The maintainer supplied a preferred `.clang-format` profile for this project and future C++ projects. The baseline documentation previously referred generally to Google C++ Style, which is not precise enough because the supplied profile deliberately changes indentation, braces, line width, initializer formatting, and related choices.

## Decision

- Check the supplied profile into the repository root as `.clang-format`.
- Treat that file as the complete formatting authority when prose guidance and formatter output differ.
- Format changed project-owned C++ files with this profile. Do not mechanically reformat unrelated files as part of a narrow change.
- Use the same profile as the maintainer's baseline when a reusable, user-level C++ skill is extracted in the future. Until then, keep the development workflow repository-scoped as decided in [ADR-0004](0004-agent-documentation-and-skill.md).
- Give other C++ repositories their own checked-in copy, or explicitly configure their tooling to use the canonical profile; a repository-local file cannot automatically govern unrelated directory trees.

The profile is based on Google style but has intentional overrides. Calling the project style simply “Google style” is therefore shorthand, not a license to replace the file with the stock preset.

## Consequences

- Visual Studio, command-line formatting, CI, and agents can apply one deterministic policy.
- The profile requires a clang-format version that recognizes all configured options. The Visual Studio 2026 bundled clang-format is the initial verified formatter.
- The 120-column limit and 4-space indentation differ from unmodified Google style.
- `InsertBraces: true` can make semantic edits; formatting changes must be reviewed like code changes rather than assumed to be whitespace-only.

## Alternatives Considered

### Use unmodified Google style

Rejected because it would discard the maintainer's explicit formatting choices.

### Describe the overrides only in prose

Rejected because prose can drift and cannot be applied or checked automatically.

### Install one machine-global fallback and omit repository files

Rejected as the project source of truth because machine-global discovery depends on directory layout and IDE configuration and is not reproducible for contributors or CI.

## Verification

- The root `.clang-format` matches the supplied profile's settings.
- Visual Studio 2026 bundled clang-format 22.1.3 parses the profile successfully.
- `AGENTS.md`, `docs/PROJECT.md`, and the repository skill name the checked-in profile as authoritative.
