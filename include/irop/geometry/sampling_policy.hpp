#pragma once

#include <cstdint>

namespace irop {

struct SurfaceSamplingPolicy {
    double alpha = 0.05;
    double beta = 0.1;
    std::uint64_t minimum_triangle_count = 4;
};

// The scale argument is a volume scale, matching Transform::volume_scale.
[[nodiscard]] double surface_sampling_ratio(double volume_scale, const SurfaceSamplingPolicy& policy = {});

// Applies the reference truncation policy with a safe minimum for a closed
// triangular surface. Actual mesh resampling begins with the packing loop.
[[nodiscard]] std::uint64_t target_surface_triangle_count(std::uint64_t original_triangle_count, double volume_scale,
                                                          const SurfaceSamplingPolicy& policy = {});

}  // namespace irop
