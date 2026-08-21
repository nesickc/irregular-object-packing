#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "irop/io/stl_io.hpp"
#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct InspectionResult {
    std::filesystem::path resolved_input_path;
    std::filesystem::path normalized_stl_path;
    std::filesystem::path summary_path;
    StlEncoding input_encoding = StlEncoding::binary;
    std::uint64_t input_bytes = 0;
    MeshStatistics mesh;
    MeshLimits limits;
    std::vector<std::string> warnings;
};

[[nodiscard]] InspectionResult inspect_stl(const std::filesystem::path& input_path,
                                           const std::filesystem::path& output_directory,
                                           const MeshLimits& limits = {});

}  // namespace irop
