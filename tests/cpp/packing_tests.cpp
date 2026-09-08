#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../../src/optimization/solver_openmp_scope.hpp"
#include "irop/geometry/collision.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/run_scene.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/pack_scene.hpp"
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

void require_same_committed_state(const irop::PackingState& actual, const irop::PackingState& expected)
{
    REQUIRE(actual.transforms.size() == expected.transforms.size());
    for (std::size_t index = 0; index < actual.transforms.size(); ++index) {
        require_same_transform(actual.transforms[index], expected.transforms[index]);
    }
    CHECK(actual.random_state.seed() == expected.random_state.seed());
    CHECK(actual.random_state.draw_count() == expected.random_state.draw_count());
    auto actual_random = actual.random_state;
    auto expected_random = expected.random_state;
    for (unsigned draw = 0; draw < 8; ++draw) {
        CHECK(actual_random.uniform(-1.0, 1.0) == expected_random.uniform(-1.0, 1.0));
    }
}

void require_same_history(const irop::PackingResult& actual, const irop::PackingResult& expected)
{
    REQUIRE(actual.history.size() == expected.history.size());
    for (std::size_t index = 0; index < actual.history.size(); ++index) {
        const auto& first = actual.history[index];
        const auto& second = expected.history[index];
        CHECK(std::tie(first.scale_step, first.iteration, first.target_volume_scale, first.objects_at_target,
                       first.sampled_object_triangles, first.sampled_container_triangles, first.cat_violations,
                       first.container_violations, first.object_collisions, first.correction_passes) ==
              std::tie(second.scale_step, second.iteration, second.target_volume_scale, second.objects_at_target,
                       second.sampled_object_triangles, second.sampled_container_triangles, second.cat_violations,
                       second.container_violations, second.object_collisions, second.correction_passes));
    }
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
    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::sampling_recovery)) == "sampling_recovery");
    CHECK(std::string(irop::to_string(irop::PackingProgressPhase::step_recovery)) == "step_recovery");
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

    SECTION("unused thread requests are validated before an already-complete run")
    {
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 0.1;
        config.solver_openmp_threads = 257;
        const auto result = irop::run_packing(object, container, initialized_single_object(object, container), config);
        CHECK(result.status == irop::PackingStatus::invalid_input);
        CHECK(result.work.local_solves == 0);
        CHECK_FALSE(result.work.local_solve.threading);
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

TEST_CASE("interrupted physical checking cannot certify CAT diagnostic completeness",
          "[packing][integration][cat-completeness]")
{
    const auto object = centered_tetrahedron();
    const auto container = irop::test::cube_mesh(5.0);
    const auto state = initialized_single_object(object, container);
    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.1001;
    config.scale_step_count = 1;
    config.maximum_rotation_delta_radians = 0.01;
    config.adaptive_sampling = false;
    irop::PackingEngineLimits limits;
    // CAT reporting is omitted, then the first physical surface query throws
    // before returning its report. Both engine policies must retain incompleteness.
    limits.collision.max_cat_triangle_pair_tests = 0;
    limits.collision.max_triangle_pair_tests = 1;
    for (const bool reference : { false, true }) {
        CAPTURE(reference);
        config.use_reference_growth_policy = reference;
        const auto result = irop::run_packing(object, container, state, config, limits);
        INFO(result.diagnostic);
        REQUIRE(result.status == irop::PackingStatus::resource_exhausted);
        CHECK(result.diagnostic.find("triangle-pair") != std::string::npos);
        CHECK(result.work.local_solves == 1);
        CHECK_FALSE(result.cat_diagnostics_complete);
        CHECK_FALSE(result.final_validation_performed);
        CHECK(result.history.empty());
        REQUIRE(result.state.transforms.size() == state.transforms.size());
        require_same_transform(result.state.transforms.front(), state.transforms.front());
        CHECK(result.state.random_state.draw_count() == state.random_state.draw_count());
    }
}

TEST_CASE("packing growth policy is retained in bounded failed local requests", "[packing][growth-policy][diagnostics]")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    const irop::PackingState state = initialized_single_object(object, container);
    irop::PackingEngineLimits limits;
    // One complete constraint evaluation cannot fit. The failure captures the
    // actual engine request without depending on solver convergence or timing.
    limits.local_solve.max_constraint_rows_evaluated = 1;
    for (const bool reference : { false, true }) {
        CAPTURE(reference);
        irop::PackingAlgorithmConfig config;
        config.final_volume_scale = 0.2;
        config.scale_step_count = 1;
        config.adaptive_sampling = false;
        config.maximum_rotation_delta_radians = 0.3;
        config.use_reference_growth_policy = reference;
        config.diagnostics.capture_failed_local_problem = true;
        const auto result = irop::run_packing(object, container, state, config, limits);
        INFO(result.diagnostic);
        REQUIRE(result.status == irop::PackingStatus::resource_exhausted);
        REQUIRE(result.diagnostics.failed_local_problem.has_value());
        const auto& snapshot = *result.diagnostics.failed_local_problem;
        const auto& request = snapshot.request;
        CHECK(snapshot.constraints.size() > limits.local_solve.max_constraint_rows_evaluated);
        CHECK(result.work.local_solve.constraint_rows_evaluated <= limits.local_solve.max_constraint_rows_evaluated);
        CHECK(request.participant == 0);
        CHECK(request.use_exact_hessian == !reference);
        CHECK(request.maximum_result_volume_scale == config.final_volume_scale);
        REQUIRE(request.bounds.maximum_volume_scale_multiplier.has_value());
        if (reference) {
            auto random = state.random_state;
            const double rotation_bound = 0.9 * config.maximum_rotation_delta_radians;
            CHECK(request.initial_guess.volume_scale_multiplier == state.config.initial_volume_scale);
            CHECK(request.initial_guess.rotation_delta.x == random.uniform(-rotation_bound, rotation_bound));
            CHECK(request.initial_guess.rotation_delta.y == random.uniform(-rotation_bound, rotation_bound));
            CHECK(request.initial_guess.rotation_delta.z == random.uniform(-rotation_bound, rotation_bound));
            CHECK(*request.bounds.maximum_volume_scale_multiplier >
                  config.final_volume_scale / state.config.initial_volume_scale);
        }
        else {
            CHECK(request.initial_guess.volume_scale_multiplier == 1.0);
            CHECK(request.initial_guess.rotation_delta.x == 0.0);
            CHECK(request.initial_guess.rotation_delta.y == 0.0);
            CHECK(request.initial_guess.rotation_delta.z == 0.0);
            CHECK(*request.bounds.maximum_volume_scale_multiplier ==
                  config.final_volume_scale / state.config.initial_volume_scale);
        }
        CHECK(request.initial_guess.translation_delta.x == 0.0);
        CHECK(request.initial_guess.translation_delta.y == 0.0);
        CHECK(request.initial_guess.translation_delta.z == 0.0);
        REQUIRE(result.state.transforms.size() == state.transforms.size());
        require_same_transform(result.state.transforms.front(), state.transforms.front());
        CHECK(result.state.random_state.draw_count() == state.random_state.draw_count());
        CHECK(result.history.empty());
    }
}

[[nodiscard]] std::pair<irop::TriangleMesh, irop::TriangleMesh> adaptive_recovery_meshes()
{
    constexpr std::size_t segments = 12;
    irop::TriangleMesh object = irop::test::cylinder_mesh(0.9, 1.055, segments);
    for (std::size_t index = 0; index < segments; ++index) {
        const double radial_factor = 1.0 + 0.0025 * static_cast<double>(index);
        for (const std::size_t offset : { std::size_t { 0 }, segments }) {
            object.vertices[offset + index].x *= radial_factor;
            object.vertices[offset + index].y *= radial_factor;
        }
        const double axial_offset = 0.0001 * static_cast<double>(index * index);
        object.vertices[index].z -= axial_offset;
        object.vertices[segments + index].z += axial_offset;
    }
    irop::TriangleMesh container = irop::test::box_mesh(3.5, 4.0, 2.85);
    for (irop::Point3& point : container.vertices) {
        const double original_x = point.x;
        point.x += 0.04 * point.z;
        point.y += 0.025 * original_x;
    }
    // Exercise the same generated STL input boundary as the measured fixture;
    // no external mesh or exact solver trace is required by the assertions.
    const irop::test::TempDirectory temporary;
    const auto object_path = temporary.path() / "object.stl";
    const auto container_path = temporary.path() / "container.stl";
    irop::write_stl(object_path, object);
    irop::write_stl(container_path, container);
    return { irop::center_mesh_at_vertex_centroid(irop::read_stl(object_path, {}).mesh).mesh,
             irop::read_stl(container_path, {}).mesh };
}

[[nodiscard]] irop::PackingState adaptive_recovery_initial_state(const irop::TriangleMesh& object,
                                                                 const irop::TriangleMesh& container)
{
    irop::PackingConfig config;
    config.object_count = 3;
    config.initial_volume_scale = 0.1;
    config.seed = 12345;
    config.enable_structured_fallback = false;
    return irop::initialize_packing(object, container, config);
}

[[nodiscard]] irop::PackingAlgorithmConfig adaptive_recovery_config()
{
    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 1.0;
    config.scale_step_count = 3;
    config.max_iterations_per_scale_step = 30;
    config.adaptive_sampling = true;
    return config;
}

void require_physically_valid_state(const irop::TriangleMesh& object, const irop::TriangleMesh& container,
                                    const irop::PackingResult& result)
{
    std::vector<irop::TriangleMesh> objects;
    for (const auto& transform : result.state.transforms) {
        objects.push_back(irop::transform_mesh(object, transform));
    }
    CHECK(irop::validate_scene_collisions(objects, container).physical_scene_valid());
}

TEST_CASE("adaptive physical recovery refines a generated surface and retains its floor across barriers",
          "[packing][integration][sampling-recovery]")
{
    const auto [object, container] = adaptive_recovery_meshes();
    const auto config = adaptive_recovery_config();
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    std::uint64_t recovery_events = 0;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&recovery_events](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::sampling_recovery) {
            ++recovery_events;
        }
    };
    const auto result = irop::run_packing(object, container, adaptive_recovery_initial_state(object, container), config,
                                          limits, callbacks);
    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.work.sampling_refinements > 0);
    CHECK(recovery_events == result.work.sampling_refinements);
    CHECK(result.work.correction_passes >= result.work.sampling_refinements);
    std::uint64_t maximum_refinements = 0;
    for (std::uint64_t count = config.sampling.minimum_triangle_count; count < object.triangles.size(); count *= 2) {
        ++maximum_refinements;
    }
    CHECK(result.work.sampling_refinements <= maximum_refinements);
    CHECK(result.work.completed_scale_steps == config.scale_step_count);
    REQUIRE(result.final_validation_performed);
    CHECK(result.final_validation.physical_scene_valid());
    require_physically_valid_state(object, container, result);
    for (const auto& transform : result.state.transforms) {
        CHECK(transform.volume_scale == config.final_volume_scale);
    }

    REQUIRE_FALSE(result.history.empty());
    const auto preserved_container_triangles = result.history.front().sampled_container_triangles;
    std::uint64_t previous_triangles = 0;
    std::uint64_t previous_step = 0;
    bool observed_retained_floor = false;
    for (const auto& record : result.history) {
        CHECK(record.sampled_container_triangles == preserved_container_triangles);
        CHECK(record.sampled_object_triangles >= previous_triangles);
        CHECK(record.sampled_object_triangles <= object.triangles.size());
        if (record.scale_step > previous_step) {
            const auto scheduled_count = irop::target_surface_triangle_count(
                object.triangles.size(), record.target_volume_scale, config.sampling);
            observed_retained_floor |= record.sampled_object_triangles > scheduled_count;
        }
        previous_triangles = record.sampled_object_triangles;
        previous_step = record.scale_step;
    }
    CHECK(observed_retained_floor);
    // The final barrier restores the original object surface. Its increased
    // fidelity must not multiply the unchanged container's sample sites.
    CHECK(result.history.back().sampled_object_triangles == object.triangles.size());

    // Refinement and every committed batch change the prepared geometry. A retry
    // cache must not carry an older approximation across either boundary.
    auto uncached_config = config;
    uncached_config.reuse_physical_retry_results = false;
    const auto uncached = irop::run_packing(object, container, adaptive_recovery_initial_state(object, container),
                                            uncached_config, limits);
    INFO(uncached.diagnostic);
    REQUIRE(uncached.succeeded());
    require_same_committed_state(result.state, uncached.state);
    require_same_history(result, uncached);
    CHECK(result.work.local_solves + result.work.reused_local_solves == uncached.work.local_solves);
    CHECK(result.work.tetrahedralization_attempts + result.work.reused_prepared_batches ==
          uncached.work.tetrahedralization_attempts);
}

TEST_CASE("reference growth retains the historical container resampling schedule",
          "[packing][integration][growth-policy]")
{
    const auto [object, container] = adaptive_recovery_meshes();
    irop::PackingConfig initialization;
    initialization.object_count = 1;
    initialization.initial_volume_scale = 0.1;
    initialization.seed = 12345;
    initialization.enable_structured_fallback = false;
    auto config = adaptive_recovery_config();
    config.scale_step_count = 2;
    config.use_reference_growth_policy = true;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    const auto result = irop::run_packing(object, container,
                                          irop::initialize_packing(object, container, initialization), config, limits);
    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.final_validation_performed);
    CHECK(result.final_validation.physical_scene_valid());
    CHECK(result.work.reused_local_solves == 0);
    CHECK(result.work.reused_prepared_batches == 0);
    CHECK(result.work.sampling_refinements == 0);
    CHECK(result.work.resampling_operations == 2 * config.scale_step_count);
    REQUIRE(result.history.size() >= 2);
    CHECK(result.history.front().scale_step == 0);
    CHECK(result.history.back().scale_step == 1);
    CHECK(result.history.back().sampled_object_triangles == object.triangles.size());
    CHECK(result.history.back().sampled_container_triangles > result.history.front().sampled_container_triangles);
}

TEST_CASE("cancellation at adaptive recovery preserves the last physically validated batch",
          "[packing][integration][sampling-recovery][cancellation]")
{
    const auto [object, container] = adaptive_recovery_meshes();
    const auto initial_state = adaptive_recovery_initial_state(object, container);
    const auto config = adaptive_recovery_config();
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    bool cancel = false;
    irop::PackingProgress recovery;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = [&cancel]() {
        return cancel;
    };
    callbacks.progress = [&cancel, &recovery](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::sampling_recovery) {
            recovery = progress;
            cancel = true;
        }
    };
    const auto result = irop::run_packing(object, container, initial_state, config, limits, callbacks);
    INFO(result.diagnostic);
    REQUIRE(result.status == irop::PackingStatus::cancelled);
    REQUIRE(result.work.sampling_refinements == 1);
    REQUIRE_FALSE(result.history.empty());
    CHECK(result.history.back().scale_step == recovery.scale_step);
    CHECK(result.history.back().iteration == recovery.iteration);
    CHECK(result.history.back().correction_passes > 0);
    CHECK_FALSE(result.final_validation_performed);
    require_physically_valid_state(object, container, result);

    // Compare with cancellation immediately after that same corrected batch,
    // before resampling. Recovery must change neither the poses nor the RNG.
    cancel = false;
    callbacks.progress = [&cancel, &recovery](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::iteration_completed &&
            progress.scale_step == recovery.scale_step && progress.iteration == recovery.iteration) {
            cancel = true;
        }
    };
    const auto before_recovery = irop::run_packing(object, container, initial_state, config, limits, callbacks);
    INFO(before_recovery.diagnostic);
    REQUIRE(before_recovery.status == irop::PackingStatus::cancelled);
    CHECK(before_recovery.work.sampling_refinements == 0);
    CHECK(before_recovery.history.size() == result.history.size());
    REQUIRE(before_recovery.state.transforms.size() == result.state.transforms.size());
    for (std::size_t index = 0; index < result.state.transforms.size(); ++index) {
        require_same_transform(result.state.transforms[index], before_recovery.state.transforms[index]);
    }
    CHECK(result.state.random_state.draw_count() == before_recovery.state.random_state.draw_count());
}

[[nodiscard]] std::pair<irop::TriangleMesh, irop::TriangleMesh> physical_step_recovery_meshes(
    const std::filesystem::path& source_object_path = {})
{
    constexpr irop::MeshIndex segments = 12;
    irop::TriangleMesh object = irop::test::cylinder_mesh(1.0, 1.055, segments);
    for (irop::MeshIndex index = 0; index < segments; ++index) {
        const double radius = (index % 2 == 0 ? 0.9 : 0.38) * (1.0 + 0.0025 * static_cast<double>(index));
        for (const auto offset : { irop::MeshIndex { 0 }, segments }) {
            object.vertices[offset + index].x *= radius;
            object.vertices[offset + index].y *= radius;
        }
        const double axial_offset = 0.0001 * static_cast<double>(index * index);
        object.vertices[index].z -= axial_offset;
        object.vertices[segments + index].z += axial_offset;
    }
    // A center fan closes each concave star cap without crossing its boundary.
    object.vertices.push_back({ 0.0, 0.0, -1.055 });
    object.vertices.push_back({ 0.0, 0.0, 1.055 });
    object.triangles.clear();
    for (irop::MeshIndex index = 0; index < segments; ++index) {
        const irop::MeshIndex next = (index + 1) % segments;
        object.triangles.push_back({ index, next, segments + next });
        object.triangles.push_back({ index, segments + next, segments + index });
        object.triangles.push_back({ 2 * segments, next, index });
        object.triangles.push_back({ 2 * segments + 1, segments + index, segments + next });
    }
    const irop::TriangleMesh container = adaptive_recovery_meshes().second;
    const irop::test::TempDirectory temporary;
    const auto path = source_object_path.empty() ? temporary.path() / "star.stl" : source_object_path;
    irop::write_stl(path, object);
    return { irop::center_mesh_at_vertex_centroid(irop::read_stl(path, {}).mesh).mesh, container };
}

[[nodiscard]] irop::PackingState physical_step_recovery_initial_state(const irop::TriangleMesh& object,
                                                                      const irop::TriangleMesh& container)
{
    irop::PackingConfig config;
    config.object_count = 4;
    config.initial_volume_scale = 0.1;
    config.seed = 12345;
    config.enable_structured_fallback = false;
    return irop::initialize_packing(object, container, config);
}

TEST_CASE("physical step recovery bounds retries while full-resolution concave objects reach their target",
          "[packing][integration][physical-step-recovery]")
{
    const auto [object, container] = physical_step_recovery_meshes();
    const auto initial_state = physical_step_recovery_initial_state(object, container);
    auto config = adaptive_recovery_config();
    config.adaptive_sampling = false;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    std::vector<std::uint64_t> retries(static_cast<std::size_t>(config.scale_step_count), 0);
    std::uint64_t retry_events = 0;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&retries, &retry_events](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::step_recovery) {
            ++retries.at(static_cast<std::size_t>(progress.scale_step));
            ++retry_events;
        }
    };
    const auto result = irop::run_packing(object, container, initial_state, config, limits, callbacks);
    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.work.physical_step_retries > 0);
    CHECK(result.work.physical_step_retries == retry_events);
    for (const auto count : retries) {
        // At most four reductions per object per barrier; simultaneous reductions
        // share one retry, so they cannot increase this upper bound.
        CHECK(count <= 4 * initial_state.transforms.size());
    }
    CHECK(result.work.completed_scale_steps == config.scale_step_count);
    CHECK(result.work.resampling_operations == 0);
    REQUIRE(result.final_validation_performed);
    CHECK(result.final_validation.physical_scene_valid());
    require_physically_valid_state(object, container, result);
    for (const auto& transform : result.state.transforms) {
        CHECK(transform.volume_scale == config.final_volume_scale);
    }
}

TEST_CASE("cancellation of a rejected physical trial preserves the preceding committed state",
          "[packing][integration][physical-step-recovery][cancellation]")
{
    const auto [object, container] = physical_step_recovery_meshes();
    const auto initial_state = physical_step_recovery_initial_state(object, container);
    auto config = adaptive_recovery_config();
    config.adaptive_sampling = false;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    bool cancel = false;
    std::optional<irop::PackingProgress> last_committed;
    irop::PackingCallbacks callbacks;
    callbacks.cancellation_requested = [&cancel]() {
        return cancel;
    };
    callbacks.progress = [&cancel, &last_committed](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::iteration_completed) {
            last_committed = progress;
        }
        else if (progress.phase == irop::PackingProgressPhase::step_recovery) {
            cancel = true;
        }
    };
    const auto result = irop::run_packing(object, container, initial_state, config, limits, callbacks);
    INFO(result.diagnostic);
    REQUIRE(result.status == irop::PackingStatus::cancelled);
    REQUIRE(result.work.physical_step_retries == 1);
    CHECK_FALSE(result.final_validation_performed);
    require_physically_valid_state(object, container, result);

    irop::PackingState expected_state = initial_state;
    if (last_committed.has_value()) {
        cancel = false;
        callbacks.progress = [&cancel, &last_committed](const irop::PackingProgress& progress) {
            if (progress.phase == irop::PackingProgressPhase::iteration_completed &&
                progress.scale_step == last_committed->scale_step && progress.iteration == last_committed->iteration) {
                cancel = true;
            }
        };
        const auto before_trial = irop::run_packing(object, container, initial_state, config, limits, callbacks);
        INFO(before_trial.diagnostic);
        REQUIRE(before_trial.status == irop::PackingStatus::cancelled);
        CHECK(before_trial.work.physical_step_retries == 0);
        CHECK(before_trial.history.size() == result.history.size());
        expected_state = before_trial.state;
    }
    else {
        CHECK(result.history.empty());
    }
    REQUIRE(expected_state.transforms.size() == result.state.transforms.size());
    for (std::size_t index = 0; index < result.state.transforms.size(); ++index) {
        require_same_transform(result.state.transforms[index], expected_state.transforms[index]);
    }
    CHECK(result.state.random_state.draw_count() == expected_state.random_state.draw_count());
}

TEST_CASE("physical retry caching preserves exact growth and accounts for avoided work",
          "[packing][integration][physical-step-recovery][retry-cache]")
{
    const auto [object, container] = physical_step_recovery_meshes();
    const auto initial_state = physical_step_recovery_initial_state(object, container);
    auto config = adaptive_recovery_config();
    config.adaptive_sampling = false;
    config.reuse_physical_retry_results = false;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);
    const auto uncached = irop::run_packing(object, container, initial_state, config, limits);
    INFO(uncached.diagnostic);
    REQUIRE(uncached.succeeded());
    REQUIRE(uncached.work.physical_step_retries > 0);
    CHECK(uncached.work.reused_local_solves == 0);
    CHECK(uncached.work.reused_prepared_batches == 0);

    config.reuse_physical_retry_results = true;
    const auto cached = irop::run_packing(object, container, initial_state, config, limits);
    INFO(cached.diagnostic);
    REQUIRE(cached.succeeded());
    REQUIRE(cached.work.reused_prepared_batches > 0);
    REQUIRE(cached.work.reused_local_solves > 0);
    require_same_committed_state(cached.state, uncached.state);
    require_same_history(cached, uncached);
    CHECK(cached.work.completed_scale_steps == config.scale_step_count);
    CHECK(cached.work.iterations == uncached.work.iterations);
    CHECK(cached.work.physical_step_retries == uncached.work.physical_step_retries);
    CHECK(cached.work.correction_passes == uncached.work.correction_passes);
    CHECK(cached.work.local_solves + cached.work.reused_local_solves == uncached.work.local_solves);
    CHECK(cached.work.tetrahedralization_attempts + cached.work.reused_prepared_batches ==
          uncached.work.tetrahedralization_attempts);
    CHECK(cached.work.cat_builds + cached.work.reused_prepared_batches == uncached.work.cat_builds);
    CHECK(cached.work.collision.object_pairs_examined == uncached.work.collision.object_pairs_examined);
    CHECK(cached.work.collision.triangle_pairs_tested == uncached.work.collision.triangle_pairs_tested);
    CHECK(cached.work.collision.containment_triangle_visits == uncached.work.collision.containment_triangle_visits);
    CHECK(cached.work.collision.cat_triangle_pairs_tested == uncached.work.collision.cat_triangle_pairs_tested);
    CHECK(cached.cat_diagnostics_complete == uncached.cat_diagnostics_complete);
    REQUIRE(cached.final_validation_performed);
    CHECK(cached.final_validation.physical_scene_valid());
    require_physically_valid_state(object, container, cached);

    // Work limits bound actual dependency calls. Reused results need no renewed
    // solver allowance, while a changed problem must still consume a fresh call.
    limits.max_total_local_solves = cached.work.local_solves;
    const auto exactly_bounded = irop::run_packing(object, container, initial_state, config, limits);
    INFO(exactly_bounded.diagnostic);
    REQUIRE(exactly_bounded.succeeded());
    CHECK(exactly_bounded.work.local_solves == limits.max_total_local_solves);
    require_same_committed_state(exactly_bounded.state, cached.state);
    require_same_history(exactly_bounded, cached);
    REQUIRE(limits.max_total_local_solves > 1);
    --limits.max_total_local_solves;
    const auto limited = irop::run_packing(object, container, initial_state, config, limits);
    REQUIRE(limited.status == irop::PackingStatus::resource_exhausted);
    CHECK(limited.work.local_solves == limits.max_total_local_solves);
    CHECK_FALSE(limited.final_validation_performed);
    require_physically_valid_state(object, container, limited);
}

TEST_CASE("cached physical retries preserve committed state on cancellation and cumulative collision exhaustion",
          "[packing][integration][physical-step-recovery][retry-cache][cancellation]")
{
    const auto [object, container] = physical_step_recovery_meshes();
    const auto initial_state = physical_step_recovery_initial_state(object, container);
    auto config = adaptive_recovery_config();
    config.adaptive_sampling = false;
    config.reuse_physical_retry_results = true;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = std::chrono::seconds(30);

    // A logical progress request without an actual solve record identifies a
    // cache hit using public diagnostics, without assuming which object collided.
    using RequestKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    std::vector<RequestKey> logical_requests;
    std::set<std::pair<std::uint64_t, std::uint64_t>> rejected_iterations;
    auto discovery_config = config;
    discovery_config.diagnostics.max_local_solve_records = 4'096;
    irop::PackingCallbacks discovery_callbacks;
    discovery_callbacks.progress = [&](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::local_solve_started) {
            REQUIRE(progress.object_id);
            logical_requests.emplace_back(progress.scale_step, progress.iteration, *progress.object_id);
        }
        else if (progress.phase == irop::PackingProgressPhase::step_recovery) {
            rejected_iterations.emplace(progress.scale_step, progress.iteration);
        }
    };
    const auto discovery =
        irop::run_packing(object, container, initial_state, discovery_config, limits, discovery_callbacks);
    INFO(discovery.diagnostic);
    REQUIRE(discovery.succeeded());
    REQUIRE(discovery.work.reused_local_solves > 0);
    REQUIRE(discovery.diagnostics.local_solve_records_dropped == 0);
    REQUIRE(discovery.diagnostics.local_solve_records.size() == discovery.work.local_solves);
    std::set<RequestKey> actual_requests;
    for (const auto& record : discovery.diagnostics.local_solve_records) {
        REQUIRE(actual_requests.emplace(record.scale_step, record.iteration, record.object_id).second);
    }
    REQUIRE(logical_requests.size() == discovery.work.local_solves + discovery.work.reused_local_solves);
    const auto cached_request = std::find_if(logical_requests.begin(), logical_requests.end(), [&](const auto& key) {
        return !actual_requests.contains(key);
    });
    REQUIRE(cached_request != logical_requests.end());
    const auto& [target_scale_step, target_iteration, target_object] = *cached_request;
    REQUIRE(target_iteration > 0);
    REQUIRE(rejected_iterations.contains({ target_scale_step, target_iteration - 1 }));
    const auto actual_calls_before_hit =
        static_cast<std::uint64_t>(std::distance(logical_requests.begin(), cached_request));
    // This is the first reused request, so every preceding logical request ran.
    REQUIRE(actual_calls_before_hit <= discovery.work.local_solves);

    const auto cancel_at_target = [&](const bool enter_retry) {
        bool cancel = false;
        irop::PackingCallbacks callbacks;
        callbacks.cancellation_requested = [&cancel]() {
            return cancel;
        };
        callbacks.progress = [&](const irop::PackingProgress& progress) {
            if (!enter_retry && progress.phase == irop::PackingProgressPhase::step_recovery &&
                progress.scale_step == target_scale_step && progress.iteration == target_iteration - 1) {
                cancel = true;
            }
            else if (enter_retry && progress.phase == irop::PackingProgressPhase::local_solve_started &&
                     progress.scale_step == target_scale_step && progress.iteration == target_iteration &&
                     progress.object_id == target_object) {
                cancel = true;
            }
        };
        return irop::run_packing(object, container, initial_state, config, limits, callbacks);
    };
    const auto rejected = cancel_at_target(false);
    REQUIRE(rejected.status == irop::PackingStatus::cancelled);
    REQUIRE(rejected.work.physical_step_retries > 0);
    CHECK(rejected.work.reused_local_solves == 0);
    const auto entered_retry = cancel_at_target(true);
    INFO(entered_retry.diagnostic);
    REQUIRE(entered_retry.status == irop::PackingStatus::cancelled);
    CHECK(entered_retry.work.physical_step_retries == rejected.work.physical_step_retries);
    CHECK(entered_retry.work.reused_prepared_batches == rejected.work.reused_prepared_batches + 1);
    CHECK(entered_retry.work.local_solves == actual_calls_before_hit);
    CHECK(entered_retry.work.reused_local_solves == 0);
    CHECK_FALSE(entered_retry.final_validation_performed);
    require_same_committed_state(entered_retry.state, rejected.state);
    require_same_history(entered_retry, rejected);
    require_physically_valid_state(object, container, entered_retry);

    // Admit the rejected trial exactly, then exhaust the same cumulative physical
    // allowance during its cached retry. It must not acquire a fresh budget.
    REQUIRE(rejected.work.collision.object_pairs_examined > 0);
    limits.collision.max_object_pair_checks = rejected.work.collision.object_pairs_examined;
    const auto exhausted = irop::run_packing(object, container, initial_state, config, limits);
    INFO(exhausted.diagnostic);
    REQUIRE(exhausted.status == irop::PackingStatus::resource_exhausted);
    CHECK(exhausted.work.reused_prepared_batches == rejected.work.reused_prepared_batches + 1);
    CHECK(exhausted.work.reused_local_solves > 0);
    CHECK(exhausted.work.collision.object_pairs_examined == limits.collision.max_object_pair_checks);
    CHECK_FALSE(exhausted.final_validation_performed);
    require_same_committed_state(exhausted.state, rejected.state);
    require_same_history(exhausted, rejected);
}

TEST_CASE("physical retry caching still publishes and reloads strictly valid float32 geometry",
          "[packing][pack-scene][physical-step-recovery][retry-cache]")
{
    const irop::test::TempDirectory temporary;
    const auto object_path = temporary.path() / "star.stl";
    const auto container_path = temporary.path() / "container.stl";
    const auto meshes = physical_step_recovery_meshes(object_path);
    irop::write_stl(container_path, meshes.second);
    irop::PackOptions options;
    options.initialization.object_count = 4;
    options.initialization.initial_volume_scale = 0.1;
    options.initialization.seed = 12345;
    options.initialization.enable_structured_fallback = false;
    options.algorithm = adaptive_recovery_config();
    options.algorithm.adaptive_sampling = false;
    options.algorithm.reuse_physical_retry_results = true;
    options.limits.max_elapsed_time = std::chrono::seconds(30);
    options.write_individual_objects = true;
    const auto result = irop::pack_scene(object_path, container_path, temporary.path() / "packed", options);
    INFO(result.packing.diagnostic);
    REQUIRE(result.packing.succeeded());
    REQUIRE(result.packing.work.reused_prepared_batches > 0);
    REQUIRE(result.packing.final_validation_performed);
    CHECK(result.packing.final_validation.physical_scene_valid());
    REQUIRE(result.individual_object_paths.size() == options.initialization.object_count);
    REQUIRE(result.container_output_path);
    std::vector<irop::TriangleMesh> serialized_objects;
    for (const auto& path : result.individual_object_paths) {
        serialized_objects.push_back(irop::read_stl(path, {}).mesh);
    }
    const auto serialized_container = irop::read_stl(*result.container_output_path, {}).mesh;
    CHECK(irop::validate_scene_collisions(serialized_objects, serialized_container).physical_scene_valid());
    for (const auto& transform : result.packing.state.transforms) {
        CHECK(transform.volume_scale == options.algorithm.final_volume_scale);
    }
    const auto loaded = irop::load_run_scene(result.run_summary_path);
    CHECK(loaded.success);
    CHECK(loaded.object_count == options.initialization.object_count);
    REQUIRE(loaded.objects);
    CHECK(loaded.objects->triangles.size() == options.initialization.object_count * meshes.first.triangles.size());
}

TEST_CASE("packing threading aggregation identifies changes in the calling task", "[packing][threading]")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.3;
    config.scale_step_count = 2;
    config.maximum_rotation_delta_radians = 0.0;
    config.adaptive_sampling = false;
    config.solver_openmp_threads = 1;
    const irop::detail::SolverOpenmpScope caller(2);
    std::optional<irop::detail::SolverOpenmpScope> changed_caller;
    std::uint64_t local_calls = 0;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::local_solve_started && ++local_calls == 2) {
            changed_caller.emplace(3);
        }
    };
    const auto result =
        irop::run_packing(object, container, initialized_single_object(object, container), config, {}, callbacks);
    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.work.local_solves >= 2);
    REQUIRE(result.work.local_solve.threading);
    CHECK(result.work.local_solve.threading->before_openmp_threads == 2);
    CHECK(result.work.local_solve.threading->scoped_openmp_threads == 1);
    CHECK(result.work.local_solve.threading_mixed);
    REQUIRE(changed_caller);
    CHECK(irop::detail::SolverOpenmpScope(0).scoped_threads() == 3);
    changed_caller.reset();
    CHECK(irop::detail::SolverOpenmpScope(0).scoped_threads() == 2);
}

}  // namespace
