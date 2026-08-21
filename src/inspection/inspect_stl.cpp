#include "irop/inspection/inspect_stl.hpp"

#include <vtkVersion.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "io/stl_io_internal.hpp"
#include "irop/error.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"

#ifndef IROP_VERSION
#define IROP_VERSION "development"
#endif

namespace irop {
namespace {

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

[[nodiscard]] std::filesystem::path resolve_existing_path(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    if (error) {
        throw Error(ErrorCategory::input_io, "failed to resolve input STL path");
    }
    return resolved;
}

[[nodiscard]] std::filesystem::path prepare_output_directory(const std::filesystem::path& output_directory)
{
    if (output_directory.empty()) {
        throw Error(ErrorCategory::output_io, "output directory must not be empty");
    }

    std::error_code error;
    std::filesystem::path resolved = std::filesystem::absolute(output_directory, error).lexically_normal();
    if (error) {
        throw Error(ErrorCategory::output_io, "failed to resolve output directory");
    }

    const bool exists = std::filesystem::exists(resolved, error);
    if (error) {
        throw Error(ErrorCategory::output_io, "failed to inspect output directory");
    }
    if (exists && !std::filesystem::is_directory(resolved, error)) {
        throw Error(ErrorCategory::output_io, "output path exists but is not a directory");
    }
    if (!exists && !std::filesystem::create_directories(resolved, error)) {
        throw Error(ErrorCategory::output_io, "failed to create output directory");
    }
    if (error) {
        throw Error(ErrorCategory::output_io, "failed to prepare output directory");
    }
    resolved = std::filesystem::weakly_canonical(resolved, error);
    if (error) {
        throw Error(ErrorCategory::output_io, "failed to resolve the prepared output directory");
    }
    return resolved;
}

[[nodiscard]] nlohmann::json point_json(const Point3& point)
{
    return nlohmann::json::array({ point.x, point.y, point.z });
}

void require_unused_output_path(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw Error(ErrorCategory::output_io, "failed to inspect a planned output artifact path");
    }
    if (!error && status.type() != std::filesystem::file_type::not_found) {
        throw Error(ErrorCategory::output_io, "an output artifact already exists; choose a new output directory");
    }
}

void remove_generated_artifact(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(path, ignored));
}

void write_summary(const InspectionResult& result, const std::filesystem::path& write_path)
{
    const nlohmann::json summary {
        { "schema_version", 1                                                                    },
        { "command",        "inspect"                                                            },
        { "outcome",        { { "category", "success" } }                                        },
        { "input",
         {
              { "resolved_path", path_as_utf8(result.resolved_input_path) },
              { "stl_encoding", to_string(result.input_encoding) },
              { "size_bytes", result.input_bytes },
          }                                                                                      },
        { "output",
         {
              { "normalized_stl", path_as_utf8(result.normalized_stl_path) },
          }                                                                                      },
        { "limits",
         {
              { "max_input_bytes", result.limits.max_input_bytes },
              { "max_vertices", result.limits.max_vertices },
              { "max_triangles", result.limits.max_triangles },
          }                                                                                      },
        { "mesh",
         {
              { "vertex_count", result.mesh.vertex_count },
              { "triangle_count", result.mesh.triangle_count },
              { "bounds",
                {
                    { "minimum", point_json(result.mesh.bounds.minimum) },
                    { "maximum", point_json(result.mesh.bounds.maximum) },
                } },
          }                                                                                      },
        { "versions",       { { "irop", IROP_VERSION }, { "vtk", vtkVersion::GetVTKVersion() } } },
        { "warnings",       result.warnings                                                      },
    };

    const std::string contents = summary.dump(2) + '\n';

#ifdef _WIN32
    HANDLE output = CreateFileW(write_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw Error(ErrorCategory::output_io, "failed to reserve a new inspection summary path");
    }

    bool write_succeeded = true;
    std::size_t written = 0;
    while (written < contents.size()) {
        const std::size_t remaining = contents.size() - written;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        DWORD chunk_written = 0;
        if (WriteFile(output, contents.data() + written, requested, &chunk_written, nullptr) == 0 ||
            chunk_written != requested) {
            write_succeeded = false;
            break;
        }
        written += chunk_written;
    }
    if (write_succeeded && FlushFileBuffers(output) == 0) {
        write_succeeded = false;
    }
    if (CloseHandle(output) == 0) {
        write_succeeded = false;
    }
    if (!write_succeeded) {
        remove_generated_artifact(write_path);
        throw Error(ErrorCategory::output_io, "failed while writing inspection summary");
    }
#else
    std::ofstream output(write_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw Error(ErrorCategory::output_io, "failed to open inspection summary for writing");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
        throw Error(ErrorCategory::output_io, "failed while writing inspection summary");
    }
    output.close();
    if (!output) {
        throw Error(ErrorCategory::output_io, "failed while closing inspection summary");
    }
#endif
}

}  // namespace

InspectionResult inspect_stl(const std::filesystem::path& input_path, const std::filesystem::path& output_directory,
                             const MeshLimits& limits)
{
    const std::filesystem::path resolved_input = resolve_existing_path(input_path);
    LoadedStl loaded = read_stl(resolved_input, limits);
    const MeshStatistics statistics = validate_and_measure_mesh(loaded.mesh, limits);
    const std::filesystem::path resolved_output = prepare_output_directory(output_directory);

    InspectionResult result {
        .resolved_input_path = resolved_input,
        .normalized_stl_path = resolved_output / "normalized.stl",
        .summary_path = resolved_output / "inspection-summary.json",
        .input_encoding = loaded.encoding,
        .input_bytes = loaded.input_bytes,
        .mesh = statistics,
        .limits = limits,
        .warnings = std::move(loaded.warnings),
    };

    require_unused_output_path(result.normalized_stl_path);
    require_unused_output_path(result.summary_path);

    detail::write_stl_transactional(result.normalized_stl_path, loaded.mesh, [&]() {
        write_summary(result, result.summary_path);
    });
    return result;
}

}  // namespace irop
