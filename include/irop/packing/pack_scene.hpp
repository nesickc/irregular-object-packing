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

struct PackSceneResult {
    PackingResult packing;
    std::filesystem::path resolved_object_path;
    std::filesystem::path resolved_container_path;
    std::optional<std::filesystem::path> packed_objects_path;
    std::optional<std::filesystem::path> container_output_path;
    std::optional<std::filesystem::path> placements_path;
    std::filesystem::path run_summary_path;
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
