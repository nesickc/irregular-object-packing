#pragma once

#include <filesystem>
#include <functional>

#include "irop/model/triangle_mesh.hpp"

namespace irop::detail {

// Runs completion only after the STL has been written and validated. On the supported Windows platform, the output
// remains locked against modification or replacement through completion so a success record cannot describe a
// different mesh. If completion fails, the STL is removed.
void write_stl_transactional(const std::filesystem::path& output_path, const TriangleMesh& mesh,
                             const std::function<void()>& completion);

}  // namespace irop::detail
