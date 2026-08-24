#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "irop/geometry/mesh_geometry.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/packing.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

[[nodiscard]] irop::TriangleMesh centered_tetrahedron()
{
    return irop::center_mesh_at_vertex_centroid(irop::test::tetrahedron_mesh()).mesh;
}

[[nodiscard]] irop::PackingState initialized_single_object(const irop::TriangleMesh& object,
                                                           const irop::TriangleMesh& container)
{
    irop::PackingConfig config;
    config.object_count = 1;
    config.initial_volume_scale = 0.1;
    config.seed = 1918;
    return irop::initialize_packing(object, container, config);
}

void require_same_transform(const irop::Transform& actual, const irop::Transform& expected)
{
    CHECK(actual.volume_scale == expected.volume_scale);
    CHECK(actual.rotation.x == expected.rotation.x);
    CHECK(actual.rotation.y == expected.rotation.y);
    CHECK(actual.rotation.z == expected.rotation.z);
    CHECK(actual.translation.x == expected.translation.x);
    CHECK(actual.translation.y == expected.translation.y);
    CHECK(actual.translation.z == expected.translation.z);
}

TEST_CASE("packing status and progress names are stable")
{
    CHECK(std::string(irop::to_string(irop::PackingStatus::success)) == "success");
    CHECK(std::string(irop::to_string(irop::PackingStatus::cancelled)) == "cancelled");
    CHECK(std::string(irop::to_string(irop::PackingStatus::invalid_input)) == "invalid_input");
    CHECK(std::string(irop::to_string(irop::PackingStatus::resource_exhausted)) == "resource_exhausted");
    CHECK(std::string(irop::to_string(irop::PackingStatus::infeasible)) == "infeasible");
    CHECK(std::string(irop::to_string(irop::PackingStatus::iteration_limit)) == "iteration_limit");
    CHECK(std::string(irop::to_string(irop::PackingStatus::correction_limit)) == "correction_limit");
    CHECK(std::string(irop::to_string(irop::PackingStatus::time_limit)) == "time_limit");
    CHECK(std::string(irop::to_string(irop::PackingStatus::numerical_failure)) == "numerical_failure");
    CHECK(std::string(irop::to_string(irop::PackingStatus::dependency_failure)) == "dependency_failure");
    CHECK(std::string(irop::to_string(irop::PackingStatus::internal_failure)) == "internal_failure");

    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::scale_step_started)) == "scale_step_started");
    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::tetrahedralization_recovery)) ==
          "tetrahedralization_recovery");
    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::iteration_completed)) == "iteration_completed");
    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::finished)) == "finished");
}
TEST_CASE("packing barrier multiplier bounds retain slack and saturate extreme ratios")
{
    constexpr double supported_tolerances[] {
        irop::maximum_local_solve_tolerance,
        1.0e-12,
        1.0e-20,
    };
    for (const double tolerance : supported_tolerances) {
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 1.0;
        config.local_solve_tolerance = tolerance;
        CHECK_NOTHROW(irop::validate_packing_algorithm_config(0.999, config, {}));
    }

    constexpr double current_scale = 0.999;
    constexpr double target_scale = 1.0;
    const double target_multiplier = target_scale / current_scale;
    const double near_barrier_bound = irop::detail::barrier_volume_scale_multiplier_bound(current_scale, target_scale);
    CHECK(near_barrier_bound > target_multiplier);
    CHECK(near_barrier_bound ==
          Approx(target_multiplier * (1.0 + irop::local_solve_barrier_relative_slack)).epsilon(1.0e-15));

    const double extreme_bound = irop::detail::barrier_volume_scale_multiplier_bound(1.0e-20, 1.0);
    CHECK(extreme_bound == std::nextafter(irop::maximum_local_solve_bound_magnitude_exclusive, 0.0));
    CHECK(extreme_bound < irop::maximum_local_solve_bound_magnitude_exclusive);
}

TEST_CASE("packing constructs the exact first linear volume-scale barrier before cancellation")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);
    const irop::Transform initial_transform = state.transforms.front();

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 1.0;
    config.scale_step_count = 9;
    config.adaptive_sampling = false;

    bool cancel = false;
    std::vector<irop::PackingProgress> events;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = [&cancel]() {
        return cancel;
    };
    callbacks.progress = [&cancel, &events](const irop::PackingProgress& progress) {
        events.push_back(progress);
        if (progress.phase == irop::PackingProgressPhase::scale_step_started) {
            cancel = true;
        }
    };

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config, {}, callbacks);
    CHECK(result.status == irop::PackingStatus::cancelled);
    REQUIRE_FALSE(events.empty());
    CHECK(events.front().phase == irop::PackingProgressPhase::scale_step_started);
    CHECK(events.front().scale_step == 0);
    CHECK(events.front().scale_step_count == 9);
    CHECK(events.front().target_volume_scale == Approx(0.2).epsilon(1.0e-15));
    CHECK(result.work.iterations == 0);
    CHECK(result.work.tetrahedralization_attempts == 0);
    CHECK(result.history.empty());
    REQUIRE(result.state.transforms.size() == 1);
    require_same_transform(result.state.transforms.front(), initial_transform);
}

TEST_CASE("packing resolves the reference translation setting before cancellation")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);
    const double expected_translation = 2.0 * std::cbrt(state.object_volume);

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = state.config.initial_volume_scale;
    config.scale_step_count = 1;
    config.adaptive_sampling = false;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = []() {
        return true;
    };

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config, {}, callbacks);
    REQUIRE(result.status == irop::PackingStatus::cancelled);
    CHECK(result.resolved_maximum_translation_per_unit_scale == Approx(expected_translation).epsilon(1.0e-15));
    CHECK(result.work.iterations == 0);
    CHECK(result.work.tetrahedralization_attempts == 0);
    CHECK(result.work.local_solves == 0);
    CHECK(result.history.empty());
}

TEST_CASE("packing observes cancellation during its initial-state validation")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    irop::PackingState state = initialized_single_object(object, irop::test::cube_mesh(5.0));
    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = state.config.initial_volume_scale;
    config.scale_step_count = 1;
    config.adaptive_sampling = false;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = []() {
        return true;
    };

    const irop::PackingResult result = irop::run_packing(object, {}, std::move(state), config, {}, callbacks);

    CHECK(result.status == irop::PackingStatus::cancelled);
    CHECK(result.diagnostic == "packing initialization was cancelled");
}

TEST_CASE("packing preserves an explicitly configured translation bound")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = state.config.initial_volume_scale;
    config.scale_step_count = 1;
    config.maximum_translation_per_unit_scale = 7.25;
    config.adaptive_sampling = false;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = []() {
        return true;
    };

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config, {}, callbacks);
    REQUIRE(result.status == irop::PackingStatus::cancelled);
    CHECK(result.resolved_maximum_translation_per_unit_scale == Approx(7.25));
    CHECK(result.work.iterations == 0);
}

TEST_CASE("packing maps a tetrahedralization input limit without mutating the iteration state")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);
    const irop::Transform initial_transform = state.transforms.front();
    const std::uint64_t initial_draw_count = state.random_state.draw_count();

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.2;
    config.scale_step_count = 1;
    config.max_iterations_per_scale_step = 1;
    config.adaptive_sampling = false;
    irop::PackingEngineLimits limits;
    limits.tetrahedralization.max_input_points = 1;

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config, limits);
    CHECK(result.status == irop::PackingStatus::resource_exhausted);
    CHECK(result.work.iterations == 1);
    CHECK(result.work.tetrahedralization_attempts == 1);
    CHECK(result.work.local_solves == 0);
    CHECK(result.history.empty());
    REQUIRE(result.state.transforms.size() == 1);
    require_same_transform(result.state.transforms.front(), initial_transform);
    CHECK(result.state.random_state.draw_count() == initial_draw_count);
}

TEST_CASE("packing rejects an invalid scale schedule and zero engine bounds without throwing")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);

    SECTION("decreasing final scale")
    {
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 0.05;
        const irop::PackingResult result =
            irop::run_packing(object, container, initialized_single_object(object, container), config);
        CHECK(result.status == irop::PackingStatus::invalid_input);
    }

    SECTION("zero correction bound")
    {
        irop::PackingEngineLimits limits;
        limits.max_correction_passes_per_iteration = 0;
        const irop::PackingResult result =
            irop::run_packing(object, container, initialized_single_object(object, container), {}, limits);
        CHECK(result.status == irop::PackingStatus::invalid_input);
    }

    SECTION("disabled adaptive sampling still validates its policy")
    {
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 0.1;
        config.adaptive_sampling = false;
        config.sampling.alpha = 0.0;
        const irop::PackingResult result =
            irop::run_packing(object, container, initialized_single_object(object, container), config);
        CHECK(result.status == irop::PackingStatus::invalid_input);
    }

    SECTION("unused nested limits are validated before an already-complete run")
    {
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 0.1;
        config.adaptive_sampling = false;
        irop::PackingEngineLimits limits;
        limits.cat.max_constraints = 0;
        const irop::PackingResult result =
            irop::run_packing(object, container, initialized_single_object(object, container), config, limits);
        CHECK(result.status == irop::PackingStatus::invalid_input);
    }
}

TEST_CASE("zero local rotation bound still consumes the reference random draws")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);
    const std::uint64_t initial_draw_count = state.random_state.draw_count();

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.1001;
    config.scale_step_count = 1;
    config.max_iterations_per_scale_step = 3;
    config.maximum_rotation_delta_radians = 0.0;
    config.adaptive_sampling = false;

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config);
    INFO(result.diagnostic);
    REQUIRE(result.status == irop::PackingStatus::success);
    CHECK(result.work.local_solves == 1);
    CHECK(result.state.random_state.draw_count() == initial_draw_count + 3);
}

TEST_CASE("adaptive packing resamples both participants before bounded tetrahedralization")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.1001;
    config.scale_step_count = 1;
    config.max_iterations_per_scale_step = 1;
    config.adaptive_sampling = true;
    irop::PackingEngineLimits limits;
    limits.tetrahedralization.max_input_points = 1;

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config, limits);
    CHECK(result.status == irop::PackingStatus::resource_exhausted);
    CHECK(result.work.resampling_operations == 2);
    CHECK(result.work.tetrahedralization_attempts == 1);
    CHECK(result.work.local_solves == 0);
}

TEST_CASE("one small growth barrier completes through TetGen CAT Ipopt and full-resolution validation",
          "[packing][integration]")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingState state = initialized_single_object(object, container);
    const std::uint64_t initial_draw_count = state.random_state.draw_count();

    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.1001;
    config.scale_step_count = 1;
    config.max_iterations_per_scale_step = 3;
    config.maximum_rotation_delta_radians = 0.01;
    config.adaptive_sampling = false;

    const irop::PackingResult result = irop::run_packing(object, container, std::move(state), config);
    INFO(result.diagnostic);
    REQUIRE(result.status == irop::PackingStatus::success);
    REQUIRE(result.state.transforms.size() == 1);
    CHECK(result.state.transforms.front().volume_scale == Approx(config.final_volume_scale).epsilon(1.0e-12));
    CHECK(result.state.random_state.draw_count() == initial_draw_count + 3);
    CHECK(result.work.completed_scale_steps == 1);
    CHECK(result.work.tetrahedralization_attempts == 1);
    CHECK(result.work.cat_builds == 1);
    CHECK(result.work.local_solves == 1);
    REQUIRE(result.history.size() == 1);
    CHECK(result.history.front().objects_at_target == 1);
    CHECK(result.final_validation_performed);
    CHECK(result.final_validation.physical_scene_valid());
}

}  // namespace
