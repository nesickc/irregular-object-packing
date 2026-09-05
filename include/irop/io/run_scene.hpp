#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct RunSceneLimits {
    std::uint64_t max_summary_bytes = 16ULL * 1024ULL * 1024ULL;
    std::uint64_t max_json_depth = 32;
    std::uint64_t max_json_nodes = 250'000;
    std::uint64_t max_json_container_entries = 100'000;
    std::uint64_t max_string_bytes = 16'384;
    std::uint64_t max_object_count = 100'000;
    // The byte, vertex and triangle allowances are shared by both display meshes.
    MeshLimits mesh_limits { .max_input_bytes = 128ULL * 1024ULL * 1024ULL,
                             .max_vertices = 3'000'000,
                             .max_triangles = 1'000'000 };
};

struct LoadedRunScene {
    std::string command;
    std::string status;
    std::string diagnostic;
    std::uint64_t object_count = 0;
    std::uint32_t seed = 0;
    double initial_volume_scale = 0.0;
    double target_volume_scale = 0.0;
    std::optional<double> packing_fraction;
    std::optional<double> minimum_volume_scale;
    std::optional<double> maximum_volume_scale;
    std::optional<double> mean_volume_scale;
    bool success = false;
    // This is the validation recorded in the summary, not a fresh physical check.
    // Initialization summaries do not contain this record.
    std::optional<bool> recorded_physical_validity;
    std::filesystem::path summary_path;
    std::optional<TriangleMesh> objects;
    std::optional<TriangleMesh> container;
    std::vector<std::string> warnings;
};

// Opens the display-relevant contract of version-one pack/initialize summaries.
// Only fixed artifacts beside the summary are read. Original source paths and
// optional individual STL paths are never followed. Geometry is structurally
// validated for display; this does not recertify the recorded physical outcome.
[[nodiscard]] LoadedRunScene load_run_scene(const std::filesystem::path& summary_path,
                                            const RunSceneLimits& limits = {},
                                            const std::function<bool()>& cancellation_requested = {});

}  // namespace irop
