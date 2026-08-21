#pragma once

#include "irop/model/triangle_mesh.hpp"

namespace irop {

[[nodiscard]] MeshStatistics validate_and_measure_mesh(const TriangleMesh& mesh, const MeshLimits& limits);

}  // namespace irop
