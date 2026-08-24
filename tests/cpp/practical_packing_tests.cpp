#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "irop/geometry/collision.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/packing.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

[[nodiscard]] irop::TriangleMesh centered(irop::TriangleMesh mesh)
{
    return irop::center_mesh_at_vertex_centroid(mesh).mesh;
}

[[nodiscard]] irop::TriangleMesh placed(const irop::TriangleMesh& mesh, const irop::Transform& transform)
{
    return irop::transform_mesh(mesh, transform);
}

void require_success_at_target(const irop::PackingResult& result, const double target)
{
    INFO(result.diagnostic);
    REQUIRE(result.status == irop::PackingStatus::success);
    REQUIRE(result.final_validation_performed);
    CHECK(result.final_validation.physical_scene_valid());
    REQUIRE_FALSE(result.state.transforms.empty());
    for (const irop::Transform& transform : result.state.transforms) {
        CHECK(transform.volume_scale == Approx(target).epsilon(1.0e-12));
    }
}

[[nodiscard]] irop::TriangleMesh packing_cylinder(const bool full_scale_fixture)
{
    constexpr std::size_t segment_count = 12;
    irop::TriangleMesh mesh = irop::test::cylinder_mesh(0.9, 1.055, segment_count);
    // Exact rotational symmetry makes this prism's co-spherical TetGen input
    // degenerate; deterministic perturbations retain a closed cylinder analog.
    for (std::size_t index = 0; index < segment_count; ++index) {
        const double radial_factor = 1.0 + 0.0025 * static_cast<double>(index);
        for (const std::size_t ring_offset : { std::size_t { 0 }, segment_count }) {
            irop::Point3& point = mesh.vertices[ring_offset + index];
            point.x *= radial_factor;
            point.y *= radial_factor;
        }
        if (full_scale_fixture) {
            const double axial_offset = 0.0001 * static_cast<double>(index * index);
            mesh.vertices[index].z -= axial_offset;
            mesh.vertices[segment_count + index].z += axial_offset;
        }
    }
    return centered(std::move(mesh));
}

[[nodiscard]] irop::TriangleMesh packing_container(const bool full_scale_fixture)
{
    irop::TriangleMesh mesh = irop::test::box_mesh(3.5, 4.0, 2.85);
    if (full_scale_fixture) {
        // A slight convex shear removes the exact co-spherical corner set of an
        // orthogonal box while preserving an obvious, roomy straight-sided volume.
        for (irop::Point3& point : mesh.vertices) {
            const double original_x = point.x;
            point.x += 0.04 * point.z;
            point.y += 0.025 * original_x;
        }
    }
    return mesh;
}

[[nodiscard]] irop::PackingResult pack_two_cylinders(const double initial_scale, const double final_scale,
                                                     const double maximum_rotation,
                                                     const std::uint64_t maximum_solver_iterations,
                                                     const bool full_scale_fixture = false)
{
    const irop::TriangleMesh object = packing_cylinder(full_scale_fixture);
    const irop::TriangleMesh container = packing_container(full_scale_fixture);

    irop::PackingConfig initialization;
    initialization.object_count = 2;
    initialization.initial_volume_scale = initial_scale;
    initialization.seed = 1918;
    irop::PackingState state = irop::initialize_packing(object, container, initialization);

    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = final_scale;
    algorithm.scale_step_count = 1;
    algorithm.max_iterations_per_scale_step = 4;
    algorithm.maximum_rotation_delta_radians = maximum_rotation;
    algorithm.adaptive_sampling = false;

    irop::PackingEngineLimits limits;
    limits.local_solve.max_iterations = maximum_solver_iterations;
    limits.local_solve.max_elapsed_time = std::chrono::seconds(30);
    return irop::run_packing(object, container, std::move(state), algorithm, limits);
}

[[nodiscard]] irop::PackingResult pack_two_tetrahedra(const double initial_scale, const double final_scale,
                                                      const std::uint64_t maximum_solver_iterations)
{
    const irop::TriangleMesh object = centered(irop::test::tetrahedron_mesh());
    const irop::TriangleMesh container = irop::test::box_mesh(3.0, 3.5, 4.0);

    irop::PackingConfig initialization;
    initialization.object_count = 2;
    initialization.initial_volume_scale = initial_scale;
    initialization.seed = 1918;
    irop::PackingState state = irop::initialize_packing(object, container, initialization);

    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = final_scale;
    algorithm.scale_step_count = 1;
    algorithm.max_iterations_per_scale_step = 4;
    algorithm.maximum_rotation_delta_radians = 0.0;
    algorithm.adaptive_sampling = false;

    irop::PackingEngineLimits limits;
    limits.local_solve.max_iterations = maximum_solver_iterations;
    limits.local_solve.max_elapsed_time = std::chrono::seconds(30);
    return irop::run_packing(object, container, std::move(state), algorithm, limits);
}

TEST_CASE("generated primitive geometry has obvious fit and non-fit outcomes", "[packing][geometry]")
{
    const irop::TriangleMesh container = irop::test::box_mesh(3.0, 3.0, 3.0);

    SECTION("cube inside a larger cube")
    {
        const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(1.0) };
        CHECK(irop::validate_scene_collisions(objects, container).physical_scene_valid());
    }

    SECTION("faceted cylinder inside a box")
    {
        const std::vector<irop::TriangleMesh> objects { irop::test::cylinder_mesh(0.75, 1.5, 12) };
        CHECK(irop::validate_scene_collisions(objects, container).physical_scene_valid());
    }

    SECTION("two separated cylinders inside a box")
    {
        const irop::TriangleMesh cylinder = irop::test::cylinder_mesh(0.6, 1.0, 12);
        const std::vector<irop::TriangleMesh> objects {
            placed(cylinder, { .translation = { -1.2, 0.0, 0.0 } }
             ),
            placed(cylinder, { .translation = { 1.2, 0.0, 0.0 }  }
             ),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
        CHECK(report.physical_scene_valid());
        CHECK(report.object_collisions.empty());
    }

    SECTION("oversized cube cannot fit")
    {
        const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(3.1) };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
        CHECK_FALSE(report.physical_scene_valid());
        CHECK(report.container_violation_object_ids == std::vector<std::uint64_t> { 0 });
    }
}

TEST_CASE("generated rod fits its narrow box only after the known rotation", "[packing][geometry][rotation]")
{
    constexpr double required_rotation = 0.2;
    const irop::TriangleMesh rod = irop::test::box_mesh(2.0, 0.5, 0.5);
    const irop::TriangleMesh container =
        placed(irop::test::box_mesh(2.2, 0.65, 0.75), { .rotation = { .z = required_rotation } });

    const std::vector<irop::TriangleMesh> unrotated { rod };
    const irop::SceneCollisionReport unrotated_report = irop::validate_scene_collisions(unrotated, container);
    CHECK_FALSE(unrotated_report.physical_scene_valid());
    CHECK(unrotated_report.container_violation_object_ids == std::vector<std::uint64_t> { 0 });

    const std::vector<irop::TriangleMesh> aligned {
        placed(rod, { .rotation = { .z = required_rotation } }),
    };
    CHECK(irop::validate_scene_collisions(aligned, container).physical_scene_valid());
}

TEST_CASE("two generated cylinders grow with local rotation enabled",
          "[practical-packing][packing][integration][practical]")
{
    const irop::PackingResult result =
        pack_two_cylinders(0.1, 0.1001, irop::maximum_local_solve_rotation_delta_radians / 12.0, 500);

    require_success_at_target(result, 0.1001);
    CHECK(result.work.completed_scale_steps == 1);
    CHECK(result.work.local_solves >= 2);
    CHECK(result.work.local_solves <= 8);
    REQUIRE(result.work.local_solve.minimum_solver_constraint.has_value());
    REQUIRE(result.work.local_solve.minimum_applied_constraint.has_value());
    CHECK(*result.work.local_solve.minimum_solver_constraint >= -1.0e-5);
    CHECK(*result.work.local_solve.minimum_applied_constraint >= -1.0e-5);
}

TEST_CASE("two generated full-size cylinders reach the barrier with bounded local work",
          "[practical-packing][packing][integration]")
{
    const irop::PackingResult result = pack_two_cylinders(0.999, 1.0, 0.0, 200, true);

    require_success_at_target(result, 1.0);
    CHECK(result.work.completed_scale_steps == 1);
    CHECK(result.work.local_solves >= 2);
    CHECK(result.work.local_solves <= 8);
}

TEST_CASE("two generated full-size tetrahedra reach the barrier with bounded local work",
          "[practical-packing][packing][integration]")
{
    const irop::PackingResult result = pack_two_tetrahedra(0.999, 1.0, 200);

    require_success_at_target(result, 1.0);
    CHECK(result.work.completed_scale_steps == 1);
    CHECK(result.work.local_solves >= 2);
    CHECK(result.work.local_solves <= 8);
}

TEST_CASE("generated rod packing can use rotation to reach its known feasible orientation",
          "[practical-packing][packing][integration][rotation]")
{
    constexpr double required_rotation = 0.2;
    const irop::TriangleMesh object = centered(irop::test::box_mesh(2.0, 0.5, 0.5));
    const irop::TriangleMesh container =
        placed(irop::test::box_mesh(2.2, 0.65, 0.75), { .rotation = { .z = required_rotation } });

    irop::PackingConfig initialization;
    initialization.initial_volume_scale = 0.1;
    initialization.seed = 1918;
    irop::PackingState state = irop::initialize_packing(object, container, initialization);

    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = 1.0;
    algorithm.scale_step_count = 9;
    algorithm.max_iterations_per_scale_step = 10;
    algorithm.maximum_rotation_delta_radians = 0.3;
    algorithm.adaptive_sampling = false;

    irop::PackingEngineLimits limits;
    limits.local_solve.max_iterations = 500;
    limits.local_solve.max_elapsed_time = std::chrono::seconds(30);

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), algorithm, limits);
    require_success_at_target(result, 1.0);
}

}  // namespace
