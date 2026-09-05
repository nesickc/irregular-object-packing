#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "irop/geometry/mesh_geometry.hpp"
#include "irop/io/local_solve_replay.hpp"
#include "irop/packing/packing.hpp"
#include "support/test_support.hpp"

namespace {

[[nodiscard]] std::vector<irop::LocalPlaneConstraint> box_constraints()
{
    return {
        { { 0.9, 0, 0 },  { 1, 0, 0 },  { -1, 0, 0 } },
        { { -0.9, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 }  },
        { { 0, 0.9, 0 },  { 0, 1, 0 },  { 0, -1, 0 } },
        { { 0, -0.9, 0 }, { 0, -1, 0 }, { 0, 1, 0 }  },
        { { 0, 0, 0.9 },  { 0, 0, 1 },  { 0, 0, -1 } },
        { { 0, 0, -0.9 }, { 0, 0, -1 }, { 0, 0, 1 }  },
    };
}

[[nodiscard]] irop::LocalSolveRequest scale_request()
{
    irop::LocalSolveRequest request;
    request.initial_guess.volume_scale_multiplier = 1.0;
    request.bounds.maximum_volume_scale_multiplier = 8.0;
    request.bounds.maximum_absolute_translation = 0.0;
    request.maximum_result_volume_scale = 10.0;
    request.tolerance = 1.0e-8;
    return request;
}

void write_json(const std::filesystem::path& path, const nlohmann::json& json)
{
    std::ofstream stream(path);
    stream << json.dump();
    stream.close();
    REQUIRE(stream.good());
}

TEST_CASE("bounded local trace and snapshot reproduce one failed problem", "[diagnostics][local-solve]")
{
    const auto constraints = box_constraints();
    const auto request = scale_request();
    irop::LocalSolveLimits limits;
    limits.max_iterations = 1;
    irop::LocalSolveWorkspace workspace;
    const auto baseline = irop::solve_prepared_local_transform(constraints, request, workspace, limits);
    REQUIRE(baseline.status == irop::LocalSolveStatus::iteration_limit);
    CHECK(baseline.trace.empty());
    CHECK_FALSE(baseline.failed_snapshot.has_value());

    const auto captured = irop::solve_prepared_local_transform(
        constraints, request, workspace, limits, { .max_trace_records = 1, .max_failed_snapshot_constraints = 6 });
    CHECK(captured.status == baseline.status);
    CHECK(captured.work.iterations == baseline.work.iterations);
    CHECK(captured.work.constraint_rows_evaluated == baseline.work.constraint_rows_evaluated);
    CHECK(captured.work.jacobian_entries_evaluated == baseline.work.jacobian_entries_evaluated);
    REQUIRE(captured.trace.size() == 1);
    CHECK(captured.trace_records_dropped > 0);
    CHECK(captured.trace.front().iteration == captured.work.iterations);
    CHECK(std::isfinite(captured.trace.front().objective));
    CHECK(std::isfinite(captured.trace.front().primal_infeasibility));
    CHECK(std::isfinite(captured.trace.front().dual_infeasibility));
    REQUIRE(captured.failed_snapshot.has_value());
    CHECK_FALSE(captured.snapshot_omitted);

    irop::test::TempDirectory directory;
    const auto path = directory.path() / L"\u043f\u0440\u043e\u0431\u043b\u0435\u043c\u0430.json";
    irop::write_local_solve_snapshot(path, *captured.failed_snapshot);
    const auto saved = irop::read_local_solve_snapshot(path);
    CHECK(saved.request.initial_guess.volume_scale_multiplier == request.initial_guess.volume_scale_multiplier);
    REQUIRE(saved.constraints.size() == constraints.size());
    CHECK(saved.constraints.back().current_vertex.z == constraints.back().current_vertex.z);
    const auto replay = irop::solve_prepared_local_transform(saved.constraints, saved.request, workspace, saved.limits,
                                                             { .max_trace_records = 1 });
    CHECK(replay.status == captured.status);
    CHECK(replay.work.iterations == captured.work.iterations);
    CHECK(replay.work.objective_evaluations == captured.work.objective_evaluations);
    // An iteration-limit outcome intentionally publishes neither a candidate
    // nor an accepted transform. Compare the retained numerical evidence.
    CHECK_FALSE(replay.candidate_step.has_value());
    CHECK_FALSE(captured.candidate_step.has_value());
    CHECK_FALSE(replay.accepted_transform.has_value());
    REQUIRE(replay.trace.size() == captured.trace.size());
    CHECK(replay.trace_records_dropped == captured.trace_records_dropped);
    CHECK(replay.trace.front().objective == captured.trace.front().objective);
    CHECK(replay.trace.front().primal_infeasibility == captured.trace.front().primal_infeasibility);
    CHECK(replay.trace.front().dual_infeasibility == captured.trace.front().dual_infeasibility);
    CHECK(replay.trace.front().barrier_parameter == captured.trace.front().barrier_parameter);

    const auto omitted = irop::solve_prepared_local_transform(constraints, request, workspace, limits,
                                                              { .max_failed_snapshot_constraints = 5 });
    CHECK(omitted.status == baseline.status);
    CHECK(omitted.snapshot_omitted);
    CHECK_FALSE(omitted.failed_snapshot.has_value());
}

TEST_CASE("prepared local replay retains all normal validation and success checks", "[diagnostics][local-solve]")
{
    auto constraints = box_constraints();
    auto request = scale_request();
    irop::LocalSolveWorkspace workspace;
    const auto solved = irop::solve_prepared_local_transform(
        constraints, request, workspace, {}, { .max_trace_records = 3, .max_failed_snapshot_constraints = 6 });
    INFO(solved.diagnostic);
    REQUIRE(solved.succeeded());
    CHECK_FALSE(solved.failed_snapshot.has_value());
    CHECK(solved.work.minimum_applied_constraint.value() >= -request.tolerance);
    CHECK(solved.accepted_transform->volume_scale == Catch::Approx(std::pow(1.0 / 0.9, 3)).epsilon(1.0e-6));

    SECTION("non-unit normal") { constraints.front().inward_unit_normal = { 2, 0, 0 }; }
    SECTION("non-finite geometry") { constraints.front().current_vertex.x = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("invalid transform") { request.current_transform.volume_scale = 0.0; }
    SECTION("invalid initial guess") { request.initial_guess.volume_scale_multiplier = 9.0; }
    const auto invalid = irop::solve_prepared_local_transform(constraints, request, workspace);
    CHECK(invalid.status == irop::LocalSolveStatus::invalid_input);
    CHECK_FALSE(invalid.accepted_transform.has_value());
    CHECK(invalid.work.objective_evaluations == 0);
}

TEST_CASE("local snapshot parser rejects malformed and excessive input before replay", "[diagnostics][security]")
{
    irop::test::TempDirectory directory;
    const auto path = directory.path() / "snapshot.json";
    const irop::LocalSolveSnapshot snapshot { scale_request(), {}, box_constraints() };
    irop::write_local_solve_snapshot(path, snapshot);
    nlohmann::json json;
    {
        std::ifstream stream(path);
        stream >> json;
    }
    SECTION("byte budget")
    {
        irop::LocalSolveSnapshotReadLimits limits;
        limits.max_file_bytes = 1;
        CHECK_THROWS_AS(irop::read_local_solve_snapshot(path, limits), irop::Error);
        return;
    }
    SECTION("constraint budget")
    {
        irop::LocalSolveSnapshotReadLimits limits;
        limits.max_constraints = 5;
        CHECK_THROWS_AS(irop::read_local_solve_snapshot(path, limits), irop::Error);
        return;
    }
    SECTION("saved callback work allowance")
    {
        json["limits"]["max_constraint_rows_evaluated"] = std::numeric_limits<std::uint64_t>::max();
    }
    SECTION("saved elapsed allowance") { json["limits"]["max_elapsed_time_ms"] = 30'001; }
    SECTION("saved iteration allowance") { json["limits"]["max_iterations"] = 1'001; }
    SECTION("incorrect row") { json["constraints"][0].erase(0); }
    SECTION("invalid normal") { json["constraints"][0][6] = 2.0; }
    SECTION("non-number coordinate") { json["constraints"][0][0] = nullptr; }
    SECTION("negative count") { json["limits"]["max_iterations"] = -1; }
    SECTION("unsupported version") { json["schema_version"] = 2; }
    SECTION("unexpected field") { json["path"] = "not-followed.stl"; }
    SECTION("excessive nesting") { json["constraints"][0][0] = nlohmann::json::parse("[[[[[[0]]]]]]"); }
    SECTION("duplicate keys")
    {
        const std::string original = json.dump();
        std::ofstream stream(path);
        stream << "{\"kind\":\"irop-local-solve\"," << original.substr(1);
        stream.close();
        CHECK_THROWS_AS(irop::read_local_solve_snapshot(path), irop::Error);
        return;
    }
    write_json(path, json);
    CHECK_THROWS_AS(irop::read_local_solve_snapshot(path), irop::Error);
}

TEST_CASE("packing failure context and bounded details preserve the baseline outcome", "[diagnostics][packing]")
{
    const auto object = irop::center_mesh_at_vertex_centroid(irop::test::tetrahedron_mesh()).mesh;
    const auto container = irop::test::cube_mesh(5.0);
    irop::PackingConfig initial;
    initial.object_count = 1;
    initial.initial_volume_scale = 0.1;
    const auto state = irop::initialize_packing(object, container, initial);
    irop::PackingAlgorithmConfig config;
    config.final_volume_scale = 0.2;
    config.scale_step_count = 1;
    config.adaptive_sampling = false;
    irop::PackingEngineLimits limits;
    limits.local_solve.max_iterations = 1;
    const auto baseline = irop::run_packing(object, container, state, config, limits);
    REQUIRE(baseline.status == irop::PackingStatus::iteration_limit);
    REQUIRE(baseline.diagnostics.failure.has_value());
    CHECK(baseline.diagnostics.failure->trace.empty());
    CHECK(baseline.diagnostics.local_solve_records.empty());
    CHECK_FALSE(baseline.diagnostics.failed_local_problem.has_value());

    config.diagnostics.capture_failed_local_problem = true;
    config.diagnostics.max_trace_records_per_solve = 2;
    config.diagnostics.max_local_solve_records = 1;
    bool progress_observed = false;
    irop::PackingCallbacks callbacks;
    callbacks.progress = [&](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::local_solve_started) {
            progress_observed = true;
            CHECK(progress.object_id == 0);
            CHECK(progress.local_iteration_limit == 1);
            CHECK(progress.local_time_limit == limits.local_solve.max_elapsed_time);
            CHECK(progress.engine_time_limit == limits.max_elapsed_time);
        }
    };
    const auto recorded = irop::run_packing(object, container, state, config, limits, callbacks);
    CHECK(recorded.status == baseline.status);
    CHECK(recorded.work.local_solves == baseline.work.local_solves);
    CHECK(recorded.work.local_solve.iterations == baseline.work.local_solve.iterations);
    CHECK(recorded.state.random_state.draw_count() == baseline.state.random_state.draw_count());
    CHECK(recorded.state.transforms.front().volume_scale == state.transforms.front().volume_scale);
    REQUIRE(recorded.diagnostics.failure.has_value());
    const auto& failure = *recorded.diagnostics.failure;
    CHECK(failure.object_id == 0);
    CHECK(failure.scale_step == 0);
    CHECK(failure.iteration == 0);
    CHECK(failure.target_volume_scale == 0.2);
    CHECK(failure.status == irop::LocalSolveStatus::iteration_limit);
    CHECK(failure.limits.max_iterations == 1);
    CHECK(failure.reason.find("Ipopt iteration limit") != std::string::npos);
    CHECK_FALSE(failure.trace.empty());
    CHECK(recorded.diagnostics.local_solve_records.size() == 1);
    CHECK(recorded.diagnostics.failed_local_problem.has_value());
    CHECK(recorded.work.stage_timings.local_solve.count() > 0);
    CHECK(recorded.work.stage_timings.tetrahedralization.count() > 0);
    CHECK(progress_observed);
}

TEST_CASE("packing diagnostic limits reject excessive aggregate traces", "[diagnostics][security]")
{
    irop::PackingAlgorithmConfig config;
    config.diagnostics.max_local_solve_records = 64;
    config.diagnostics.max_trace_records_per_solve = 128;
    CHECK_NOTHROW(irop::validate_packing_algorithm_config(0.1, config, {}));
    config.diagnostics.max_local_solve_records = 65;
    CHECK_THROWS_AS(irop::validate_packing_algorithm_config(0.1, config, {}), irop::Error);
}

TEST_CASE("TetGen failure reason survives bounded recovery history truncation", "[diagnostics][packing][recovery]")
{
    const auto object = irop::center_mesh_at_vertex_centroid(irop::test::cylinder_mesh(0.9, 1.055, 12)).mesh;
    const auto container = irop::test::box_mesh(3.5, 4.0, 2.85);
    irop::PackingConfig initial;
    initial.object_count = 2;
    initial.initial_volume_scale = 0.999;
    initial.seed = 1918;
    const auto state = irop::initialize_packing(object, container, initial);
    irop::PackingAlgorithmConfig config;
    config.scale_step_count = 1;
    config.max_iterations_per_scale_step = 3;
    config.maximum_rotation_delta_radians = 0.0;
    config.adaptive_sampling = false;
    config.diagnostics.max_recovery_records = 1;
    const auto result = irop::run_packing(object, container, state, config);
    REQUIRE(result.status == irop::PackingStatus::dependency_failure);
    REQUIRE(result.diagnostics.recovery_records.size() == 1);
    CHECK(result.diagnostics.recovery_records_dropped == 2);
    CHECK(result.diagnostics.recovery_records.front().recovery_applied);
    CHECK_FALSE(result.diagnostics.recovery_records.front().reason.empty());
    CHECK(result.diagnostics.recovery_records.front().reason.size() <= 2'048);
    CHECK(result.work.tetrahedralization_recoveries == 2);
    CHECK(result.work.local_solves == 0);
}

}  // namespace
