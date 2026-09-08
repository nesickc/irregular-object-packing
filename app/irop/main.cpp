#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <string>

#include "irop/error.hpp"
#include "irop/initialization/initialize_scene.hpp"
#include "irop/inspection/inspect_stl.hpp"
#include "irop/io/local_solve_replay.hpp"
#include "irop/model/triangle_mesh.hpp"
#include "irop/packing/pack_scene.hpp"

#ifndef IROP_VERSION
#define IROP_VERSION "development"
#endif

namespace {

enum class ExitCode : int {
    success = 0,
    usage = 2,
    input = 3,
    resource_limit = 4,
    output_io = 5,
    unsuccessful = 6,
    internal = 70,
    cancelled = 130,
};

volatile std::sig_atomic_t interruption_requested = 0;

void handle_interruption(const int) noexcept { interruption_requested = 1; }

[[nodiscard]] ExitCode exit_code_for(const irop::ErrorCategory category) noexcept
{
    switch (category) {
    case irop::ErrorCategory::invalid_configuration:
        return ExitCode::input;
    case irop::ErrorCategory::input_io:
    case irop::ErrorCategory::invalid_mesh:
        return ExitCode::input;
    case irop::ErrorCategory::resource_limit:
        return ExitCode::resource_limit;
    case irop::ErrorCategory::output_io:
        return ExitCode::output_io;
    case irop::ErrorCategory::dependency_failure:
        return ExitCode::internal;
    case irop::ErrorCategory::cancelled:
        return ExitCode::cancelled;
    case irop::ErrorCategory::internal:
        return ExitCode::internal;
    }
    return ExitCode::internal;
}

[[nodiscard]] ExitCode exit_code_for(const irop::PackingStatus status) noexcept
{
    switch (status) {
    case irop::PackingStatus::success:
        return ExitCode::success;
    case irop::PackingStatus::cancelled:
        return ExitCode::cancelled;
    case irop::PackingStatus::resource_exhausted:
    case irop::PackingStatus::time_limit:
        return ExitCode::resource_limit;
    case irop::PackingStatus::invalid_input:
    case irop::PackingStatus::infeasible:
    case irop::PackingStatus::iteration_limit:
    case irop::PackingStatus::correction_limit:
    case irop::PackingStatus::numerical_failure:
    case irop::PackingStatus::dependency_failure:
    case irop::PackingStatus::internal_failure:
        return ExitCode::unsuccessful;
    }
    return ExitCode::internal;
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

void log_error_noexcept(const char* category, const char* message) noexcept
{
    try {
        spdlog::error("{}: {}", category, message);
    }
    catch (...) {
        // Diagnostics must never obscure the stable process exit code.
    }
}

[[nodiscard]] int run_application(const int argc, char** argv)
{
    CLI::App application { "Inspect and pack repeated irregular triangular meshes" };
    application.set_version_flag("--version", IROP_VERSION);
    application.require_subcommand(1);

    std::filesystem::path input_path;
    std::filesystem::path inspection_output_directory;
    irop::MeshLimits inspection_limits;

    CLI::App* inspect_command = application.add_subcommand("inspect", "Validate and normalize an STL mesh");
    inspect_command->add_option("input", input_path, "Input STL path")->required();
    inspect_command
        ->add_option("-o,--output,--output-dir", inspection_output_directory,
                     "New artifact paths under this output directory")
        ->required();
    inspect_command
        ->add_option("--max-input-bytes", inspection_limits.max_input_bytes, "Maximum accepted input file size")
        ->check(CLI::PositiveNumber);
    inspect_command->add_option("--max-vertices", inspection_limits.max_vertices, "Maximum accepted vertex count")
        ->check(CLI::PositiveNumber);
    inspect_command->add_option("--max-triangles", inspection_limits.max_triangles, "Maximum accepted triangle count")
        ->check(CLI::PositiveNumber);

    std::filesystem::path object_path;
    std::filesystem::path container_path;
    std::filesystem::path initialization_output_directory;
    irop::InitializationOptions initialization_options;
    CLI::App* initialize_command =
        application.add_subcommand("initialize", "Create a deterministic initial placement scene");
    initialize_command->add_option("--object", object_path, "Full-size object-template STL path")->required();
    initialize_command->add_option("--container", container_path, "Closed container STL path")->required();
    initialize_command
        ->add_option("-o,--output,--output-dir", initialization_output_directory,
                     "New artifact paths under this output directory")
        ->required();
    initialize_command->add_option("--count", initialization_options.packing.object_count, "Number of object copies")
        ->required()
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--initial-volume-scale", initialization_options.packing.initial_volume_scale,
                     "Initial object volume scale in (0, 1]")
        ->check(CLI::Range(0.0, 1.0));
    initialize_command->add_option("--seed", initialization_options.packing.seed, "Deterministic random seed");
    initialize_command
        ->add_option("--max-sampling-attempts", initialization_options.packing.max_sampling_attempts,
                     "Maximum candidate centers sampled across the run")
        ->check(CLI::PositiveNumber);
    initialize_command->add_flag("!--no-initialization-fallback",
                                 initialization_options.packing.enable_structured_fallback,
                                 "Disable structured placement after random sampling exhausts its attempt limit");
    initialize_command
        ->add_option("--max-structured-candidates", initialization_options.packing.max_structured_candidates,
                     "Maximum candidate placements across all structured fallback orientations")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-geometry-query-triangle-visits",
                     initialization_options.packing.max_geometry_query_triangle_visits,
                     "Maximum container-triangle visits across initialization queries")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-pairwise-distance-checks", initialization_options.packing.max_pairwise_distance_checks,
                     "Maximum center-distance or structured-envelope separation checks across initialization")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-surface-intersection-triangle-pairs",
                     initialization_options.packing.max_surface_intersection_triangle_pairs,
                     "Maximum triangle-pair tests for exact surface containment")
        ->check(CLI::PositiveNumber);
    initialize_command->add_flag("--individual-stls,--write-individual-stls",
                                 initialization_options.write_individual_objects,
                                 "Also write one STL per initialized object");
    initialize_command
        ->add_option("--max-input-bytes", initialization_options.input_limits.max_input_bytes,
                     "Maximum accepted size of each input STL")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-input-vertices", initialization_options.input_limits.max_vertices,
                     "Maximum accepted vertex count per input mesh")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-input-triangles", initialization_options.input_limits.max_triangles,
                     "Maximum accepted triangle count per input mesh")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-output-vertices", initialization_options.packing.output_mesh_limits.max_vertices,
                     "Maximum combined initialized-object vertex count")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-output-triangles", initialization_options.packing.output_mesh_limits.max_triangles,
                     "Maximum combined initialized-object triangle count")
        ->check(CLI::PositiveNumber);

    std::filesystem::path packing_object_path;
    std::filesystem::path packing_container_path;
    std::filesystem::path packing_output_directory;
    irop::PackOptions pack_options;
    bool disable_adaptive_sampling = false;
    std::uint64_t maximum_packing_milliseconds =
        static_cast<std::uint64_t>(pack_options.limits.max_elapsed_time.count());
    std::uint64_t maximum_local_solve_milliseconds =
        static_cast<std::uint64_t>(pack_options.limits.local_solve.max_elapsed_time.count());
    CLI::App* pack_command = application.add_subcommand("pack", "Run the bounded end-to-end packing loop");
    pack_command->add_option("--object", packing_object_path, "Full-size object-template STL path")->required();
    pack_command->add_option("--container", packing_container_path, "Closed container STL path")->required();
    pack_command
        ->add_option("-o,--output,--output-dir", packing_output_directory,
                     "New artifact-set directory (must not already exist)")
        ->required();
    pack_command->add_option("--count", pack_options.initialization.object_count, "Number of object copies")
        ->required()
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--initial-volume-scale", pack_options.initialization.initial_volume_scale,
                     "Initial object volume scale in (0, 1]")
        ->check(CLI::Range(0.0, 1.0));
    pack_command
        ->add_option("--final-volume-scale", pack_options.algorithm.final_volume_scale,
                     "Final target object volume scale in (0, 1]")
        ->check(CLI::Range(0.0, 1.0));
    pack_command
        ->add_option("--scale-steps", pack_options.algorithm.scale_step_count,
                     "Number of scale barriers, including the final target")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-iterations-per-scale-step", pack_options.algorithm.max_iterations_per_scale_step,
                     "Maximum solve/correction iterations at each scale barrier")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--maximum-rotation-delta-radians", pack_options.algorithm.maximum_rotation_delta_radians,
                     "Configured symmetric local rotation delta before the reference 0.9 multiplier")
        ->check(CLI::NonNegativeNumber);
    pack_command
        ->add_option("--maximum-translation-per-unit-scale", pack_options.algorithm.maximum_translation_per_unit_scale,
                     "Base local translation delta; defaults to twice the object equivalent length")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--padding", pack_options.algorithm.padding,
                     "Nonnegative CAT plane-constraint padding in input coordinate units")
        ->check(CLI::NonNegativeNumber);
    pack_command
        ->add_option("--correction-volume-scale-factor", pack_options.algorithm.correction_volume_scale_factor,
                     "Volume-scale multiplier for physically colliding objects")
        ->check(CLI::Range(0.0, 1.0));
    pack_command
        ->add_option("--tetra-recovery-volume-scale-factor",
                     pack_options.algorithm.tetrahedralization_recovery_scale_factor,
                     "Volume-scale multiplier after a recoverable TetGen failure")
        ->check(CLI::Range(0.0, 1.0));
    pack_command
        ->add_option("--local-solve-tolerance", pack_options.algorithm.local_solve_tolerance,
                     "Ipopt local-solve tolerance")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--sampling-alpha", pack_options.algorithm.sampling.alpha, "Adaptive surface-sampling alpha")
        ->check(CLI::Range(0.0, 1.0));
    pack_command->add_option("--sampling-beta", pack_options.algorithm.sampling.beta, "Adaptive surface-sampling beta")
        ->check(CLI::Range(0.0, 1.0));
    pack_command
        ->add_option("--minimum-sampled-triangles", pack_options.algorithm.sampling.minimum_triangle_count,
                     "Minimum requested triangles for a closed sampled surface")
        ->check(CLI::PositiveNumber);
    pack_command->add_flag("--no-adaptive-sampling", disable_adaptive_sampling,
                           "Use the full-resolution meshes at every scale barrier");
    pack_command->add_option("--seed", pack_options.initialization.seed, "Deterministic random seed");
    pack_command
        ->add_option("--solver-openmp-threads", pack_options.algorithm.solver_openmp_threads,
                     "Solver OpenMP task threads (0 inherits; MKL environment overrides may take precedence)")
        ->check(CLI::Range(0, static_cast<int>(irop::maximum_local_solve_openmp_threads)));
    pack_command
        ->add_option("--max-sampling-attempts", pack_options.initialization.max_sampling_attempts,
                     "Maximum initialization candidate samples")
        ->check(CLI::PositiveNumber);
    pack_command->add_flag("!--no-initialization-fallback", pack_options.initialization.enable_structured_fallback,
                           "Disable structured placement after random sampling exhausts its attempt limit");
    pack_command
        ->add_option("--max-structured-candidates", pack_options.initialization.max_structured_candidates,
                     "Maximum candidate placements across all structured fallback orientations")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-geometry-query-triangle-visits",
                     pack_options.initialization.max_geometry_query_triangle_visits,
                     "Maximum container-triangle visits across initialization queries")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-pairwise-distance-checks", pack_options.initialization.max_pairwise_distance_checks,
                     "Maximum center-distance or structured-envelope separation checks across initialization")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-surface-intersection-triangle-pairs",
                     pack_options.initialization.max_surface_intersection_triangle_pairs,
                     "Maximum initialization surface-intersection triangle-pair tests")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-collision-object-pair-checks", pack_options.limits.collision.max_object_pair_checks,
                     "Maximum object-pair checks across packing and output validation")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-collision-triangle-pair-tests", pack_options.limits.collision.max_triangle_pair_tests,
                     "Maximum surface triangle-pair tests across packing and output validation")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-correction-passes", pack_options.limits.max_correction_passes_per_iteration,
                     "Maximum collision scale reductions per packing iteration")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-history-records", pack_options.limits.max_history_records,
                     "Maximum retained packing iteration records")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-total-local-solves", pack_options.limits.max_total_local_solves,
                     "Maximum local solves across the run")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-local-solve-iterations", pack_options.limits.local_solve.max_iterations,
                     "Maximum Ipopt iterations for each object solve")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-local-solve-milliseconds", maximum_local_solve_milliseconds,
                     "Maximum elapsed time checked inside each local solve")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-elapsed-milliseconds", maximum_packing_milliseconds,
                     "Maximum wall-clock duration checked between dependency calls")
        ->check(CLI::PositiveNumber);
    pack_command->add_flag("--individual-stls,--write-individual-stls", pack_options.write_individual_objects,
                           "Also write one STL per successfully packed object");
    pack_command
        ->add_option("--max-input-bytes", pack_options.input_limits.max_input_bytes,
                     "Maximum accepted size of each input STL")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-input-vertices", pack_options.input_limits.max_vertices,
                     "Maximum accepted vertex count per input mesh")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-input-triangles", pack_options.input_limits.max_triangles,
                     "Maximum accepted triangle count per input mesh")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-output-vertices", pack_options.initialization.output_mesh_limits.max_vertices,
                     "Maximum combined packed-object vertex count")
        ->check(CLI::PositiveNumber);
    pack_command
        ->add_option("--max-output-triangles", pack_options.initialization.output_mesh_limits.max_triangles,
                     "Maximum combined packed-object triangle count")
        ->check(CLI::PositiveNumber);

    pack_command->add_flag("!--no-physical-retry-reuse", pack_options.algorithm.reuse_physical_retry_results,
                           "Recompute all local results after physical retries for reproducibility");
    pack_command->add_flag("--reference-growth-policy", pack_options.algorithm.use_reference_growth_policy,
                           "Use the historical random-start and sampling policy for reproducibility");
    pack_command->add_flag("--capture-failed-local-solve",
                           pack_options.algorithm.diagnostics.capture_failed_local_problem,
                           "Save one bounded failed local problem for replay (no packed geometry)");
    pack_command
        ->add_option("--local-solve-trace-records", pack_options.algorithm.diagnostics.max_trace_records_per_solve,
                     "Maximum diagnostic trace rows per retained solve; zero disables")
        ->check(CLI::Range(0, 4096));
    pack_command
        ->add_option("--local-solve-records", pack_options.algorithm.diagnostics.max_local_solve_records,
                     "Maximum retained per-object solve records; zero disables")
        ->check(CLI::Range(0, 4096));

    std::filesystem::path replay_path;
    std::uint64_t replay_trace_records = 128;
    std::uint64_t replay_max_iterations = 1000;
    std::uint64_t replay_max_milliseconds = 30000;
    CLI::App* replay_command =
        application.add_subcommand("replay-local-solve", "Replay a bounded saved local optimization problem");
    replay_command->add_option("snapshot", replay_path, "Saved failed-local-solve.json")->required();
    replay_command->add_option("--trace-records", replay_trace_records, "Maximum printed diagnostic rows")
        ->check(CLI::Range(0, 4096));
    replay_command
        ->add_option("--max-iterations", replay_max_iterations, "Maximum accepted saved solver iteration budget")
        ->check(CLI::Range(1, 1000000));
    replay_command->add_option("--max-milliseconds", replay_max_milliseconds, "Maximum accepted saved elapsed budget")
        ->check(CLI::Range(1, 300000));

    try {
        application.parse(argc, argv);
    }
    catch (const CLI::ParseError& error) {
        const int cli_exit = application.exit(error);
        return cli_exit == static_cast<int>(CLI::ExitCodes::Success) ? static_cast<int>(ExitCode::success)
                                                                     : static_cast<int>(ExitCode::usage);
    }

    if (*replay_command) {
        irop::LocalSolveSnapshotReadLimits read_limits;
        read_limits.solve_limits.max_iterations = replay_max_iterations;
        read_limits.solve_limits.max_elapsed_time =
            std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(replay_max_milliseconds));
        const auto snapshot = irop::read_local_solve_snapshot(replay_path, read_limits);
        irop::LocalSolveWorkspace workspace;
        const auto result =
            irop::solve_prepared_local_transform(snapshot.constraints, snapshot.request, workspace, snapshot.limits,
                                                 { .max_trace_records = replay_trace_records });
        spdlog::info("local replay status={} constraints={} iterations={} elapsed_ms={}: {}",
                     irop::to_string(result.status), result.work.constraints_prepared, result.work.iterations,
                     result.work.elapsed_time.count(), result.diagnostic);
        for (const auto& row : result.trace) {
            spdlog::info("iteration={} restoration={} objective={} primal={} dual={} mu={}", row.iteration,
                         row.restoration_phase, row.objective, row.primal_infeasibility, row.dual_infeasibility,
                         row.barrier_parameter);
        }
        spdlog::info("trace_records_dropped={}", result.trace_records_dropped);
        return static_cast<int>(result.succeeded() ? ExitCode::success : ExitCode::unsuccessful);
    }
    if (*inspect_command) {
        const irop::InspectionResult result =
            irop::inspect_stl(input_path, inspection_output_directory, inspection_limits);
        spdlog::info("validated {} vertices and {} triangles; wrote {}", result.mesh.vertex_count,
                     result.mesh.triangle_count, path_as_utf8(result.normalized_stl_path));
    }
    else if (*initialize_command) {
        const irop::InitializationResult result = irop::initialize_scene(
            object_path, container_path, initialization_output_directory, initialization_options);
        spdlog::info("initialized {} objects with seed {}; wrote {}", result.state.transforms.size(),
                     result.state.random_state.seed(), path_as_utf8(result.initialized_objects_path));
    }
    else if (*pack_command) {
        pack_options.algorithm.adaptive_sampling = !disable_adaptive_sampling;
        if (maximum_packing_milliseconds > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count())) {
            throw irop::Error(irop::ErrorCategory::invalid_configuration,
                              "maximum packing duration is not representable");
        }
        pack_options.limits.max_elapsed_time =
            std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(maximum_packing_milliseconds));
        if (maximum_local_solve_milliseconds > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count())) {
            throw irop::Error(irop::ErrorCategory::invalid_configuration,
                              "maximum local-solve duration is not representable");
        }
        pack_options.limits.local_solve.max_elapsed_time =
            std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(maximum_local_solve_milliseconds));
        interruption_requested = 0;
        static_cast<void>(std::signal(SIGINT, handle_interruption));
#if defined(_WIN32)
        static_cast<void>(std::signal(SIGBREAK, handle_interruption));
#endif
        pack_options.callbacks.cancellation_requested = []() noexcept {
            return interruption_requested != 0;
        };
        pack_options.callbacks.progress = [](const irop::PackingProgress& progress) {
            switch (progress.phase) {
            case irop::PackingProgressPhase::input_preparation:
                spdlog::info("preparing object and container meshes");
                break;
            case irop::PackingProgressPhase::initialization_started:
                spdlog::info("initializing {} objects; random attempt limit {}", progress.object_count,
                             progress.initialization_attempt_limit.value_or(0));
                break;
            case irop::PackingProgressPhase::scale_step_started:
                spdlog::info("packing scale step {}/{}: target volume scale {}", progress.scale_step + 1,
                             progress.scale_step_count, progress.target_volume_scale);
                break;
            case irop::PackingProgressPhase::sampling_recovery:
                spdlog::info("refining object mesh after physical collision correction");
                break;
            case irop::PackingProgressPhase::tetrahedralization_recovery:
                spdlog::warn("TetGen recovery at scale step {}, iteration {}", progress.scale_step + 1,
                             progress.iteration + 1);
                break;
            case irop::PackingProgressPhase::step_recovery:
                spdlog::info("retrying smaller growth and movement steps after physical collision");
                break;
            case irop::PackingProgressPhase::local_solve_started:
                spdlog::debug("solving object {} at scale step {}, local iteration cap {}",
                              progress.object_id.value_or(0), progress.scale_step + 1, progress.local_iteration_limit);
                break;
            case irop::PackingProgressPhase::iteration_completed:
                spdlog::debug("packing step {}, iteration {}: {}/{} objects at target", progress.scale_step + 1,
                              progress.iteration + 1, progress.objects_at_target, progress.object_count);
                break;
            case irop::PackingProgressPhase::finished:
                break;
            }
        };

        const irop::PackSceneResult result =
            irop::pack_scene(packing_object_path, packing_container_path, packing_output_directory, pack_options);
        if (result.packing.succeeded()) {
            spdlog::info("packed {} objects; wrote {}", result.packing.state.transforms.size(),
                         path_as_utf8(*result.packed_objects_path));
        }
        else {
            spdlog::error("packing ended with {}: {}; wrote {}", irop::to_string(result.packing.status),
                          result.packing.diagnostic, path_as_utf8(result.run_summary_path));
        }
        return static_cast<int>(exit_code_for(result.packing.status));
    }
    return static_cast<int>(ExitCode::success);
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        return run_application(argc, argv);
    }
    catch (const irop::Error& error) {
        log_error_noexcept(irop::to_string(error.category()), error.what());
        return static_cast<int>(exit_code_for(error.category()));
    }
    catch (const std::bad_alloc&) {
        log_error_noexcept("resource_limit", "memory allocation failed");
        return static_cast<int>(ExitCode::resource_limit);
    }
    catch (const std::exception& error) {
        log_error_noexcept("internal", error.what());
        return static_cast<int>(ExitCode::internal);
    }
    catch (...) {
        log_error_noexcept("internal", "unknown failure");
        return static_cast<int>(ExitCode::internal);
    }
}
