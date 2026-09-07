#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "irop/geometry/collision.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/pack_scene.hpp"
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

void require_same_transforms(const std::vector<irop::Transform>& actual, const std::vector<irop::Transform>& expected)
{
    REQUIRE(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        CAPTURE(index);
        CHECK(actual[index].volume_scale == expected[index].volume_scale);
        CHECK(actual[index].rotation.x == expected[index].rotation.x);
        CHECK(actual[index].rotation.y == expected[index].rotation.y);
        CHECK(actual[index].rotation.z == expected[index].rotation.z);
        CHECK(actual[index].translation.x == expected[index].translation.x);
        CHECK(actual[index].translation.y == expected[index].translation.y);
        CHECK(actual[index].translation.z == expected[index].translation.z);
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

TEST_CASE("reference TetGen degeneracy recovery is bounded and observable",
          "[practical-packing][packing][integration][recovery]")
{
    const irop::TriangleMesh object = centered(irop::test::cylinder_mesh(0.9, 1.055, 12));
    const irop::TriangleMesh container = irop::test::box_mesh(3.5, 4.0, 2.85);

    irop::PackingConfig initialization;
    initialization.object_count = 2;
    initialization.initial_volume_scale = 0.999;
    initialization.seed = 1918;
    irop::PackingState state = irop::initialize_packing(object, container, initialization);
    const std::vector<irop::Transform> initial_transforms = state.transforms;

    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = 1.0;
    algorithm.scale_step_count = 1;
    algorithm.max_iterations_per_scale_step = 3;
    algorithm.maximum_rotation_delta_radians = 0.0;
    algorithm.adaptive_sampling = false;
    algorithm.use_reference_growth_policy = true;

    std::vector<irop::PackingProgressPhase> phases;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&phases](const irop::PackingProgress& progress) {
        phases.push_back(progress.phase);
    };

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), algorithm, {}, callbacks);

    INFO(result.diagnostic);
    CHECK(result.status == irop::PackingStatus::dependency_failure);
    CHECK(result.work.tetrahedralization_attempts == 3);
    CHECK(result.work.tetrahedralization_recoveries == 2);
    CHECK(result.work.local_solves == 0);
    const double expected_scale = initialization.initial_volume_scale *
                                  algorithm.tetrahedralization_recovery_scale_factor *
                                  algorithm.tetrahedralization_recovery_scale_factor;
    REQUIRE(result.state.transforms.size() == initial_transforms.size());
    for (std::size_t index = 0; index < result.state.transforms.size(); ++index) {
        const irop::Transform& transform = result.state.transforms[index];
        const irop::Transform& initial = initial_transforms[index];
        CHECK(transform.volume_scale == Approx(expected_scale).epsilon(1.0e-15));
        CHECK(transform.rotation.x == initial.rotation.x);
        CHECK(transform.rotation.y == initial.rotation.y);
        CHECK(transform.rotation.z == initial.rotation.z);
        CHECK(transform.translation.x == initial.translation.x);
        CHECK(transform.translation.y == initial.translation.y);
        CHECK(transform.translation.z == initial.translation.z);
    }
    REQUIRE(phases.size() == 4);
    CHECK(phases[0] == irop::PackingProgressPhase::scale_step_started);
    CHECK(phases[1] == irop::PackingProgressPhase::tetrahedralization_recovery);
    CHECK(phases[2] == irop::PackingProgressPhase::tetrahedralization_recovery);
    CHECK(phases[3] == irop::PackingProgressPhase::finished);
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

TEST_CASE("five generated full-size cylinders already at the barrier bypass packing iterations",
          "[practical-packing][packing][integration][no-growth]")
{
    const irop::TriangleMesh object = centered(irop::test::cylinder_mesh(0.9, 1.055, 12));
    const irop::TriangleMesh container = irop::test::box_mesh(6.0, 6.0, 6.0);

    irop::PackingConfig initialization;
    initialization.object_count = 5;
    initialization.initial_volume_scale = 1.0;
    initialization.seed = 1918;
    irop::PackingState state = irop::initialize_packing(object, container, initialization);
    const std::vector<irop::Transform> initial_transforms = state.transforms;
    const std::uint64_t initial_draw_count = state.random_state.draw_count();

    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = 1.0;
    algorithm.scale_step_count = 1;
    algorithm.adaptive_sampling = false;

    irop::PackingEngineLimits limits;
    // This valid but insufficient budget makes any accidental TetGen call fail.
    limits.tetrahedralization.max_input_points = 1;

    std::vector<irop::PackingProgressPhase> phases;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&phases](const irop::PackingProgress& progress) {
        phases.push_back(progress.phase);
    };

    const irop::PackingResult result =
        irop::run_packing(object, container, std::move(state), algorithm, limits, callbacks);

    require_success_at_target(result, 1.0);
    require_same_transforms(result.state.transforms, initial_transforms);
    CHECK(result.state.random_state.draw_count() == initial_draw_count);
    CHECK(result.work.completed_scale_steps == 1);
    CHECK(result.work.iterations == 0);
    CHECK(result.work.resampling_operations == 0);
    CHECK(result.work.tetrahedralization_attempts == 0);
    CHECK(result.work.tetrahedralization_recoveries == 0);
    CHECK(result.work.cat_builds == 0);
    CHECK(result.work.local_solves == 0);
    CHECK(result.work.correction_passes == 0);
    CHECK(result.final_validation.work.object_pairs_examined == 10);
    CHECK(result.history.empty());
    REQUIRE(phases.size() == 2);
    CHECK(phases[0] == irop::PackingProgressPhase::scale_step_started);
    CHECK(phases[1] == irop::PackingProgressPhase::finished);
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

[[nodiscard]] irop::PackSceneResult pack_generated_growth_scene(const irop::TriangleMesh& object,
                                                                const irop::TriangleMesh& container,
                                                                const std::filesystem::path& directory,
                                                                const std::uint32_t seed, const std::uint64_t count,
                                                                const std::uint64_t iterations_per_scale_step = 30)
{
    const auto object_path = directory / "object.stl";
    const auto container_path = directory / "container.stl";
    irop::write_stl(object_path, object);
    irop::write_stl(container_path, container);
    irop::PackOptions options;
    options.initialization.object_count = count;
    options.initialization.initial_volume_scale = 0.1;
    options.initialization.seed = seed;
    options.initialization.enable_structured_fallback = false;
    options.algorithm.final_volume_scale = 1.0;
    options.algorithm.scale_step_count = 9;
    options.algorithm.max_iterations_per_scale_step = iterations_per_scale_step;
    options.algorithm.adaptive_sampling = false;
    options.limits.max_elapsed_time = std::chrono::seconds(30);
    options.write_individual_objects = true;
    return irop::pack_scene(object_path, container_path, directory / "result", options);
}

void require_generated_serialized_growth(const irop::PackSceneResult& result, const std::uint64_t count)
{
    INFO(result.packing.diagnostic);
    CAPTURE(irop::to_string(result.packing.status), result.packing.work.local_solves,
            result.packing.work.local_solve.iterations, result.packing.work.elapsed_time.count());
    require_success_at_target(result.packing, 1.0);
    REQUIRE(result.packing.state.transforms.size() == count);
    for (const auto& transform : result.packing.state.transforms) {
        CHECK(transform.volume_scale == 1.0);
    }
    CHECK(result.packing.state.config.initial_volume_scale == 0.1);
    CHECK(result.packing.work.local_solves > 0);
    REQUIRE(result.packed_objects_path.has_value());
    REQUIRE(result.container_output_path.has_value());
    REQUIRE(result.placements_path.has_value());
    REQUIRE(result.individual_object_paths.size() == count);
    std::vector<irop::TriangleMesh> published_objects;
    for (const auto& path : result.individual_object_paths) {
        const auto loaded = irop::read_stl(path, {});
        CHECK(loaded.encoding == irop::StlEncoding::binary);
        published_objects.push_back(loaded.mesh);
    }
    const auto published_container = irop::read_stl(*result.container_output_path, {}).mesh;
    CHECK(irop::validate_scene_collisions(published_objects, published_container).physical_scene_valid());
}

TEST_CASE("two asymmetric generated cylinders grow fully and publish valid geometry across seeds",
          "[practical-packing][packing][integration][growth-corpus]")
{
    const auto object = packing_cylinder(true);
    const auto container = packing_container(true);
    const std::vector<irop::TriangleMesh> known_fit {
        placed(object, { .translation = { -1.5, 0.0, 0.0 } }
         ),
        placed(object, { .translation = { 1.5, 0.0, 0.0 }  }
         ),
    };
    REQUIRE(irop::validate_scene_collisions(known_fit, container).physical_scene_valid());
    for (const std::uint32_t seed : { 0U, 1918U, 12345U }) {
        DYNAMIC_SECTION("seed " << seed)
        {
            irop::test::TempDirectory temporary;
            const auto result = pack_generated_growth_scene(object, container, temporary.path(), seed, 2);
            require_generated_serialized_growth(result, 2);
        }
    }
}

TEST_CASE("two slender generated objects grow fully and publish valid geometry across seeds",
          "[practical-packing][packing][integration][growth-corpus]")
{
    auto object = packing_cylinder(true);
    for (auto& point : object.vertices) {
        point.x *= 0.3;
        point.y *= 0.3;
        point.z *= 1.8;
    }
    const auto container = packing_container(true);
    const std::vector<irop::TriangleMesh> known_fit {
        placed(object, { .translation = { -1.5, 0.0, 0.0 } }
         ),
        placed(object, { .translation = { 1.5, 0.0, 0.0 }  }
         ),
    };
    REQUIRE(irop::validate_scene_collisions(known_fit, container).physical_scene_valid());
    for (const std::uint32_t seed : { 0U, 1918U }) {
        DYNAMIC_SECTION("seed " << seed)
        {
            irop::test::TempDirectory temporary;
            const auto result = pack_generated_growth_scene(object, container, temporary.path(), seed, 2);
            require_generated_serialized_growth(result, 2);
        }
    }
}

TEST_CASE("an initially small generated object too large at full size cannot publish packing geometry",
          "[practical-packing][packing][integration][growth-corpus][non-fit]")
{
    const auto container = packing_container(true);
    const double container_volume = irop::ClosedMeshQuery(container).volume();
    const auto object = irop::scale_mesh_to_volume(packing_cylinder(true), 1.1 * container_volume);
    REQUIRE(irop::ClosedMeshQuery(object).volume() > container_volume);
    const std::vector<irop::TriangleMesh> initial_pose { placed(object, { .volume_scale = 0.1 }) };
    REQUIRE(irop::validate_scene_collisions(initial_pose, container).physical_scene_valid());
    irop::test::TempDirectory temporary;
    const auto result = pack_generated_growth_scene(object, container, temporary.path(), 1918, 1, 3);
    INFO(result.packing.diagnostic);
    CAPTURE(irop::to_string(result.packing.status));
    CHECK((result.packing.status == irop::PackingStatus::infeasible ||
           result.packing.status == irop::PackingStatus::iteration_limit ||
           result.packing.status == irop::PackingStatus::correction_limit));
    CHECK_FALSE(result.packed_objects_path.has_value());
    CHECK_FALSE(result.container_output_path.has_value());
    CHECK_FALSE(result.placements_path.has_value());
    CHECK(result.individual_object_paths.empty());
    CHECK(std::filesystem::is_regular_file(result.run_summary_path));
    const auto output = result.run_summary_path.parent_path();
    CHECK_FALSE(std::filesystem::exists(output / "packed-objects.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "placements.json"));
}

}  // namespace
