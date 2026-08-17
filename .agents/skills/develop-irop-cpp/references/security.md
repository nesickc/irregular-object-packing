# Security Workflow

Use this reference for mesh/configuration input, filesystem output, resource limits, parser boundaries, dependency updates, and hardening.

The product is a local tool, but local files remain untrusted.

## Threat Model

Assume an attacker can provide:

- Truncated or contradictory ASCII/binary STL files.
- Counts designed to overflow arithmetic or trigger huge allocation.
- NaN, infinity, extreme magnitudes, degenerate triangles, and invalid indices.
- JSON with extreme nesting, sizes, values, or output paths.
- Geometry designed to create excessive tetrahedralization, collision, or solver work.

Do not assume network exposure, multi-user service isolation, or elevated execution.

## Boundary Requirements

- Check file size before parsing and reject unsupported sizes early.
- Check all count conversions and allocation arithmetic.
- Validate finite coordinates, indices, topology requirements, and configured ranges after parsing.
- Bound vertices, triangles, objects, generated tetrahedra, constraints, iterations, and elapsed work through configurable limits.
- Keep output under the resolved output directory and use safe path APIs.
- Write success markers/results only after required artifacts complete.
- Translate dependency failures without dumping unbounded input-derived content.
- Never execute commands, load libraries, or evaluate code based on input data.

## Algorithmic Denial of Service

- Add explicit limits to correction and retry loops.
- Make cancellation or timeout checks available at coarse expensive boundaries.
- Treat explosive intermediate geometry as resource exhaustion, not a reason to continue allocating.
- Preserve diagnostic summaries when safe, but do not serialize enormous failure state.

## Verification

Add compact cases for truncation, count mismatch, overflow boundary, non-finite values, invalid topology, path escape, and exhausted work limits. Use sanitizers or platform hardening checks when supported by the selected MSVC/dependency configuration, but do not make unsupported tooling a substitute for boundary validation.
