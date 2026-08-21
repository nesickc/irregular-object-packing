#include "irop/model/mesh_validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "irop/error.hpp"

namespace irop {
namespace {

[[nodiscard]] bool is_finite(const Point3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] Point3 subtract(const Point3& left, const Point3& right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Point3 cross(const Point3& left, const Point3& right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double squared_norm(const Point3& point)
{
    return point.x * point.x + point.y * point.y + point.z * point.z;
}

[[noreturn]] void throw_count_limit(const char* kind, const std::uint64_t actual, const std::uint64_t maximum)
{
    throw Error(ErrorCategory::resource_limit, std::string("mesh ") + kind + " count " + std::to_string(actual) +
                                                   " exceeds configured limit " + std::to_string(maximum));
}

}  // namespace

MeshStatistics validate_and_measure_mesh(const TriangleMesh& mesh, const MeshLimits& limits)
{
    const auto vertex_count = static_cast<std::uint64_t>(mesh.vertices.size());
    const auto triangle_count = static_cast<std::uint64_t>(mesh.triangles.size());

    if (vertex_count > limits.max_vertices) {
        throw_count_limit("vertex", vertex_count, limits.max_vertices);
    }
    if (triangle_count > limits.max_triangles) {
        throw_count_limit("triangle", triangle_count, limits.max_triangles);
    }
    if (mesh.vertices.empty()) {
        throw Error(ErrorCategory::invalid_mesh, "mesh contains no vertices");
    }
    if (mesh.triangles.empty()) {
        throw Error(ErrorCategory::invalid_mesh, "mesh contains no triangles");
    }

    MeshBounds bounds { mesh.vertices.front(), mesh.vertices.front() };
    for (const Point3& vertex : mesh.vertices) {
        if (!is_finite(vertex)) {
            throw Error(ErrorCategory::invalid_mesh, "mesh contains a non-finite vertex coordinate");
        }

        bounds.minimum.x = std::min(bounds.minimum.x, vertex.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.z);
    }

    for (const Triangle& triangle : mesh.triangles) {
        if (triangle[0] >= vertex_count || triangle[1] >= vertex_count || triangle[2] >= vertex_count) {
            throw Error(ErrorCategory::invalid_mesh, "mesh contains a triangle with an out-of-range vertex index");
        }
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2]) {
            throw Error(ErrorCategory::invalid_mesh, "mesh contains a triangle with repeated vertex indices");
        }

        const Point3& first = mesh.vertices[static_cast<std::size_t>(triangle[0])];
        const Point3& second = mesh.vertices[static_cast<std::size_t>(triangle[1])];
        const Point3& third = mesh.vertices[static_cast<std::size_t>(triangle[2])];
        const double area_factor_squared = squared_norm(cross(subtract(second, first), subtract(third, first)));
        if (!std::isfinite(area_factor_squared) || area_factor_squared <= 0.0) {
            throw Error(ErrorCategory::invalid_mesh, "mesh contains a degenerate or numerically invalid triangle");
        }
    }

    return {
        .vertex_count = vertex_count,
        .triangle_count = triangle_count,
        .bounds = bounds,
    };
}

}  // namespace irop
