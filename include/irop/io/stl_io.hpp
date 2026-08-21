#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

enum class StlEncoding {
    ascii,
    binary,
};

[[nodiscard]] const char* to_string(StlEncoding encoding) noexcept;

struct LoadedStl {
    TriangleMesh mesh;
    StlEncoding encoding = StlEncoding::binary;
    std::uint64_t input_bytes = 0;
    std::vector<std::string> warnings;
};

[[nodiscard]] LoadedStl read_stl(const std::filesystem::path& input_path, const MeshLimits& limits);
void write_stl(const std::filesystem::path& output_path, const TriangleMesh& mesh);

}  // namespace irop
