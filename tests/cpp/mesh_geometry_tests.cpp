#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/model/mesh_validation.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

TEST_CASE("closed mesh query measures volume bounds containment and distance")
{
    const irop::TriangleMesh cube = irop::test::cube_mesh(5.0);
    const irop::ClosedMeshQuery query(cube);

    CHECK(query.volume() == Approx(1000.0));
    CHECK(query.bounds().minimum.x == -5.0);
    CHECK(query.bounds().minimum.y == -5.0);
    CHECK(query.bounds().minimum.z == -5.0);
    CHECK(query.bounds().maximum.x == 5.0);
    CHECK(query.bounds().maximum.y == 5.0);
    CHECK(query.bounds().maximum.z == 5.0);
    CHECK(query.contains({ 0.0, 0.0, 0.0 }));
    CHECK_FALSE(query.contains({ 6.0, 0.0, 0.0 }));
    CHECK(query.distance_to_surface({ 0.0, 0.0, 0.0 }) == Approx(5.0));
    CHECK(query.distance_to_surface({ 6.0, 0.0, 0.0 }) == Approx(1.0));
}

TEST_CASE("tetrahedron enclosed volume is one sixth")
{
    CHECK(irop::ClosedMeshQuery(irop::test::tetrahedron_mesh()).volume() == Approx(1.0 / 6.0));
}

TEST_CASE("closed mesh query owns its validated snapshot")
{
    irop::TriangleMesh source = irop::test::cube_mesh(1.0);
    const irop::ClosedMeshQuery query(source);
    source.vertices.clear();
    source.triangles.clear();
    CHECK(query.contains({ 0.0, 0.0, 0.0 }));
    CHECK(query.volume() == Approx(8.0));
}

TEST_CASE("closed mesh query normalizes local and global winding without mutating input")
{
    irop::TriangleMesh local_flip = irop::test::tetrahedron_mesh();
    std::swap(local_flip.triangles.front()[1], local_flip.triangles.front()[2]);
    const irop::Triangle original_face = local_flip.triangles.front();
    const irop::ClosedMeshQuery local_query(local_flip);
    CHECK(local_query.volume() == Approx(1.0 / 6.0));
    CHECK(local_query.contains({ 0.1, 0.1, 0.1 }));
    CHECK(local_flip.triangles.front() == original_face);

    irop::TriangleMesh global_flip = irop::test::tetrahedron_mesh();
    for (irop::Triangle& triangle : global_flip.triangles) {
        std::swap(triangle[1], triangle[2]);
    }
    const irop::ClosedMeshQuery global_query(global_flip);
    CHECK(global_query.volume() == Approx(1.0 / 6.0));
    CHECK(global_query.contains({ 0.1, 0.1, 0.1 }));
}

TEST_CASE("closed mesh query rejects ambiguous multiple surface components")
{
    const irop::TriangleMesh shells = irop::combine_meshes({ irop::test::cube_mesh(2.0), irop::test::cube_mesh(1.0) });
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::ClosedMeshQuery(shells));
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("surface intersection query distinguishes separation crossing and contact")
{
    const irop::TriangleMesh outer = irop::test::cube_mesh(2.0);
    const irop::TriangleMesh inner = irop::test::cube_mesh(0.5);
    const irop::SurfaceIntersectionResult separated = irop::query_surface_intersection(outer, inner, 1'000);
    CHECK_FALSE(separated.intersects);
    CHECK(separated.tested_triangle_pairs == 144);

    irop::TriangleMesh crossing = irop::test::cube_mesh(1.0);
    for (irop::Point3& point : crossing.vertices) {
        point.x += 1.5;
    }
    CHECK(irop::query_surface_intersection(irop::test::cube_mesh(1.0), crossing, 1'000).intersects);

    irop::TriangleMesh touching = irop::test::cube_mesh(1.0);
    for (irop::Point3& point : touching.vertices) {
        point.x += 2.0;
    }
    CHECK(irop::query_surface_intersection(irop::test::cube_mesh(1.0), touching, 1'000).intersects);
}

TEST_CASE("surface intersection query enforces its triangle-pair budget")
{
    irop::test::require_error_category([]() {
        static_cast<void>(irop::query_surface_intersection(irop::test::cube_mesh(2.0), irop::test::cube_mesh(0.5), 1));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("centering preserves Python vertex-centroid semantics without mutating the source")
{
    irop::TriangleMesh source = irop::test::tetrahedron_mesh();
    for (irop::Point3& point : source.vertices) {
        point.x += 7.0;
        point.y -= 3.0;
        point.z += 2.0;
    }
    const irop::TriangleMesh original = source;

    const irop::CenteredMesh centered = irop::center_mesh_at_vertex_centroid(source);
    const irop::Point3 centroid = irop::vertex_centroid(centered.mesh);

    CHECK(centered.original_vertex_centroid.x == Approx(7.25));
    CHECK(centered.original_vertex_centroid.y == Approx(-2.75));
    CHECK(centered.original_vertex_centroid.z == Approx(2.25));
    CHECK(centroid.x == Approx(0.0).margin(1.0e-14));
    CHECK(centroid.y == Approx(0.0).margin(1.0e-14));
    CHECK(centroid.z == Approx(0.0).margin(1.0e-14));
    CHECK(source.vertices.front().x == original.vertices.front().x);
    CHECK(source.vertices.front().y == original.vertices.front().y);
    CHECK(source.vertices.front().z == original.vertices.front().z);
}

TEST_CASE("mesh scaling reaches the requested enclosed volume")
{
    const irop::TriangleMesh tetrahedron = irop::test::tetrahedron_mesh();
    const irop::TriangleMesh scaled = irop::scale_mesh_to_volume(tetrahedron, 8.0 / 6.0);
    CHECK(irop::ClosedMeshQuery(scaled).volume() == Approx(8.0 / 6.0).epsilon(1.0e-12));
    CHECK(scaled.vertices[1].x == Approx(2.0));
}

TEST_CASE("maximum radius uses the supplied center")
{
    const irop::TriangleMesh cube = irop::test::cube_mesh(1.0);
    CHECK(irop::maximum_radius(cube, {}) == Approx(std::sqrt(3.0)));
}

TEST_CASE("mesh combination remaps indices and enforces combined limits")
{
    const irop::TriangleMesh tetrahedron = irop::test::tetrahedron_mesh();
    const irop::TriangleMesh combined = irop::combine_meshes({ tetrahedron, tetrahedron });
    CHECK(combined.vertices.size() == 8);
    CHECK(combined.triangles.size() == 8);
    CHECK(combined.triangles[4][0] == 4);

    irop::MeshLimits limits;
    limits.max_vertices = 7;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::combine_meshes({ tetrahedron, tetrahedron }, limits));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("closed mesh query rejects open surfaces")
{
    irop::TriangleMesh open = irop::test::tetrahedron_mesh();
    open.triangles.pop_back();
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::ClosedMeshQuery(open));
    }, irop::ErrorCategory::invalid_mesh);
}

}  // namespace
