#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <numbers>

#include "irop/error.hpp"
#include "irop/geometry/transform.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

void check_point(const irop::Point3& point, const double x, const double y, const double z)
{
    CHECK(point.x == Approx(x).margin(1.0e-12));
    CHECK(point.y == Approx(y).margin(1.0e-12));
    CHECK(point.z == Approx(z).margin(1.0e-12));
}

TEST_CASE("identity transform preserves points")
{
    const irop::Matrix4 identity = irop::identity_matrix();
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            CHECK(identity(row, column) == (row == column ? 1.0 : 0.0));
        }
    }
    check_point(irop::transform_point(irop::matrix_for({}), { 1.25, -2.0, 3.5 }), 1.25, -2.0, 3.5);
}

TEST_CASE("volume scale uses its cube root as the linear scale")
{
    const irop::Transform transform { .volume_scale = 8.0 };
    check_point(irop::transform_point(irop::matrix_for(transform), { 1.0, -2.0, 0.5 }), 2.0, -4.0, 1.0);
}

TEST_CASE("rotation uses radians and the Python Ry Rz Rx order")
{
    const irop::Transform z_rotation {
        .rotation = { .z = std::numbers::pi_v<double> / 2.0 },
    };
    check_point(irop::transform_point(irop::matrix_for(z_rotation), { 1.0, 0.0, 0.0 }), 0.0, 1.0, 0.0);

    const irop::Transform order_lock {
        .rotation = {
            .x = 0.0,
            .y = std::numbers::pi_v<double> / 2.0,
            .z = std::numbers::pi_v<double> / 2.0,
        },
    };
    check_point(irop::transform_point(irop::matrix_for(order_lock), { 1.0, 0.0, 0.0 }), 0.0, 1.0, 0.0);
}

TEST_CASE("translation is applied after rotation and volume scaling")
{
    const irop::Transform transform {
        .volume_scale = 8.0,
        .rotation = { .z = std::numbers::pi_v<double> / 2.0 },
        .translation = { 1.0, 2.0, 3.0 },
    };
    check_point(irop::transform_point(irop::matrix_for(transform), { 1.0, 0.0, 0.0 }), 1.0, 4.0, 3.0);
}

TEST_CASE("matrix composition matches sequential application")
{
    const irop::Matrix4 before = irop::matrix_for({ .volume_scale = 8.0 });
    const irop::Matrix4 after = irop::matrix_for({
        .translation = { 3.0, -4.0, 5.0 }
    });
    const irop::Point3 input { -1.0, 2.0, 0.5 };
    const irop::Point3 sequential = irop::transform_point(after, irop::transform_point(before, input));
    const irop::Point3 composed = irop::transform_point(irop::compose(after, before), input);
    check_point(composed, sequential.x, sequential.y, sequential.z);
}

TEST_CASE("transform rejects invalid scale and non-finite parameters")
{
    irop::test::require_error_category([]() {
        static_cast<void>(irop::matrix_for({ .volume_scale = 0.0 }));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([]() {
        static_cast<void>(irop::matrix_for({
            .translation = { std::numeric_limits<double>::infinity(), 0.0, 0.0 },
        }));
    }, irop::ErrorCategory::invalid_configuration);
}

TEST_CASE("transform rejects non-finite coordinates produced by homogeneous division")
{
    irop::Matrix4 matrix = irop::identity_matrix();
    matrix.values[0] = std::numeric_limits<double>::max();
    matrix.values[15] = 0.5;

    irop::test::require_error_category([&matrix]() {
        static_cast<void>(irop::transform_point(matrix, { 1.0, 0.0, 0.0 }));
    }, irop::ErrorCategory::invalid_configuration);
}

}  // namespace
