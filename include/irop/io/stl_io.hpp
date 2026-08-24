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

// Returns the exact project-owned vertex representation used by the binary STL
// writer. Binary STL stores coordinates as IEEE-754 single precision, so this
// snapshot is suitable for validating geometry after output quantization and
// before publishing a success result.
[[nodiscard]] TriangleMesh quantize_mesh_for_binary_stl(const TriangleMesh& mesh);

void write_stl(const std::filesystem::path& output_path, const TriangleMesh& mesh);

}  // namespace irop
