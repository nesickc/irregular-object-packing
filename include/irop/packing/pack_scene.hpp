#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "irop/model/triangle_mesh.hpp"
#include "irop/packing/packing.hpp"

namespace irop {

struct PackOptions {
    PackingConfig initialization;
    PackingAlgorithmConfig algorithm;
    PackingEngineLimits limits;
    MeshLimits input_limits;
    bool write_individual_objects = false;
    PackingCallbacks callbacks;
};

struct PackSceneTimings {
    double preparation_seconds = 0.0;
    double initialization_seconds = 0.0;
    double packing_seconds = 0.0;
    double output_validation_seconds = 0.0;
    // Mesh/artifact preparation excluding output validation. Summary formatting
    // and final directory publication are included only in total_seconds.
    double export_seconds = 0.0;
    double total_seconds = 0.0;
};

struct PackSceneResult {
    PackingResult packing;
    PackSceneTimings timings;
    std::filesystem::path resolved_object_path;
    std::filesystem::path resolved_container_path;
    std::optional<std::filesystem::path> packed_objects_path;
    std::optional<std::filesystem::path> container_output_path;
    std::optional<std::filesystem::path> placements_path;
    std::filesystem::path run_summary_path;
    std::optional<std::filesystem::path> failed_local_solve_path;
    std::vector<std::filesystem::path> individual_object_paths;
    MeshStatistics centered_object_statistics;
    MeshStatistics container_statistics;
    Point3 source_object_centroid;
};

// Expected algorithm failures return a committed unsuccessful run summary.
// Input/configuration/output failures still throw the project-owned Error type.
[[nodiscard]] PackSceneResult pack_scene(const std::filesystem::path& object_path,
                                         const std::filesystem::path& container_path,
                                         const std::filesystem::path& output_directory, const PackOptions& options);

}  // namespace irop
