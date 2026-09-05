#pragma once

#include <cstdint>
#include <filesystem>

#include "irop/optimization/local_solver.hpp"

namespace irop {

struct LocalSolveSnapshotReadLimits {
    std::uint64_t max_file_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t max_constraints = 100'000;
    // Saved limits are never silently clamped. Explicit experiment callers may
    // supply a larger bounded work envelope; all adapter checks remain active.
    LocalSolveLimits solve_limits { .max_iterations = 1'000 };
};

// Writes the bounded, numeric-only version-one prepared local problem. Caller
// owns its destination transaction; this diagnostic is not a packing result.
void write_local_solve_snapshot(const std::filesystem::path& path, const LocalSolveSnapshot& snapshot);

// File/depth/node/constraint and saved work limits are checked before replay.
// Original meshes, paths, RNG and earlier object solves are not needed.
[[nodiscard]] LocalSolveSnapshot read_local_solve_snapshot(const std::filesystem::path& path,
                                                           const LocalSolveSnapshotReadLimits& limits = {});

}  // namespace irop
