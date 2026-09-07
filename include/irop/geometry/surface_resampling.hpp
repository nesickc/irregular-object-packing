#pragma once

#include <cstdint>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct SurfaceResamplingLimits {
    static constexpr std::uint64_t default_max_subdivision_steps = 8;

    MeshLimits mesh_limits;
    std::uint64_t max_subdivision_steps = default_max_subdivision_steps;
};

enum class SurfaceResamplingMode {
    reference,
    preserve_surface,
};

struct SurfaceResamplingResult {
    TriangleMesh mesh;
    std::uint64_t requested_triangle_count = 0;
    std::uint64_t actual_triangle_count = 0;
    std::uint64_t subdivision_steps = 0;
    bool changed = false;
};

[[nodiscard]] double average_triangle_area(const TriangleMesh& mesh);

// Matches the Python container target policy: compare average triangle areas,
// truncate the unmultiplied target, then apply the integer refinement factor.
[[nodiscard]] std::uint64_t target_container_triangle_count(const TriangleMesh& sampled_object,
                                                            const TriangleMesh& original_container,
                                                            std::uint64_t refinement_factor = 4,
                                                            std::uint64_t minimum_triangle_count = 4);

// Validates the result as one closed surface and never exposes dependency-specific
// types. The reference mode uses pinned VTK filters privately and may move the
// surface through decimation, Loop subdivision and smoothing. Preserve-surface
// mode retains the original when the target is no larger; otherwise it splits
// triangles linearly in double precision without moving the original surface.
[[nodiscard]] SurfaceResamplingResult resample_closed_surface(
    const TriangleMesh& mesh, std::uint64_t target_triangle_count, const SurfaceResamplingLimits& limits = {},
    SurfaceResamplingMode mode = SurfaceResamplingMode::reference);

}  // namespace irop
