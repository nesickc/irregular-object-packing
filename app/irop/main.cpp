#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <string>

#include "irop/error.hpp"
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
    std::filesystem::path output_directory;
    irop::MeshLimits limits;

    CLI::App* inspect_command = application.add_subcommand("inspect", "Validate and normalize an STL mesh");
    inspect_command->add_option("input", input_path, "Input STL path")->required();
    inspect_command
        ->add_option("-o,--output,--output-dir", output_directory, "New artifact paths under this output directory")
        ->required();
    inspect_command->add_option("--max-input-bytes", limits.max_input_bytes, "Maximum accepted input file size")
        ->check(CLI::PositiveNumber);
    inspect_command->add_option("--max-vertices", limits.max_vertices, "Maximum accepted vertex count")
        ->check(CLI::PositiveNumber);
    inspect_command->add_option("--max-triangles", limits.max_triangles, "Maximum accepted triangle count")
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
        const irop::InspectionResult result = irop::inspect_stl(input_path, output_directory, limits);
        spdlog::info("validated {} vertices and {} triangles; wrote {}", result.mesh.vertex_count,
                     result.mesh.triangle_count, path_as_utf8(result.normalized_stl_path));
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
