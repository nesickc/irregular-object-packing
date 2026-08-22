#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string_view>

#include "irop/error.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/model/triangle_mesh.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

TEST_CASE("mesh validation reports counts and bounds for a finite triangle mesh")
{
    const irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();

    const irop::MeshStatistics statistics = irop::validate_and_measure_mesh(mesh, {});

    CHECK(statistics.vertex_count == 4);
    CHECK(statistics.triangle_count == 4);
    CHECK(statistics.bounds.minimum.x == Approx(0.0));
    CHECK(statistics.bounds.minimum.y == Approx(0.0));
    CHECK(statistics.bounds.minimum.z == Approx(0.0));
    CHECK(statistics.bounds.maximum.x == Approx(1.0));
    CHECK(statistics.bounds.maximum.y == Approx(1.0));
    CHECK(statistics.bounds.maximum.z == Approx(1.0));
}

TEST_CASE("mesh validation enforces configured count limits before geometry checks")
{
    const irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();

    SECTION("vertex limit")
    {
        irop::MeshLimits limits;
        limits.max_vertices = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("triangle limit")
    {
        irop::MeshLimits limits;
        limits.max_triangles = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, limits));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("mesh validation rejects empty geometry")
{
    SECTION("no vertices")
    {
        irop::TriangleMesh mesh;
        mesh.triangles.push_back({ 0, 1, 2 });

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("no triangles")
    {
        irop::TriangleMesh mesh;
        mesh.vertices.push_back({ 0.0, 0.0, 0.0 });

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("mesh validation rejects unsafe coordinates and indices")
{
    SECTION("non-finite coordinate")
    {
        irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
        mesh.vertices[0].x = std::numeric_limits<double>::quiet_NaN();

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("out-of-range index")
    {
        irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
        mesh.triangles[0][2] = 99;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("repeated index")
    {
        irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
        mesh.triangles[0] = { 0, 0, 1 };

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("mesh validation rejects zero-area and numerically unsafe triangles")
{
    SECTION("collinear vertices")
    {
        irop::TriangleMesh mesh {
            .vertices = { { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 } },
            .triangles = { { 0, 1, 2 } },
        };

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("finite coordinates overflow derived area arithmetic")
    {
        constexpr double huge = std::numeric_limits<double>::max() / 2.0;
        irop::TriangleMesh mesh {
            .vertices = { { huge, 0.0, 0.0 }, { -huge, huge, 0.0 }, { -huge, 0.0, huge } },
            .triangles = { { 0, 1, 2 } },
        };

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("error category names are stable machine-readable values")
{
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::invalid_configuration)) == "invalid_configuration");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::input_io)) == "input_io");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::resource_limit)) == "resource_limit");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::invalid_mesh)) == "invalid_mesh");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::output_io)) == "output_io");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::dependency_failure)) == "dependency_failure");
    CHECK(std::string_view(irop::to_string(irop::ErrorCategory::internal)) == "internal");
}

}  // namespace
