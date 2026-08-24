#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/surface_resampling.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

TEST_CASE("average triangle area validates geometry and uses all triangles")
{
    CHECK(irop::average_triangle_area(irop::test::cube_mesh(1.0)) == Approx(2.0));
    CHECK(irop::average_triangle_area(irop::test::tetrahedron_mesh()) == Approx((1.5 + std::sqrt(3.0) * 0.5) / 4.0));

    irop::TriangleMesh invalid = irop::test::tetrahedron_mesh();
    invalid.vertices.front().x = std::numeric_limits<double>::quiet_NaN();
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::average_triangle_area(invalid));
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("container triangle target truncates before applying the refinement factor")
{
    const irop::TriangleMesh sampled_object = irop::test::cube_mesh(1.0);
    const irop::TriangleMesh container = irop::test::cube_mesh(std::sqrt(1.1));

    CHECK(irop::target_container_triangle_count(sampled_object, container) == 52);
    CHECK(irop::target_container_triangle_count(sampled_object, irop::test::cube_mesh(0.1)) == 4);
    CHECK(irop::target_container_triangle_count(sampled_object, container, 2, 64) == 64);
}

TEST_CASE("container triangle target validates factor minimum and overflow")
{
    const irop::TriangleMesh cube = irop::test::cube_mesh(1.0);

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::target_container_triangle_count(cube, cube, 0));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::target_container_triangle_count(cube, cube, 1, 3));
    }, irop::ErrorCategory::invalid_configuration);
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::target_container_triangle_count(cube, cube, std::numeric_limits<std::uint64_t>::max()));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("equal-count closed surface resampling preserves the source exactly")
{
    const irop::TriangleMesh source = irop::test::cube_mesh(1.0);
    const irop::SurfaceResamplingResult result = irop::resample_closed_surface(source, 12);

    CHECK_FALSE(result.changed);
    CHECK(result.requested_triangle_count == 12);
    CHECK(result.actual_triangle_count == 12);
    CHECK(result.subdivision_steps == 0);
    CHECK(result.mesh.triangles == source.triangles);
    REQUIRE(result.mesh.vertices.size() == source.vertices.size());
    CHECK(result.mesh.vertices.front().x == source.vertices.front().x);
    CHECK(result.mesh.vertices.front().y == source.vertices.front().y);
    CHECK(result.mesh.vertices.front().z == source.vertices.front().z);
}

TEST_CASE("Loop subdivision reaches the requested count in bounded powers of four")
{
    const irop::TriangleMesh source = irop::test::tetrahedron_mesh();
    const irop::SurfaceResamplingResult result = irop::resample_closed_surface(source, 5);

    CHECK(result.changed);
    CHECK(result.requested_triangle_count == 5);
    CHECK(result.actual_triangle_count == 16);
    CHECK(result.subdivision_steps == 1);
    CHECK(result.mesh.vertices.size() == 10);
    CHECK(irop::ClosedMeshQuery(result.mesh).volume() > 0.0);
    CHECK(source.triangles.size() == 4);
    CHECK(source.vertices[1].x == 1.0);
}

TEST_CASE("quadric decimation reduces and preserves a closed surface")
{
    const irop::TriangleMesh source = irop::test::cube_mesh(1.0);
    const irop::SurfaceResamplingResult result = irop::resample_closed_surface(source, 8);

    CHECK(result.changed);
    CHECK(result.requested_triangle_count == 8);
    CHECK(result.actual_triangle_count >= 4);
    CHECK(result.actual_triangle_count < source.triangles.size());
    CHECK(result.subdivision_steps == 0);
    CHECK(irop::ClosedMeshQuery(result.mesh).volume() > 0.0);
    CHECK(source.triangles.size() == 12);
}

TEST_CASE("surface resampling preflights subdivision growth and step limits")
{
    const irop::TriangleMesh tetrahedron = irop::test::tetrahedron_mesh();

    SECTION("subdivision step limit")
    {
        irop::SurfaceResamplingLimits limits;
        limits.max_subdivision_steps = 1;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 17, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("intermediate vertex count")
    {
        irop::SurfaceResamplingLimits limits;
        limits.mesh_limits.max_vertices = 9;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 5, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("intermediate triangle count")
    {
        irop::SurfaceResamplingLimits limits;
        limits.mesh_limits.max_triangles = 15;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 5, limits));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("surface resampling rejects unsafe targets inputs and limits")
{
    const irop::TriangleMesh tetrahedron = irop::test::tetrahedron_mesh();

    SECTION("target below a closed triangular surface minimum")
    {
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 3));
        }, irop::ErrorCategory::invalid_configuration);
    }

    SECTION("target beyond configured output limit")
    {
        irop::SurfaceResamplingLimits limits;
        limits.mesh_limits.max_triangles = 8;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 9, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("zero subdivision limit")
    {
        irop::SurfaceResamplingLimits limits;
        limits.max_subdivision_steps = 0;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(tetrahedron, 4, limits));
        }, irop::ErrorCategory::invalid_configuration);
    }

    SECTION("open input surface")
    {
        irop::TriangleMesh open = tetrahedron;
        open.triangles.pop_back();
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::resample_closed_surface(open, 4));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

}  // namespace
