#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <string>

#include "irop/error.hpp"
#include "irop/initialization/initialize_scene.hpp"
#include "irop/inspection/inspect_stl.hpp"
#include "irop/model/triangle_mesh.hpp"

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
    internal = 70,
};

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
    case irop::ErrorCategory::internal:
        return ExitCode::internal;
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
    initialize_command
        ->add_option("--max-geometry-query-triangle-visits",
                     initialization_options.packing.max_geometry_query_triangle_visits,
                     "Maximum container-triangle visits across initialization queries")
        ->check(CLI::PositiveNumber);
    initialize_command
        ->add_option("--max-pairwise-distance-checks", initialization_options.packing.max_pairwise_distance_checks,
                     "Maximum center-to-center distance checks across initialization")
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

    try {
        application.parse(argc, argv);
    }
    catch (const CLI::ParseError& error) {
        const int cli_exit = application.exit(error);
        return cli_exit == static_cast<int>(CLI::ExitCodes::Success) ? static_cast<int>(ExitCode::success)
                                                                     : static_cast<int>(ExitCode::usage);
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
