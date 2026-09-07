#include <algorithm>
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
#include "irop/io/run_scene.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/optimization/local_solver.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/pack_scene.hpp"
#include "irop/packing/packing.hpp"
#include "source_hashes.hpp"
#include "support.hpp"

namespace {

using irop::benchmark::Clock;
using irop::benchmark::Json;
using irop::benchmark::Timer;

struct Options {
    std::string name;
    std::string label;
    std::filesystem::path output;
    std::filesystem::path object;
    std::filesystem::path container;
    std::filesystem::path run_output;
    double final_scale = 1.0;
    std::uint64_t scale_steps = 9;
    std::uint64_t local_iterations = 1'000;
    std::chrono::milliseconds local_timeout { 30'000 };
    bool adaptive_sampling = true;
    double rotation_delta = irop::PackingAlgorithmConfig {}.maximum_rotation_delta_radians;
    bool detailed_diagnostics = false;
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
            "usage: irop_benchmarks <init-sparse|init-dense|collision|stages|growth|pack> "
            "--output report.json [--count 10] [--segments 12] [--attempts 100000] "
            "[--seed 1918] [--initial-scale 0.1] [--timeout-ms 10000] [--label baseline]");
    }
    options.name = argv[1];
    if (options.name == "pack") {
        options.attempts = irop::PackingConfig::default_max_sampling_attempts;
        options.timeout = irop::PackingEngineLimits::default_max_elapsed_time;
    }
    for (int index = 2; index < argc; ++index) {
        const std::string_view key = argv[index];
        if (key == "--no-initialization-fallback") {
            options.initialization_fallback = false;
            continue;
        }
        if (key == "--no-adaptive-sampling") {
            options.adaptive_sampling = false;
            continue;
        }
        if (key == "--detailed-diagnostics") {
            options.detailed_diagnostics = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("benchmark option requires a value");
        }
        const std::string_view value = argv[++index];
        if (key == "--output") {
            options.output = irop::benchmark::path_from_utf8(value);
        }
        else if (key == "--object") {
            options.object = irop::benchmark::path_from_utf8(value);
        }
        else if (key == "--container") {
            options.container = irop::benchmark::path_from_utf8(value);
        }
        else if (key == "--run-output") {
            options.run_output = irop::benchmark::path_from_utf8(value);
        }
        else if (key == "--scale-steps") {
            options.scale_steps = positive_integer(value, 200);
        }
        else if (key == "--local-iterations") {
            options.local_iterations = positive_integer(value, 10'000);
        }
        else if (key == "--local-timeout-ms") {
            options.local_timeout = std::chrono::milliseconds(positive_integer(value, 300'000));
        }
        else if (key == "--label") {
            options.label = value;
        }
        else if (key == "--count") {
            options.count = positive_integer(value, 1'000);
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
            options.timeout = std::chrono::milliseconds(positive_integer(value, 300'000));
        }
        else if (key == "--initial-scale" || key == "--final-scale" || key == "--rotation-delta") {
            double scale = 0.0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), scale);
            if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || !std::isfinite(scale)) {
                throw std::invalid_argument("scale or rotation option must be a finite number");
            }
            if (key == "--rotation-delta") {
                if (scale < 0.0 || scale > irop::maximum_local_solve_rotation_delta_radians) {
                    throw std::invalid_argument("--rotation-delta must be in [0, pi] radians");
                }
                options.rotation_delta = scale;
            }
            else {
                if (scale <= 0.0 || scale > 1.0) {
                    throw std::invalid_argument("volume scales must be in (0, 1]");
                }
                if (key == "--initial-scale") {
                    options.initial_scale = scale;
                }
                else {
                    options.final_scale = scale;
                }
            }
        }
        else {
            throw std::invalid_argument("unknown benchmark option");
        }
    }
    if (options.output.empty() || std::filesystem::exists(std::filesystem::symlink_status(options.output))) {
        throw std::invalid_argument("--output must name a new report file");
    }
    if (options.name == "pack" && (options.object.empty() || options.container.empty() || options.run_output.empty())) {
        throw std::invalid_argument("pack requires --object STL --container STL --run-output NEW_DIRECTORY");
    }
    if (options.name == "pack" && options.initial_scale.value_or(0.1) > options.final_scale) {
        throw std::invalid_argument("initial scale must not exceed final scale");
    }
    return options;
}

[[nodiscard]] Json collision_work(const irop::SceneCollisionWork& work)
{
    return {
        { "object_pairs_examined",       work.object_pairs_examined       },
        { "triangle_pairs_tested",       work.triangle_pairs_tested       },
        { "cat_triangle_pairs_tested",   work.cat_triangle_pairs_tested   },
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
    report["configuration"]["use_reference_growth_policy"] = algorithm.use_reference_growth_policy;
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
    report["cat_diagnostics_complete"] = result.cat_diagnostics_complete;
    report["work"] = {
        { "iterations",                            result.work.iterations                                    },
        { "resamples",                             result.work.resampling_operations                         },
        { "tetgen_calls",                          result.work.tetrahedralization_attempts                   },
        { "sampling_refinements",                  result.work.sampling_refinements                          },
        { "physical_step_retries",                 result.work.physical_step_retries                         },
        { "omitted_single_participant_tetrahedra",
         result.work.tetrahedralization.omitted_single_participant_tetrahedra                                },
        { "cat_builds",                            result.work.cat_builds                                    },
        { "local_solves",                          result.work.local_solves                                  },
        { "solver_iterations",                     result.work.local_solve.iterations                        },
        { "constraint_rows_evaluated",             result.work.local_solve.constraint_rows_evaluated         },
        { "jacobian_entries_evaluated",            result.work.local_solve.jacobian_entries_evaluated        },
        { "hessian_evaluations",                   result.work.local_solve.hessian_evaluations               },
        { "hessian_constraint_rows_evaluated",     result.work.local_solve.hessian_constraint_rows_evaluated },
        { "collision",                             collision_work(result.work.collision)                     }
    };
}

[[nodiscard]] Json wall_stage(const double seconds)
{
    return {
        { "wall_ms", seconds * 1'000.0 },
        { "cpu_ms",  nullptr           }
    };
}

[[nodiscard]] Json solve_record(const irop::PackingLocalSolveRecord& record)
{
    return {
        { "object_id",                         record.object_id                              },
        { "scale_step",                        record.scale_step                             },
        { "iteration",                         record.iteration                              },
        { "target_volume_scale",               record.target_volume_scale                    },
        { "status",                            irop::to_string(record.status)                },
        { "reason",                            record.reason                                 },
        { "iteration_limit",                   record.limits.max_iterations                  },
        { "time_limit_ms",                     record.limits.max_elapsed_time.count()        },
        { "solver_iterations",                 record.work.iterations                        },
        { "constraints_prepared",              record.work.constraints_prepared              },
        { "constraint_rows_evaluated",         record.work.constraint_rows_evaluated         },
        { "jacobian_entries_evaluated",        record.work.jacobian_entries_evaluated        },
        { "hessian_evaluations",               record.work.hessian_evaluations               },
        { "hessian_constraint_rows_evaluated", record.work.hessian_constraint_rows_evaluated },
        { "elapsed_ms",                        record.work.elapsed_time.count()              },
        { "trace_records",                     record.trace.size()                           },
        { "trace_records_dropped",             record.trace_records_dropped                  },
    };
}

void pack_case(const Options& options, Json& report)
{
    irop::PackOptions packing;
    packing.initialization.object_count = options.count;
    packing.initialization.seed = options.seed;
    packing.initialization.initial_volume_scale = options.initial_scale.value_or(0.1);
    packing.initialization.max_sampling_attempts = options.attempts;
    packing.initialization.enable_structured_fallback = options.initialization_fallback;
    packing.initialization.max_structured_candidates = options.max_structured_candidates;
    packing.algorithm.final_volume_scale = options.final_scale;
    packing.algorithm.scale_step_count = options.scale_steps;
    packing.algorithm.adaptive_sampling = options.adaptive_sampling;
    packing.algorithm.maximum_rotation_delta_radians = options.rotation_delta;
    packing.algorithm.diagnostics.max_local_solve_records = options.detailed_diagnostics ? 128 : 1'000;
    packing.algorithm.diagnostics.max_trace_records_per_solve = options.detailed_diagnostics ? 64 : 0;
    packing.algorithm.diagnostics.capture_failed_local_problem = options.detailed_diagnostics;
    packing.limits.max_elapsed_time = options.timeout;
    packing.limits.local_solve.max_elapsed_time = options.local_timeout;
    packing.limits.local_solve.max_iterations = options.local_iterations;
    report["configuration"]["initial_volume_scale"] = packing.initialization.initial_volume_scale;
    report["configuration"]["final_volume_scale"] = packing.algorithm.final_volume_scale;
    report["configuration"]["scale_step_count"] = packing.algorithm.scale_step_count;
    report["configuration"]["adaptive_sampling"] = packing.algorithm.adaptive_sampling;
    report["configuration"]["use_reference_growth_policy"] = packing.algorithm.use_reference_growth_policy;
    report["configuration"]["maximum_rotation_delta_radians"] = packing.algorithm.maximum_rotation_delta_radians;
    report["configuration"]["max_iterations_per_scale_step"] = packing.algorithm.max_iterations_per_scale_step;
    report["configuration"]["local_iteration_limit"] = packing.limits.local_solve.max_iterations;
    report["configuration"]["local_timeout_ms"] = packing.limits.local_solve.max_elapsed_time.count();
    report["configuration"]["max_local_solve_records"] = packing.algorithm.diagnostics.max_local_solve_records;
    report["configuration"]["max_trace_records_per_solve"] = packing.algorithm.diagnostics.max_trace_records_per_solve;
    report["configuration"]["capture_failed_local_problem"] =
        packing.algorithm.diagnostics.capture_failed_local_problem;
    report["configuration"]["remaining_settings"] =
        "unchanged library defaults; exact resolved config and limits are in the retained run summary";
    report["requested_run_directory"] = irop::benchmark::path_utf8(std::filesystem::absolute(options.run_output));
    report["workload_kind"] = packing.initialization.initial_volume_scale == packing.algorithm.final_volume_scale
                                  ? "direct_placement"
                                  : "genuine_growth";
    report["timing_policy"] =
        "independent process; input hashing warms filesystem caches; stages are wall only; process CPU and lifetime "
        "peak memory include hashing, pack_scene, and saved-run loading; detailed stage times may include partial "
        "failed work";
    const Timer hashing;
    report["inputs"]["object"] = irop::benchmark::input_metadata(options.object, packing.input_limits.max_input_bytes);
    report["inputs"]["container"] =
        irop::benchmark::input_metadata(options.container, packing.input_limits.max_input_bytes);
    report["stages"]["input_hashing"] = hashing.elapsed();
    const Timer service;
    const auto result = irop::pack_scene(options.object, options.container, options.run_output, packing);
    report["stages"]["pack_scene_total"] = service.elapsed();
    report["stages"]["input_preparation"] = wall_stage(result.timings.preparation_seconds);
    report["stages"]["initialization"] = wall_stage(result.timings.initialization_seconds);
    report["stages"]["packing_engine"] = wall_stage(result.timings.packing_seconds);
    report["stages"]["output_validation"] = wall_stage(result.timings.output_validation_seconds);
    report["stages"]["export"] = wall_stage(result.timings.export_seconds);
    const auto& timings = result.packing.work.stage_timings;
    report["stages"]["resampling"] = wall_stage(std::chrono::duration<double>(timings.resampling).count());
    report["stages"]["transforms"] = wall_stage(std::chrono::duration<double>(timings.transform).count());
    report["stages"]["tetrahedralization"] =
        wall_stage(std::chrono::duration<double>(timings.tetrahedralization).count());
    report["stages"]["cat"] = wall_stage(std::chrono::duration<double>(timings.cat).count());
    report["stages"]["local_solves"] = wall_stage(std::chrono::duration<double>(timings.local_solve).count());
    report["stages"]["correction"] = wall_stage(std::chrono::duration<double>(timings.correction).count());
    report["stages"]["final_validation"] = wall_stage(std::chrono::duration<double>(timings.final_validation).count());
    const auto& engine = result.packing;
    report["status"] = irop::to_string(engine.status);
    report["diagnostic"] = engine.diagnostic;
    report["physically_valid"] =
        engine.final_validation_performed ? Json(engine.final_validation.physical_scene_valid()) : Json(nullptr);
    report["run_summary"] = irop::benchmark::path_utf8(result.run_summary_path);
    report["placements"] = placements(engine.state);
    report["cat_diagnostics_complete"] = engine.cat_diagnostics_complete;
    report["initialization_work"] = initialization_work(engine.state);
    report["meshes"] = {
        { "object",
         { { "vertices", result.centered_object_statistics.vertex_count },
            { "triangles", result.centered_object_statistics.triangle_count } } },
        { "container",
         { { "vertices", result.container_statistics.vertex_count },
            { "triangles", result.container_statistics.triangle_count } }       },
    };
    report["work"] = {
        { "completed_scale_steps",                 engine.work.completed_scale_steps                         },
        { "iterations",                            engine.work.iterations                                    },
        { "resamples",                             engine.work.resampling_operations                         },
        { "tetgen_calls",                          engine.work.tetrahedralization_attempts                   },
        { "sampling_refinements",                  engine.work.sampling_refinements                          },
        { "physical_step_retries",                 engine.work.physical_step_retries                         },
        { "tetgen_recoveries",                     engine.work.tetrahedralization_recoveries                 },
        { "tetrahedra",                            engine.work.tetrahedralization.output_tetrahedra          },
        { "omitted_single_participant_tetrahedra",
         engine.work.tetrahedralization.omitted_single_participant_tetrahedra                                },
        { "cat_builds",                            engine.work.cat_builds                                    },
        { "cat_constraints",                       engine.work.cat.constraints_generated                     },
        { "local_solves",                          engine.work.local_solves                                  },
        { "solver_iterations",                     engine.work.local_solve.iterations                        },
        { "constraint_rows_evaluated",             engine.work.local_solve.constraint_rows_evaluated         },
        { "jacobian_entries_evaluated",            engine.work.local_solve.jacobian_entries_evaluated        },
        { "hessian_evaluations",                   engine.work.local_solve.hessian_evaluations               },
        { "hessian_constraint_rows_evaluated",     engine.work.local_solve.hessian_constraint_rows_evaluated },
        { "correction_passes",                     engine.work.correction_passes                             },
        { "collision",                             collision_work(engine.work.collision)                     },
    };
    report["diagnostics"]["local_solves"] = Json::array();
    for (const auto& record : engine.diagnostics.local_solve_records) {
        report["diagnostics"]["local_solves"].push_back(solve_record(record));
    }
    report["diagnostics"]["local_solve_records_dropped"] = engine.diagnostics.local_solve_records_dropped;
    report["diagnostics"]["failure"] =
        engine.diagnostics.failure ? solve_record(*engine.diagnostics.failure) : Json(nullptr);
    report["diagnostics"]["recovery_records_dropped"] = engine.diagnostics.recovery_records_dropped;
    report["diagnostics"]["failed_snapshot_omitted"] = engine.diagnostics.failed_snapshot_omitted;
    report["diagnostics"]["full_record"] = "bounded traces and TetGen recovery reasons are in the retained run summary";
    report["completed_at_exact_target"] =
        engine.succeeded() && std::all_of(engine.state.transforms.begin(), engine.state.transforms.end(),
                                          [&](const irop::Transform& transform) {
        return transform.volume_scale == packing.algorithm.final_volume_scale;
    });
    const Timer loading;
    try {
        const auto loaded = irop::load_run_scene(result.run_summary_path);
        report["stages"]["saved_run_loading"] = loading.elapsed();
        report["saved_run"]["status"] = "loaded";
        report["saved_run"]["object_count"] = loaded.object_count;
        report["saved_run"]["has_geometry"] = loaded.objects.has_value();
    }
    catch (const std::exception& error) {
        report["stages"]["saved_run_loading"] = loading.elapsed();
        report["saved_run"]["status"] = "load_failed";
        report["saved_run"]["diagnostic"] = error.what();
    }
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
    report["work"]["omitted_single_participant_tetrahedra"] = tetrahedra.work.omitted_single_participant_tetrahedra;
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
    report["work"]["solver_hessian_evaluations"] = solved.work.hessian_evaluations;
    report["work"]["solver_hessian_constraint_rows"] = solved.work.hessian_constraint_rows_evaluated;
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
    if (artifact == options.output || std::filesystem::exists(std::filesystem::symlink_status(artifact))) {
        throw std::invalid_argument("benchmark STL artifact path must not already exist or equal the report path");
    }
    const Timer serialization;
    irop::write_stl(artifact, participants[0]);
    report["stages"]["stl_serialization"] = serialization.elapsed();
    report["serialized_bytes"] = std::filesystem::file_size(artifact);
    report["artifact"] = irop::benchmark::path_utf8(artifact);
    report["status"] = solved.succeeded() && validation.physical_scene_valid() ? "success" : "unsuccessful";
}

}  // namespace

int wmain(const int argc, wchar_t** wide_arguments)
{
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(irop::benchmark::path_utf8(std::filesystem::path(wide_arguments[index])));
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size());
        for (std::string& argument : arguments) {
            argv.push_back(argument.data());
        }
        const Options options = parse_options(argc, argv.data());
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
                { "collision_sha256_at_configure", IROP_BENCHMARK_COLLISION_SHA256 },
                { "source_sha256_at_configure", Json::parse(irop::benchmark::source_hashes) } }           },
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
            else if (options.name == "pack") {
                pack_case(options, report);
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
        irop::benchmark::write_report(options.output, report.dump(2) + '\n');
        std::cout << irop::benchmark::path_utf8(options.output) << '\n';
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
