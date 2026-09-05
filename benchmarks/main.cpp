#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "irop/cat/cat.hpp"
#include "irop/error.hpp"
#include "irop/geometry/collision.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/optimization/local_solver.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/packing.hpp"
#include "support.hpp"

namespace {

using irop::benchmark::Clock;
using irop::benchmark::Json;
using irop::benchmark::Timer;

struct Options {
    std::string name;
    std::string label;
    std::filesystem::path output;
    std::uint64_t count = 10;
    std::size_t segments = 12;
    std::uint64_t attempts = 100'000;
    std::uint32_t seed = 1918;
    std::optional<double> initial_scale;
    bool initialization_fallback = true;
    std::uint64_t max_structured_candidates = irop::PackingConfig::default_max_structured_candidates;
    std::chrono::milliseconds timeout { 10'000 };
};

[[nodiscard]] std::uint64_t positive_integer(const std::string_view value, const std::uint64_t maximum)
{
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || result == 0 || result > maximum) {
        throw std::invalid_argument("benchmark numeric option is invalid or exceeds its documented harness bound");
    }
    return result;
}

[[nodiscard]] Options parse_options(const int argc, char** argv)
{
    Options options;
    if (argc < 2) {
        throw std::invalid_argument(
            "usage: irop_benchmarks <init-sparse|init-dense|collision|stages|growth> "
            "--output report.json [--count 10] [--segments 12] [--attempts 100000] "
            "[--seed 1918] [--initial-scale 0.1] [--timeout-ms 10000] [--label baseline]");
    }
    options.name = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view key = argv[index];
        if (key == "--no-initialization-fallback") {
            options.initialization_fallback = false;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("benchmark option requires a value");
        }
        const std::string_view value = argv[++index];
        if (key == "--output") {
            options.output = std::filesystem::path(value);
        }
        else if (key == "--label") {
            options.label = value;
        }
        else if (key == "--count") {
            options.count = positive_integer(value, 512);
        }
        else if (key == "--segments") {
            options.segments = static_cast<std::size_t>(positive_integer(value, 512));
        }
        else if (key == "--attempts") {
            options.attempts = positive_integer(value, 10'000'000);
        }
        else if (key == "--max-structured-candidates") {
            options.max_structured_candidates = positive_integer(value, 1'000'000);
        }
        else if (key == "--seed") {
            options.seed = value == "0" ? 0 : static_cast<std::uint32_t>(positive_integer(value, UINT32_MAX));
        }
        else if (key == "--timeout-ms") {
            options.timeout = std::chrono::milliseconds(positive_integer(value, 60'000));
        }
        else if (key == "--initial-scale") {
            double scale = 0.0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), scale);
            if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || !std::isfinite(scale) ||
                scale <= 0.0 || scale > 1.0) {
                throw std::invalid_argument("--initial-scale must be finite and in (0, 1]");
            }
            options.initial_scale = scale;
        }
        else {
            throw std::invalid_argument("unknown benchmark option");
        }
    }
    if (options.output.empty() || std::filesystem::exists(options.output)) {
        throw std::invalid_argument("--output must name a new report file");
    }
    return options;
}

[[nodiscard]] Json collision_work(const irop::SceneCollisionWork& work)
{
    return {
        { "object_pairs_examined",       work.object_pairs_examined       },
        { "triangle_pairs_tested",       work.triangle_pairs_tested       },
        { "containment_triangle_visits", work.containment_triangle_visits }
    };
}

[[nodiscard]] Json initialization_work(const irop::PackingState& state)
{
    return {
        { "accepted_objects",                    state.transforms.size()                      },
        { "method",                              irop::to_string(state.initialization_method) },
        { "sampling_attempts",                   state.sampling_attempts                      },
        { "structured_candidates",               state.structured_candidates                  },
        { "orientations_examined",               state.orientations_examined                  },
        { "reference_accepted_count",            state.reference_accepted_count               },
        { "random_draws",                        state.random_state.draw_count()              },
        { "rejected_candidates",                 state.rejected_candidate_count               },
        { "geometry_query_triangle_visits",      state.geometry_query_triangle_visits         },
        { "pairwise_distance_checks",            state.pairwise_distance_checks               },
        { "surface_intersection_triangle_pairs", state.surface_intersection_triangle_pairs    }
    };
}

[[nodiscard]] Json placements(const irop::PackingState& state)
{
    Json values = Json::array();
    for (const auto& transform : state.transforms) {
        values.push_back({
            { "volume_scale",     transform.volume_scale                                                        },
            { "rotation_radians", { transform.rotation.x, transform.rotation.y, transform.rotation.z }          },
            { "translation",      { transform.translation.x, transform.translation.y, transform.translation.z } }
        });
    }
    return values;
}

void initialization_case(const Options& options, Json& report)
{
    const auto object = irop::benchmark::cylinder(45.23, 52.7535, options.segments);
    const auto container = irop::benchmark::box(175.0, 200.0, 142.5);
    irop::PackingConfig config;
    config.object_count = options.count;
    config.initial_volume_scale = options.initial_scale.value_or(options.name == "init-dense" ? 1.0 : 0.1);
    config.seed = options.seed;
    config.max_sampling_attempts = options.attempts;
    config.enable_structured_fallback = options.initialization_fallback;
    config.max_structured_candidates = options.max_structured_candidates;
    report["meshes"] = {
        { "object",    irop::benchmark::mesh_metadata(object)    },
        { "container", irop::benchmark::mesh_metadata(container) }
    };
    report["configuration"]["initial_volume_scale"] = config.initial_volume_scale;
    report["configuration"]["cylinder_diameter"] = 90.46;
    report["configuration"]["cylinder_height"] = 105.507;
    report["configuration"]["container_dimensions"] = { 350.0, 400.0, 285.0 };
    const Timer timer;
    try {
        const auto state = irop::initialize_packing(object, container, config, [&]() {
            return Clock::now() - timer.started >= options.timeout;
        });
        report["stages"]["initialization"] = timer.elapsed();
        report["work"] = initialization_work(state);
        report["placements"] = placements(state);
        report["status"] = "success";
    }
    catch (const irop::Error& error) {
        report["stages"]["initialization"] = timer.elapsed();
        report["status"] = irop::to_string(error.category());
        report["diagnostic"] = error.what();
        report["work"] = nullptr;
        report["work_unavailable_reason"] = "initialization API throws without returning partial state";
    }
}

void collision_case(const Options& options, Json& report)
{
    const auto object = irop::benchmark::cylinder(0.5, 0.5, options.segments);
    const auto container = irop::benchmark::box(static_cast<double>(options.count) + 1.0, 2.0, 2.0);
    std::vector<irop::TriangleMesh> objects;
    const Timer transformation;
    for (std::uint64_t index = 0; index < options.count; ++index) {
        irop::Transform transform;
        transform.translation.x = 2.0 * static_cast<double>(index) - static_cast<double>(options.count) + 1.0;
        objects.push_back(irop::transform_mesh(object, transform));
    }
    report["stages"]["transforms"] = transformation.elapsed();
    report["meshes"] = {
        { "object",    irop::benchmark::mesh_metadata(object)    },
        { "container", irop::benchmark::mesh_metadata(container) }
    };
    const Timer collision;
    const auto result = irop::validate_scene_collisions(objects, container);
    report["stages"]["collision"] = collision.elapsed();
    report["work"] = collision_work(result.work);
    report["physically_valid"] = result.physical_scene_valid();
    report["status"] = result.physical_scene_valid() ? "success" : "invalid_scene";
}

void growth_case(const Options& options, Json& report)
{
    const auto object = irop::benchmark::tetrahedron();
    const auto container = irop::benchmark::box(2.0, 2.0, 2.0);
    irop::PackingConfig initialization;
    initialization.seed = options.seed;
    initialization.max_sampling_attempts = options.attempts;
    initialization.enable_structured_fallback = options.initialization_fallback;
    initialization.max_structured_candidates = options.max_structured_candidates;
    const Timer initialize;
    auto state = irop::initialize_packing(object, container, initialization);
    report["stages"]["initialization"] = initialize.elapsed();
    report["initialization_work"] = initialization_work(state);
    irop::PackingAlgorithmConfig algorithm;
    algorithm.final_volume_scale = 0.2;
    algorithm.scale_step_count = 1;
    algorithm.max_iterations_per_scale_step = 4;
    algorithm.maximum_rotation_delta_radians = 0.0;
    irop::PackingEngineLimits limits;
    limits.max_elapsed_time = options.timeout;
    limits.local_solve.max_elapsed_time = options.timeout;
    limits.local_solve.max_iterations = 200;
    report["configuration"]["object_count"] = 1;
    report["configuration"]["initial_volume_scale"] = 0.1;
    report["configuration"]["final_volume_scale"] = algorithm.final_volume_scale;
    report["configuration"]["adaptive_sampling"] = true;
    report["configuration"]["max_iterations_per_scale_step"] = algorithm.max_iterations_per_scale_step;
    report["meshes"] = {
        { "object",    irop::benchmark::mesh_metadata(object)    },
        { "container", irop::benchmark::mesh_metadata(container) }
    };
    const Timer engine;
    const auto result = irop::run_packing(object, container, std::move(state), algorithm, limits);
    report["stages"]["packing_engine"] = engine.elapsed();
    report["status"] = irop::to_string(result.status);
    report["diagnostic"] = result.diagnostic;
    report["physically_valid"] = result.final_validation_performed && result.final_validation.physical_scene_valid();
    report["placements"] = placements(result.state);
    report["work"] = {
        { "iterations",                 result.work.iterations                             },
        { "resamples",                  result.work.resampling_operations                  },
        { "tetgen_calls",               result.work.tetrahedralization_attempts            },
        { "cat_builds",                 result.work.cat_builds                             },
        { "local_solves",               result.work.local_solves                           },
        { "solver_iterations",          result.work.local_solve.iterations                 },
        { "constraint_rows_evaluated",  result.work.local_solve.constraint_rows_evaluated  },
        { "jacobian_entries_evaluated", result.work.local_solve.jacobian_entries_evaluated },
        { "collision",                  collision_work(result.work.collision)              }
    };
}

void stages_case(const Options& options, Json& report)
{
    auto object = irop::benchmark::tetrahedron();
    auto container = irop::benchmark::box(2.0, 2.0, 2.0);
    report["configuration"]["object_count"] = 1;
    report["configuration"]["description"] = "one independently measured local growth pipeline; not an engine run";
    const Timer resampling;
    object = irop::resample_closed_surface(object, 16).mesh;
    container = irop::resample_closed_surface(container, 48).mesh;
    report["stages"]["resampling"] = resampling.elapsed();
    report["meshes"] = {
        { "object",    irop::benchmark::mesh_metadata(object)    },
        { "container", irop::benchmark::mesh_metadata(container) }
    };
    irop::Transform transform;
    transform.volume_scale = 0.1;
    const Timer transformation;
    std::vector<irop::TriangleMesh> participants { irop::transform_mesh(object, transform), container };
    report["stages"]["transforms"] = transformation.elapsed();
    const Timer tetrahedralization;
    const auto tetrahedra = irop::tetrahedralize_surfaces(participants);
    report["stages"]["tetrahedralization"] = tetrahedralization.elapsed();
    report["work"]["tetrahedra"] = tetrahedra.work.output_tetrahedra;
    if (!tetrahedra.succeeded()) {
        report["status"] = irop::to_string(tetrahedra.status);
        report["diagnostic"] = tetrahedra.diagnostic;
        return;
    }
    const Timer cat_timer;
    const auto cat = irop::build_cat(tetrahedra.mesh);
    report["stages"]["cat"] = cat_timer.elapsed();
    report["work"]["cat_constraints"] = cat.work.constraints_generated;
    if (!cat.succeeded()) {
        report["status"] = irop::to_string(cat.status);
        report["diagnostic"] = cat.diagnostic;
        return;
    }
    const Timer constraint_timer;
    double checksum = 0.0;
    constexpr std::uint64_t repetitions = 100;
    std::uint64_t rows = 0;
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& constraint : cat.constraints) {
            if (constraint.owner != 0) {
                continue;
            }
            const irop::LocalPlaneConstraint local { tetrahedra.mesh.points.at(constraint.source_point),
                                                     constraint.plane_point, constraint.inward_unit_normal };
            checksum += irop::evaluate_local_constraint({}, local, 0.0, {});
            for (const double derivative : irop::evaluate_local_constraint_gradient({}, local, 0.0, {})) {
                checksum += derivative;
            }
            ++rows;
        }
    }
    report["stages"]["constraint_and_analytic_gradient"] = constraint_timer.elapsed();
    report["work"]["standalone_constraint_rows"] = rows;
    report["work"]["constraint_checksum"] = checksum;
    irop::LocalSolveRequest request;
    request.current_transform = transform;
    request.bounds.maximum_volume_scale_multiplier = 2.00004;
    request.bounds.maximum_absolute_translation = 1.0;
    request.maximum_result_volume_scale = 0.2;
    irop::LocalSolveWorkspace workspace;
    irop::LocalSolveLimits limits;
    limits.max_elapsed_time = options.timeout;
    const Timer solver_timer;
    const auto solved = irop::solve_local_transform(tetrahedra.mesh, cat, request, workspace, limits);
    report["stages"]["ipopt_solve"] = solver_timer.elapsed();
    report["solver_status"] = irop::to_string(solved.status);
    report["work"]["solver_iterations"] = solved.work.iterations;
    report["work"]["solver_constraint_rows"] = solved.work.constraint_rows_evaluated;
    report["work"]["solver_jacobian_entries"] = solved.work.jacobian_entries_evaluated;
    if (solved.accepted_transform.has_value()) {
        participants[0] = irop::transform_mesh(object, *solved.accepted_transform);
    }
    const Timer collision;
    const auto validation = irop::validate_scene_collisions(std::span(participants).first(1), container);
    report["stages"]["collision"] = collision.elapsed();
    report["work"]["collision"] = collision_work(validation.work);
    report["physically_valid"] = validation.physical_scene_valid();
    auto artifact = options.output;
    artifact.replace_extension(".stl");
    if (artifact == options.output || std::filesystem::exists(artifact)) {
        throw std::invalid_argument("benchmark STL artifact path must not already exist or equal the report path");
    }
    const Timer serialization;
    irop::write_stl(artifact, participants[0]);
    report["stages"]["stl_serialization"] = serialization.elapsed();
    report["serialized_bytes"] = std::filesystem::file_size(artifact);
    report["artifact"] = artifact.generic_string();
    report["status"] = solved.succeeded() && validation.physical_scene_valid() ? "success" : "unsuccessful";
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        const Options options = parse_options(argc, argv);
        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path());
        }
        Json report = {
            { "schema_version", 1                                                                         },
            { "case",           options.name                                                              },
            { "label",          options.label                                                             },
            { "build",
             { { "configuration", IROP_BENCHMARK_BUILD_CONFIG },
                { "compiler", IROP_BENCHMARK_COMPILER },
                { "revision_at_configure", IROP_BENCHMARK_REVISION },
                { "initializer_sha256_at_configure", IROP_BENCHMARK_INITIALIZER_SHA256 },
                { "collision_sha256_at_configure", IROP_BENCHMARK_COLLISION_SHA256 } }                    },
            { "dependencies",
             { { "vtk", IROP_BENCHMARK_VTK_VERSION },
                { "tetgen", "1.6.0" },
                { "ipopt", "3.14.19" },
                { "eigen", "3.4.1" } }                                                                    },
            { "machine",        irop::benchmark::machine_metadata()                                       },
            { "configuration",
             { { "object_count", options.count },
                { "cylinder_segments", options.segments },
                { "seed", options.seed },
                { "max_sampling_attempts", options.attempts },
                { "enable_structured_fallback", options.initialization_fallback },
                { "max_structured_candidates", options.max_structured_candidates },
                { "timeout_ms", options.timeout.count() },
                { "orchestration_threads", 1 },
                { "dependency_thread_policy", "unchanged library defaults" } }                            },
            { "timing_policy",
             "cold first call; wall and process CPU; no warmup; run repeated cases in separate processes" },
        };
        const Timer total;
        try {
            if (options.name == "init-sparse" || options.name == "init-dense") {
                initialization_case(options, report);
            }
            else if (options.name == "collision") {
                collision_case(options, report);
            }
            else if (options.name == "growth") {
                growth_case(options, report);
            }
            else if (options.name == "stages") {
                stages_case(options, report);
            }
            else {
                throw std::invalid_argument("unknown benchmark case");
            }
        }
        catch (const irop::Error& error) {
            report["status"] = irop::to_string(error.category());
            report["diagnostic"] = error.what();
            report["interrupted_stage_has_no_completed_measurement"] = true;
        }
        catch (const std::exception& error) {
            report["status"] = "benchmark_error";
            report["diagnostic"] = error.what();
            report["interrupted_stage_has_no_completed_measurement"] = true;
        }
        report["total"] = total.elapsed();
        report["memory"] = irop::benchmark::memory_measurements();
        std::ofstream output(options.output);
        output << report.dump(2) << '\n';
        output.close();
        if (!output) {
            throw std::runtime_error("failed to write benchmark report");
        }
        std::cout << options.output.generic_string() << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 2;
    }
    catch (...) {
        std::fputs("unexpected benchmark failure\n", stderr);
        return 2;
    }
}
