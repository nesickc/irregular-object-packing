#pragma once

#include <filesystem>
#include <vector>

#include "irop/model/triangle_mesh.hpp"
#include "irop/packing/initialization.hpp"

namespace irop {

struct InitializationOptions {
    PackingConfig packing;
    MeshLimits input_limits;
    bool write_individual_objects = false;
};

struct InitializationResult {
    PackingState state;
    std::filesystem::path resolved_object_path;
    std::filesystem::path resolved_container_path;
    std::filesystem::path initialized_objects_path;
    std::filesystem::path container_output_path;
    std::filesystem::path placements_path;
    std::filesystem::path run_summary_path;
    std::vector<std::filesystem::path> individual_object_paths;
    MeshStatistics centered_object_statistics;
    MeshStatistics container_statistics;
    Point3 source_object_centroid;
};

[[nodiscard]] InitializationResult initialize_scene(const std::filesystem::path& object_path,
                                                    const std::filesystem::path& container_path,
                                                    const std::filesystem::path& output_directory,
                                                    const InitializationOptions& options);

}  // namespace irop
