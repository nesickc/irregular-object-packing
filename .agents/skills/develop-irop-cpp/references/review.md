# Review Workflow

Use this reference for C++ implementation, CMake, vcpkg, test, security, or performance reviews.

## Review Order

1. Behavioral correctness against the active milestone and Python reference.
2. Undocumented compatibility preservation or deviation.
3. Memory safety, integer safety, resource limits, and termination.
4. Ownership, lifetime, invalidation, and concurrency.
5. Module direction and leakage of dependency-specific types.
6. Numerical stability, scale semantics, tolerances, and coordinate spaces.
7. Error translation and misleading success states.
8. Test relevance and missing high-risk cases.
9. Measured performance regressions in important paths.
10. Style and maintainability issues that materially affect future work.

## Compatibility Questions

- Does surprising code have a cataloged marker?
- Does a correction have a deviation record and focused test?
- Did the change alter rotation, translation, scale, correction selection, convergence, or output semantics?
- Is a Python defect being copied merely because it exists, or corrected without considering downstream effects?

## Architecture Questions

- Is behavior in `irop_core` rather than the CLI?
- Are VTK, TetGen, Ipopt, JSON, and filesystem concerns contained at their intended boundaries?
- Is a new interface justified by a substantial boundary or multiple real implementations?
- Is state explicit and instance-owned?
- Is the change smaller and clearer than the abstraction introduced to support it?

## Evidence

Require proportionate evidence. Do not accept “tests pass” without knowing which tests ran. Do not require broad test expansion for a narrow documentation or mechanical formatting change.

Check that `docs/STATUS.md` reflects actual implementation and verification. Request an ADR only for durable, project-wide decisions.
