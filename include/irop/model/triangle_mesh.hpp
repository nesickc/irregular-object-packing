#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace irop {

using MeshIndex = std::uint64_t;

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

using Triangle = std::array<MeshIndex, 3>;

struct TriangleMesh {
    std::vector<Point3> vertices;
    std::vector<Triangle> triangles;
};

struct MeshLimits {
    static constexpr std::uint64_t default_max_input_bytes = 256ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t default_max_vertices = 15'000'000ULL;
    static constexpr std::uint64_t default_max_triangles = 5'000'000ULL;

    std::uint64_t max_input_bytes = default_max_input_bytes;
    std::uint64_t max_vertices = default_max_vertices;
    std::uint64_t max_triangles = default_max_triangles;
};

struct MeshBounds {
    Point3 minimum;
    Point3 maximum;
};

struct MeshStatistics {
    std::uint64_t vertex_count = 0;
    std::uint64_t triangle_count = 0;
    MeshBounds bounds;
};

}  // namespace irop
