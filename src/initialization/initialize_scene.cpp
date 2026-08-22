#include "irop/initialization/initialize_scene.hpp"

#include <vtkVersion.h>

#include <Eigen/Core>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/packing/initialization.hpp"

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

[[nodiscard]] std::filesystem::path resolve_input_path(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::canonical(path, error);
    if (error) {
        throw Error(ErrorCategory::input_io, "input STL does not exist or cannot be resolved");
    }
    return resolved;
}

class OutputDirectoryTransaction final {
public:
    explicit OutputDirectoryTransaction(const std::filesystem::path& requested_output)
    {
        if (requested_output.empty()) {
            throw Error(ErrorCategory::output_io, "output directory must not be empty");
        }
        std::error_code error;
        final_directory_ = std::filesystem::absolute(requested_output, error).lexically_normal();
        if (error || final_directory_.filename().empty()) {
            throw Error(ErrorCategory::output_io, "failed to resolve a named output directory");
        }
        std::filesystem::path parent = final_directory_.parent_path();
        if (parent.empty()) {
            throw Error(ErrorCategory::output_io, "output directory must have a resolvable parent");
        }
        if (!std::filesystem::exists(parent, error)) {
            error.clear();
            static_cast<void>(std::filesystem::create_directories(parent, error));
        }
        parent = std::filesystem::canonical(parent, error);
        if (error || !std::filesystem::is_directory(parent, error)) {
            throw Error(ErrorCategory::output_io, "failed to prepare the output parent directory");
        }
        final_directory_ = parent / final_directory_.filename();
        const std::filesystem::file_status final_status = std::filesystem::symlink_status(final_directory_, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw Error(ErrorCategory::output_io, "failed to inspect the initialization output path");
        }
        if (!error && final_status.type() != std::filesystem::file_type::not_found) {
            throw Error(ErrorCategory::output_io,
                        "initialization output directory already exists; choose a new artifact-set path");
        }

        std::random_device random;
        for (std::size_t attempt = 0; attempt < 64; ++attempt) {
            std::ostringstream name;
            name << ".irop-initialize-staging-" << std::hex << std::setfill('0') << std::setw(8) << random()
                 << std::setw(8) << random() << std::setw(8) << random() << std::setw(8) << random();
            staging_directory_ = parent / name.str();
            error.clear();
            if (std::filesystem::create_directory(staging_directory_, error)) {
                return;
            }
            if (error) {
                throw Error(ErrorCategory::output_io, "failed to create a private initialization staging directory");
            }
        }
        throw Error(ErrorCategory::output_io, "failed to reserve a unique initialization staging directory");
    }

    OutputDirectoryTransaction(const OutputDirectoryTransaction&) = delete;
    OutputDirectoryTransaction& operator=(const OutputDirectoryTransaction&) = delete;

    ~OutputDirectoryTransaction() noexcept
    {
        if (committed_ || staging_directory_.empty()) {
            return;
        }
        // The constructor created this exact private sibling path and the class
        // exposes no mutator for it. Keep cleanup no-throw during unwinding.
        try {
            std::error_code ignored;
            static_cast<void>(std::filesystem::remove_all(staging_directory_, ignored));
        }
        catch (...) {
            // Destructors cannot replace the original publication failure.
        }
    }

    [[nodiscard]] const std::filesystem::path& final_directory() const noexcept { return final_directory_; }
    [[nodiscard]] const std::filesystem::path& staging_directory() const noexcept { return staging_directory_; }

    void commit()
    {
        std::error_code error;
        std::filesystem::rename(staging_directory_, final_directory_, error);
        if (error) {
            throw Error(ErrorCategory::output_io, "failed to atomically publish the initialization artifact set");
        }
        committed_ = true;
    }

private:
    std::filesystem::path final_directory_;
    std::filesystem::path staging_directory_;
    bool committed_ = false;
};

void remove_file_noexcept(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(path, ignored));
}

[[nodiscard]] nlohmann::json point_json(const Point3& point)
{
    return nlohmann::json::array({ point.x, point.y, point.z });
}

[[nodiscard]] nlohmann::json bounds_json(const MeshBounds& bounds)
{
    return {
        { "minimum", point_json(bounds.minimum) },
        { "maximum", point_json(bounds.maximum) },
    };
}

[[nodiscard]] nlohmann::json matrix_json(const Matrix4& matrix)
{
    nlohmann::json rows = nlohmann::json::array();
    for (std::size_t row = 0; row < 4; ++row) {
        nlohmann::json values = nlohmann::json::array();
        for (std::size_t column = 0; column < 4; ++column) {
            values.push_back(matrix(row, column));
        }
        rows.push_back(std::move(values));
    }
    return rows;
}

[[nodiscard]] std::filesystem::path individual_object_path(const std::filesystem::path& directory,
                                                           const std::size_t index)
{
    std::ostringstream filename;
    filename << "object-" << std::setfill('0') << std::setw(6) << index << ".stl";
    return directory / filename.str();
}

[[nodiscard]] nlohmann::json placements_json(const InitializationResult& result)
{
    nlohmann::json placements = nlohmann::json::array();
    for (std::size_t index = 0; index < result.state.transforms.size(); ++index) {
        const Transform& transform = result.state.transforms[index];
        placements.push_back({
            { "object_id", index },
            { "object_identity", "object-template" },
            { "volume_scale", transform.volume_scale },
            { "rotation_radians",
             nlohmann::json::array({ transform.rotation.x, transform.rotation.y, transform.rotation.z }) },
            { "translation", point_json(transform.translation) },
            { "input_to_world_matrix",
             matrix_json(compose(
                  matrix_for(transform),
             matrix_for({ .translation = { -result.source_object_centroid.x, -result.source_object_centroid.y,
                                                -result.source_object_centroid.z } }))) },
        });
    }

    return {
        { "schema_version",       1                      },
        { "command",              "initialize"           },
        { "phase",                "initialization"       },
        { "coordinate_units",     "arbitrary_consistent" },
        { "transform_convention",
         {
              { "vector_convention", "column" },
              { "rotation_units", "radians" },
              { "rotation_order", "Ry*Rz*Rx" },
              { "scale_kind", "volume" },
          }                                              },
        { "object_template",
         {
              { "identity", "object-template" },
              { "source_vertex_centroid", point_json(result.source_object_centroid) },
              { "centered_at_origin", true },
          }                                              },
        { "placements",           std::move(placements)  },
    };
}

[[nodiscard]] std::string eigen_version()
{
    return std::to_string(EIGEN_WORLD_VERSION) + "." + std::to_string(EIGEN_MAJOR_VERSION) + "." +
           std::to_string(EIGEN_MINOR_VERSION);
}

[[nodiscard]] nlohmann::json run_summary_json(const InitializationResult& result, const LoadedStl& object,
                                              const LoadedStl& container, const InitializationOptions& options,
                                              const double initialization_seconds,
                                              const double artifact_preparation_seconds)
{
    nlohmann::json individual_paths = nlohmann::json::array();
    for (const std::filesystem::path& path : result.individual_object_paths) {
        individual_paths.push_back(path_as_utf8(path));
    }
    nlohmann::json warnings = nlohmann::json::array();
    for (const std::string& warning : object.warnings) {
        warnings.push_back("object: " + warning);
    }
    for (const std::string& warning : container.warnings) {
        warnings.push_back("container: " + warning);
    }

    return {
        { "schema_version", 1                             },
        { "command",        "initialize"                  },
        { "outcome",        { { "category", "success" } } },
        { "inputs",
         {
              { "object",
                {
                    { "resolved_path", path_as_utf8(result.resolved_object_path) },
                    { "stl_encoding", to_string(object.encoding) },
                    { "size_bytes", object.input_bytes },
                    { "vertex_count", result.centered_object_statistics.vertex_count },
                    { "triangle_count", result.centered_object_statistics.triangle_count },
                    { "source_vertex_centroid", point_json(result.source_object_centroid) },
                    { "volume", result.state.object_volume },
                    { "maximum_radius", result.state.object_bounding_radius / result.state.initial_linear_scale },
                } },
              { "container",
                {
                    { "resolved_path", path_as_utf8(result.resolved_container_path) },
                    { "stl_encoding", to_string(container.encoding) },
                    { "size_bytes", container.input_bytes },
                    { "vertex_count", result.container_statistics.vertex_count },
                    { "triangle_count", result.container_statistics.triangle_count },
                    { "volume", result.state.container_volume },
                    { "bounds", bounds_json(result.container_statistics.bounds) },
                } },
          }                                               },
        { "config",
         {
              { "object_count", result.state.config.object_count },
              { "initial_volume_scale", result.state.config.initial_volume_scale },
              { "seed", result.state.config.seed },
              { "max_sampling_attempts", result.state.config.max_sampling_attempts },
              { "max_geometry_query_triangle_visits", result.state.config.max_geometry_query_triangle_visits },
              { "max_pairwise_distance_checks", result.state.config.max_pairwise_distance_checks },
              { "max_surface_intersection_triangle_pairs",
                result.state.config.max_surface_intersection_triangle_pairs },
              { "input_limits",
                {
                    { "max_input_bytes", options.input_limits.max_input_bytes },
                    { "max_vertices", options.input_limits.max_vertices },
                    { "max_triangles", options.input_limits.max_triangles },
                } },
              { "output_limits",
                {
                    { "max_vertices", result.state.config.output_mesh_limits.max_vertices },
                    { "max_triangles", result.state.config.output_mesh_limits.max_triangles },
                } },
          }                                               },
        { "sampling",
         {
              { "policy", "uniform-aabb-rejection-bounding-sphere" },
              { "initial_linear_scale", result.state.initial_linear_scale },
              { "minimum_boundary_clearance", result.state.object_bounding_radius },
              { "minimum_center_distance", result.state.minimum_center_distance },
              { "rejected_candidate_count", result.state.rejected_candidate_count },
              { "random_draw_count", result.state.random_state.draw_count() },
              { "random_generator", "numpy-legacy-mt19937-compatible" },
              { "geometry_query_triangle_visits", result.state.geometry_query_triangle_visits },
              { "pairwise_distance_checks", result.state.pairwise_distance_checks },
              { "surface_intersection_triangle_pairs", result.state.surface_intersection_triangle_pairs },
          }                                               },
        { "timings",
         {
              { "initialization_seconds", initialization_seconds },
              { "artifact_preparation_seconds", artifact_preparation_seconds },
          }                                               },
        { "outputs",
         {
              { "initialized_objects_stl", path_as_utf8(result.initialized_objects_path) },
              { "container_stl", path_as_utf8(result.container_output_path) },
              { "placements_json", path_as_utf8(result.placements_path) },
              { "run_summary_json", path_as_utf8(result.run_summary_path) },
              { "individual_object_stls", std::move(individual_paths) },
          }                                               },
        { "versions",
         {
              { "irop", IROP_VERSION },
              { "vtk", vtkVersion::GetVTKVersion() },
              { "eigen", eigen_version() },
          }                                               },
        { "warnings",       std::move(warnings)           },
    };
}

void write_new_text_file(const std::filesystem::path& path, const std::string& contents)
{
#ifdef _WIN32
    HANDLE output =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw Error(ErrorCategory::output_io, "failed to reserve a new structured JSON artifact path");
    }
    bool succeeded = true;
    std::size_t written = 0;
    while (written < contents.size()) {
        const DWORD requested =
            static_cast<DWORD>(std::min<std::size_t>(contents.size() - written, static_cast<std::size_t>(MAXDWORD)));
        DWORD chunk_written = 0;
        if (WriteFile(output, contents.data() + written, requested, &chunk_written, nullptr) == 0 ||
            chunk_written != requested) {
            succeeded = false;
            break;
        }
        written += chunk_written;
    }
    if (succeeded && FlushFileBuffers(output) == 0) {
        succeeded = false;
    }
    if (CloseHandle(output) == 0) {
        succeeded = false;
    }
    if (!succeeded) {
        remove_file_noexcept(path);
        throw Error(ErrorCategory::output_io, "failed while writing a structured JSON artifact");
    }
#else
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw Error(ErrorCategory::output_io, "failed to open a structured JSON artifact for writing");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        remove_file_noexcept(path);
        throw Error(ErrorCategory::output_io, "failed while writing a structured JSON artifact");
    }
#endif
}

}  // namespace

InitializationResult initialize_scene(const std::filesystem::path& object_path,
                                      const std::filesystem::path& container_path,
                                      const std::filesystem::path& output_directory,
                                      const InitializationOptions& options)
{
    const auto run_started = std::chrono::steady_clock::now();
    const std::filesystem::path resolved_object = resolve_input_path(object_path);
    const std::filesystem::path resolved_container = resolve_input_path(container_path);
    LoadedStl object = read_stl(resolved_object, options.input_limits);
    LoadedStl container = read_stl(resolved_container, options.input_limits);
    CenteredMesh centered_object = center_mesh_at_vertex_centroid(object.mesh);
    const MeshStatistics centered_statistics = validate_and_measure_mesh(centered_object.mesh, options.input_limits);
    const MeshStatistics container_statistics = validate_and_measure_mesh(container.mesh, options.input_limits);
    PackingState state = initialize_packing(centered_object.mesh, container.mesh, options.packing);
    std::vector<TriangleMesh> objects = instantiate_objects(centered_object.mesh, state);
    TriangleMesh combined = combine_meshes(objects, options.packing.output_mesh_limits);
    const double initialization_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - run_started).count();

    OutputDirectoryTransaction output_transaction(output_directory);
    const std::filesystem::path& output = output_transaction.final_directory();
    const std::filesystem::path& staging = output_transaction.staging_directory();
    const std::filesystem::path objects_directory = output / "objects";
    const std::filesystem::path staging_objects_directory = staging / "objects";
    InitializationResult result {
        .state = std::move(state),
        .resolved_object_path = resolved_object,
        .resolved_container_path = resolved_container,
        .initialized_objects_path = output / "initialized-objects.stl",
        .container_output_path = output / "container.stl",
        .placements_path = output / "placements.json",
        .run_summary_path = output / "run-summary.json",
        .individual_object_paths = {},
        .centered_object_statistics = centered_statistics,
        .container_statistics = container_statistics,
        .source_object_centroid = centered_object.original_vertex_centroid,
    };
    if (options.write_individual_objects) {
        result.individual_object_paths.reserve(objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index) {
            result.individual_object_paths.push_back(individual_object_path(objects_directory, index));
        }
    }

    std::vector<std::filesystem::path> staging_individual_paths;
    staging_individual_paths.reserve(result.individual_object_paths.size());
    for (std::size_t index = 0; index < result.individual_object_paths.size(); ++index) {
        staging_individual_paths.push_back(individual_object_path(staging_objects_directory, index));
    }
    if (options.write_individual_objects) {
        std::error_code directory_error;
        if (!std::filesystem::create_directory(staging_objects_directory, directory_error) || directory_error) {
            throw Error(ErrorCategory::output_io, "failed to create the staged individual-object directory");
        }
    }

    const auto publication_started = std::chrono::steady_clock::now();
    write_stl(staging / "initialized-objects.stl", combined);
    write_stl(staging / "container.stl", container.mesh);
    for (std::size_t index = 0; index < staging_individual_paths.size(); ++index) {
        write_stl(staging_individual_paths[index], objects[index]);
    }
    write_new_text_file(staging / "placements.json", placements_json(result).dump(2) + '\n');
    const double artifact_preparation_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - publication_started).count();
    write_new_text_file(
        staging / "run-summary.json",
        run_summary_json(result, object, container, options, initialization_seconds, artifact_preparation_seconds)
                .dump(2) +
            '\n');
    output_transaction.commit();
    return result;
}

}  // namespace irop
