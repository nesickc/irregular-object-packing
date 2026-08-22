#include "irop/geometry/sampling_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "irop/error.hpp"

namespace irop {
namespace {

void validate_policy(const double volume_scale, const SurfaceSamplingPolicy& policy)
{
    if (!std::isfinite(volume_scale) || volume_scale <= 0.0 || volume_scale > 1.0) {
        throw Error(ErrorCategory::invalid_configuration,
                    "surface-sampling volume scale must be finite and in the interval (0, 1]");
    }
    if (!std::isfinite(policy.alpha) || policy.alpha <= 0.0 || policy.alpha >= 1.0) {
        throw Error(ErrorCategory::invalid_configuration,
                    "surface-sampling alpha must be finite and in the interval (0, 1)");
    }
    if (!std::isfinite(policy.beta) || policy.beta <= 0.0 || policy.beta > 1.0) {
        throw Error(ErrorCategory::invalid_configuration,
                    "surface-sampling beta must be finite and in the interval (0, 1]");
    }
    if (policy.minimum_triangle_count < 4) {
        throw Error(ErrorCategory::invalid_configuration,
                    "surface-sampling minimum must support a closed triangular surface");
    }
}

}  // namespace

double surface_sampling_ratio(const double volume_scale, const SurfaceSamplingPolicy& policy)
{
    validate_policy(volume_scale, policy);
    if (volume_scale == 1.0) {
        return 1.0;
    }
    const double ratio =
        policy.alpha * std::pow(1.0 + std::pow(policy.alpha, 1.0 / policy.beta) - volume_scale, -policy.beta);
    if (!std::isfinite(ratio) || ratio <= 0.0 || ratio > 1.0) {
        throw Error(ErrorCategory::invalid_configuration, "surface-sampling policy produced an invalid ratio");
    }
    return ratio;
}

std::uint64_t target_surface_triangle_count(const std::uint64_t original_triangle_count, const double volume_scale,
                                            const SurfaceSamplingPolicy& policy)
{
    validate_policy(volume_scale, policy);
    if (original_triangle_count < policy.minimum_triangle_count) {
        throw Error(ErrorCategory::invalid_configuration,
                    "source mesh has fewer triangles than the configured safe sampling minimum");
    }
    // Match Python's binary64 multiplication before integer truncation.
    const double target = surface_sampling_ratio(volume_scale, policy) * static_cast<double>(original_triangle_count);
    const double uint64_upper_exclusive = std::ldexp(1.0, std::numeric_limits<std::uint64_t>::digits);
    if (!std::isfinite(target)) {
        throw Error(ErrorCategory::resource_limit, "surface-sampling target count is not representable");
    }
    if (target >= uint64_upper_exclusive) {
        // The policy ratio cannot exceed one. Binary64 can nevertheless round
        // UINT64_MAX up to 2^64, so clamp that endpoint before integer conversion.
        return original_triangle_count;
    }
    const auto truncated = static_cast<std::uint64_t>(target);
    // DEVIATION(IROP-DEV-0010): Never request fewer than four triangles because
    // a smaller triangular surface cannot enclose a volume safely.
    // See docs/COMPATIBILITY.md.
    return std::min(original_triangle_count, std::max(policy.minimum_triangle_count, truncated));
}

}  // namespace irop
