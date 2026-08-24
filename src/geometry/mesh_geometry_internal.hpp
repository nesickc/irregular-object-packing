#pragma once

#include <cstdint>

#include "irop/geometry/mesh_geometry.hpp"
#include "irop/model/triangle_mesh.hpp"

namespace irop::detail {

// The caller must have structurally validated both meshes and must keep them
// immutable for the duration of the query. Public callers should use
// query_surface_intersection(), which establishes that invariant itself.
[[nodiscard]] SurfaceIntersectionResult query_validated_surface_intersection(const TriangleMesh& first,
                                                                             const TriangleMesh& second,
                                                                             std::uint64_t maximum_triangle_pair_tests);

}  // namespace irop::detail
