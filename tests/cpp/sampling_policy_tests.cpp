#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "irop/error.hpp"
#include "irop/geometry/sampling_policy.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

TEST_CASE("surface sampling policy preserves the reference endpoint and formula")
{
    const irop::SurfaceSamplingPolicy policy { .alpha = 0.1, .beta = 0.5 };
    CHECK(irop::surface_sampling_ratio(1.0, policy) == 1.0);

    const double volume_scale = 0.25;
    const double expected =
        policy.alpha * std::pow(1.0 + std::pow(policy.alpha, 1.0 / policy.beta) - volume_scale, -policy.beta);
    CHECK(irop::surface_sampling_ratio(volume_scale, policy) == Approx(expected).epsilon(1.0e-14));
    CHECK(irop::surface_sampling_ratio(0.1, policy) < irop::surface_sampling_ratio(0.5, policy));
}

TEST_CASE("target triangle count truncates and retains a closed-surface minimum")
{
    const irop::SurfaceSamplingPolicy policy { .alpha = 0.1, .beta = 0.5, .minimum_triangle_count = 4 };
    const double ratio = irop::surface_sampling_ratio(0.25, policy);
    CHECK(irop::target_surface_triangle_count(1000, 0.25, policy) == static_cast<std::uint64_t>(ratio * 1000.0));
    CHECK(irop::target_surface_triangle_count(4, 0.01, policy) == 4);
    CHECK(irop::target_surface_triangle_count(1000, 1.0, policy) == 1000);
}

TEST_CASE("target triangle count handles the maximum uint64 endpoint without an out-of-range conversion")
{
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    CHECK(irop::target_surface_triangle_count(maximum, 1.0) == maximum);
    CHECK(irop::target_surface_triangle_count(maximum, std::nextafter(1.0, 0.0)) <= maximum);
}

TEST_CASE("surface sampling policy rejects unsafe parameters")
{
    irop::test::require_error_category([]() {
        static_cast<void>(irop::surface_sampling_ratio(0.0));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([]() {
        static_cast<void>(irop::surface_sampling_ratio(std::numeric_limits<double>::quiet_NaN()));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([]() {
        static_cast<void>(irop::surface_sampling_ratio(0.5, { .alpha = 1.0 }));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([]() {
        static_cast<void>(irop::target_surface_triangle_count(3, 0.5));
    }, irop::ErrorCategory::invalid_configuration);
}

}  // namespace
