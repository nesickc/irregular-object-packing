#pragma once

#include <cstdint>
#include <vector>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct CenteredMesh {
    TriangleMesh mesh;
    Point3 original_vertex_centroid;
};

[[nodiscard]] Point3 vertex_centroid(const TriangleMesh& mesh);
[[nodiscard]] CenteredMesh center_mesh_at_vertex_centroid(const TriangleMesh& mesh);
[[nodiscard]] TriangleMesh scale_mesh_to_volume(const TriangleMesh& mesh, double target_volume);
[[nodiscard]] double maximum_radius(const TriangleMesh& mesh, const Point3& center);

class ClosedMeshQuery final {
public:
    explicit ClosedMeshQuery(const TriangleMesh& mesh);

    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] const MeshBounds& bounds() const noexcept;
    [[nodiscard]] bool contains(const Point3& point) const;
    [[nodiscard]] double distance_to_surface(const Point3& point) const;

private:
    // Own an immutable snapshot so queries cannot dangle or observe topology
    // mutations after validation.
    TriangleMesh mesh_;
    double volume_;
    MeshBounds bounds_;
};

struct SurfaceIntersectionResult {
    bool intersects = false;
    std::uint64_t tested_triangle_pairs = 0;
};

[[nodiscard]] SurfaceIntersectionResult query_surface_intersection(const TriangleMesh& first,
                                                                   const TriangleMesh& second,
                                                                   std::uint64_t maximum_triangle_pair_tests);

[[nodiscard]] TriangleMesh combine_meshes(const std::vector<TriangleMesh>& meshes, const MeshLimits& limits = {});

}  // namespace irop
