#include "irop/geometry/mesh_geometry.hpp"

#include <vtkTriangle.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>

#include "irop/error.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/model/mesh_validation.hpp"
#include "mesh_geometry_internal.hpp"

namespace irop {
namespace {

struct EdgeKey {
    MeshIndex first;
    MeshIndex second;

    bool operator==(const EdgeKey&) const noexcept = default;
};

struct EdgeKeyHash {
    [[nodiscard]] std::size_t operator()(const EdgeKey& edge) const noexcept
    {
        const std::size_t first = std::hash<MeshIndex> {}(edge.first);
        const std::size_t second = std::hash<MeshIndex> {}(edge.second);
        return first ^ (second + 0x9E3779B97F4A7C15ULL + (first << 6U) + (first >> 2U));
    }
};

struct FaceEdgeUse {
    std::size_t face = 0;
    bool forward = false;
};

struct EdgeUse {
    std::uint64_t count = 0;
    std::array<FaceEdgeUse, 2> faces {};
};

class ParityDisjointSet final {
public:
    explicit ParityDisjointSet(const std::size_t size) : parent_(size), rank_(size, 0), parity_(size, 0)
    {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    [[nodiscard]] std::pair<std::size_t, bool> find(std::size_t node) const noexcept
    {
        bool parity = false;
        while (parent_[node] != node) {
            parity = parity != (parity_[node] != 0);
            node = parent_[node];
        }
        return { node, parity };
    }

    [[nodiscard]] bool unite(const std::size_t first, const std::size_t second, const bool required_difference) noexcept
    {
        auto [first_root, first_parity] = find(first);
        auto [second_root, second_parity] = find(second);
        if (first_root == second_root) {
            return (first_parity != second_parity) == required_difference;
        }
        const bool link_parity = first_parity != second_parity != required_difference;
        if (rank_[first_root] < rank_[second_root]) {
            std::swap(first_root, second_root);
        }
        parent_[second_root] = first_root;
        parity_[second_root] = static_cast<std::uint8_t>(link_parity);
        if (rank_[first_root] == rank_[second_root]) {
            ++rank_[first_root];
        }
        return true;
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::uint8_t> rank_;
    std::vector<std::uint8_t> parity_;
};

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] MeshLimits structural_limits_for(const TriangleMesh& mesh) noexcept
{
    return {
        .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_vertices = static_cast<std::uint64_t>(mesh.vertices.size()),
        .max_triangles = static_cast<std::uint64_t>(mesh.triangles.size()),
    };
}

[[nodiscard]] Point3 subtract(const Point3& left, const Point3& right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Point3 add(const Point3& left, const Point3& right) noexcept
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

[[nodiscard]] Point3 multiply(const Point3& point, const double scalar) noexcept
{
    return { point.x * scalar, point.y * scalar, point.z * scalar };
}

[[nodiscard]] double dot(const Point3& left, const Point3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Point3 cross(const Point3& left, const Point3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double squared_norm(const Point3& point) noexcept { return dot(point, point); }

void record_edge(std::unordered_map<EdgeKey, EdgeUse, EdgeKeyHash>& edges, const MeshIndex from, const MeshIndex to,
                 const std::size_t face)
{
    const EdgeKey key { std::min(from, to), std::max(from, to) };
    EdgeUse& use = edges[key];
    if (use.count < use.faces.size()) {
        use.faces[static_cast<std::size_t>(use.count)] = { .face = face, .forward = from < to };
    }
    ++use.count;
}

[[nodiscard]] double validate_closed_surface_and_measure_volume(TriangleMesh& mesh, MeshBounds& bounds)
{
    const MeshStatistics statistics = validate_and_measure_mesh(mesh, structural_limits_for(mesh));
    bounds = statistics.bounds;

    std::unordered_map<EdgeKey, EdgeUse, EdgeKeyHash> edges;
    if (mesh.triangles.size() > std::numeric_limits<std::size_t>::max() / 3U) {
        throw Error(ErrorCategory::resource_limit, "mesh has too many triangle edges to validate");
    }
    edges.reserve(mesh.triangles.size() * 3U);
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        const Triangle& triangle = mesh.triangles[face];
        record_edge(edges, triangle[0], triangle[1], face);
        record_edge(edges, triangle[1], triangle[2], face);
        record_edge(edges, triangle[2], triangle[0], face);
    }
    ParityDisjointSet orientation(mesh.triangles.size());
    for (const auto& [edge, use] : edges) {
        static_cast<void>(edge);
        if (use.count != 2) {
            throw Error(ErrorCategory::invalid_mesh, "mesh must be a closed two-manifold surface");
        }
        const bool same_direction = use.faces[0].forward == use.faces[1].forward;
        if (!orientation.unite(use.faces[0].face, use.faces[1].face, same_direction)) {
            throw Error(ErrorCategory::invalid_mesh, "closed mesh surface is not consistently orientable");
        }
    }

    // DEVIATION(IROP-DEV-0011): Normalize inconsistent triangle winding in the
    // query-owned snapshot. Python accepts manifold inputs with locally flipped
    // faces but its signed proximity result can then depend on unreliable normals.
    // See docs/COMPATIBILITY.md.
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        if (orientation.find(face).second) {
            std::swap(mesh.triangles[face][1], mesh.triangles[face][2]);
        }
    }

    std::size_t component_count = 0;
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        component_count += orientation.find(face).first == face ? 1U : 0U;
    }
    // DEVIATION(IROP-DEV-0012): Reject multiple closed components until the
    // domain contract can distinguish disjoint solids from nested cavity shells.
    // Treating every component as positive volume would turn cavities into
    // usable packing space.
    // See docs/COMPATIBILITY.md.
    if (component_count != 1) {
        throw Error(ErrorCategory::invalid_mesh, "closed mesh must contain exactly one connected surface component");
    }

    const Point3 reference {
        bounds.minimum.x + (bounds.maximum.x - bounds.minimum.x) * 0.5,
        bounds.minimum.y + (bounds.maximum.y - bounds.minimum.y) * 0.5,
        bounds.minimum.z + (bounds.maximum.z - bounds.minimum.z) * 0.5,
    };
    std::vector<long double> component_six_volumes(mesh.triangles.size(), 0.0L);
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        const Triangle& triangle = mesh.triangles[face];
        const Point3 first = subtract(mesh.vertices[static_cast<std::size_t>(triangle[0])], reference);
        const Point3 second = subtract(mesh.vertices[static_cast<std::size_t>(triangle[1])], reference);
        const Point3 third = subtract(mesh.vertices[static_cast<std::size_t>(triangle[2])], reference);
        const long double determinant = static_cast<long double>(first.x) *
                                            (static_cast<long double>(second.y) * static_cast<long double>(third.z) -
                                             static_cast<long double>(second.z) * static_cast<long double>(third.y)) -
                                        static_cast<long double>(first.y) *
                                            (static_cast<long double>(second.x) * static_cast<long double>(third.z) -
                                             static_cast<long double>(second.z) * static_cast<long double>(third.x)) +
                                        static_cast<long double>(first.z) *
                                            (static_cast<long double>(second.x) * static_cast<long double>(third.y) -
                                             static_cast<long double>(second.y) * static_cast<long double>(third.x));
        component_six_volumes[orientation.find(face).first] += determinant;
    }

    std::vector<std::uint8_t> reverse_component(mesh.triangles.size(), 0);
    long double volume_long = 0.0L;
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        if (orientation.find(face).first != face) {
            continue;
        }
        const long double signed_six_volume = component_six_volumes[face];
        if (!std::isfinite(signed_six_volume) || signed_six_volume == 0.0L) {
            throw Error(ErrorCategory::invalid_mesh, "closed mesh component has zero or unrepresentable volume");
        }
        reverse_component[face] = static_cast<std::uint8_t>(signed_six_volume < 0.0L);
        volume_long += std::abs(signed_six_volume) / 6.0L;
    }
    for (std::size_t face = 0; face < mesh.triangles.size(); ++face) {
        if (reverse_component[orientation.find(face).first] != 0) {
            std::swap(mesh.triangles[face][1], mesh.triangles[face][2]);
        }
    }

    if (!std::isfinite(volume_long) || volume_long <= 0.0L ||
        volume_long > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw Error(ErrorCategory::invalid_mesh, "closed mesh has a non-positive or unrepresentable enclosed volume");
    }
    return static_cast<double>(volume_long);
}

[[nodiscard]] double squared_distance_to_triangle(const Point3& point, const Point3& first, const Point3& second,
                                                  const Point3& third)
{
    const Point3 edge_ab = subtract(second, first);
    const Point3 edge_ac = subtract(third, first);
    const Point3 from_a = subtract(point, first);
    const double d1 = dot(edge_ab, from_a);
    const double d2 = dot(edge_ac, from_a);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return squared_norm(from_a);
    }

    const Point3 from_b = subtract(point, second);
    const double d3 = dot(edge_ab, from_b);
    const double d4 = dot(edge_ac, from_b);
    if (d3 >= 0.0 && d4 <= d3) {
        return squared_norm(from_b);
    }

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const double weight = d1 / (d1 - d3);
        return squared_norm(subtract(point, add(first, multiply(edge_ab, weight))));
    }

    const Point3 from_c = subtract(point, third);
    const double d5 = dot(edge_ab, from_c);
    const double d6 = dot(edge_ac, from_c);
    if (d6 >= 0.0 && d5 <= d6) {
        return squared_norm(from_c);
    }

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const double weight = d2 / (d2 - d6);
        return squared_norm(subtract(point, add(first, multiply(edge_ac, weight))));
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const Point3 edge_bc = subtract(third, second);
        const double weight = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return squared_norm(subtract(point, add(second, multiply(edge_bc, weight))));
    }

    const double denominator = 1.0 / (va + vb + vc);
    const double weight_b = vb * denominator;
    const double weight_c = vc * denominator;
    const Point3 closest = add(first, add(multiply(edge_ab, weight_b), multiply(edge_ac, weight_c)));
    return squared_norm(subtract(point, closest));
}

[[noreturn]] void throw_combined_limit(const char* kind, const std::uint64_t actual, const std::uint64_t maximum)
{
    throw Error(ErrorCategory::resource_limit, std::string("combined mesh ") + kind + " count " +
                                                   std::to_string(actual) + " exceeds configured limit " +
                                                   std::to_string(maximum));
}

}  // namespace

Point3 vertex_centroid(const TriangleMesh& mesh)
{
    static_cast<void>(validate_and_measure_mesh(mesh, structural_limits_for(mesh)));
    long double x = 0.0L;
    long double y = 0.0L;
    long double z = 0.0L;
    for (const Point3& vertex : mesh.vertices) {
        x += static_cast<long double>(vertex.x);
        y += static_cast<long double>(vertex.y);
        z += static_cast<long double>(vertex.z);
    }
    const long double divisor = static_cast<long double>(mesh.vertices.size());
    const Point3 result {
        static_cast<double>(x / divisor),
        static_cast<double>(y / divisor),
        static_cast<double>(z / divisor),
    };
    if (!is_finite(result)) {
        throw Error(ErrorCategory::invalid_mesh, "mesh vertex centroid is not representable");
    }
    return result;
}

CenteredMesh center_mesh_at_vertex_centroid(const TriangleMesh& mesh)
{
    // COMPATIBILITY(IROP-COMPAT-0003): PyVista center_of_mass without scalar
    // weights is a vertex centroid, so centering deliberately uses that pivot.
    // See docs/COMPATIBILITY.md.
    const Point3 centroid = vertex_centroid(mesh);
    TriangleMesh centered = mesh;
    for (Point3& vertex : centered.vertices) {
        vertex = subtract(vertex, centroid);
    }
    static_cast<void>(validate_and_measure_mesh(centered, structural_limits_for(centered)));
    return { .mesh = std::move(centered), .original_vertex_centroid = centroid };
}

TriangleMesh scale_mesh_to_volume(const TriangleMesh& mesh, const double target_volume)
{
    if (!std::isfinite(target_volume) || target_volume <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "target mesh volume must be finite and positive");
    }
    const ClosedMeshQuery query(mesh);
    const double volume_scale = target_volume / query.volume();
    if (!std::isfinite(volume_scale) || volume_scale <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "target mesh volume produces an invalid scale");
    }
    return transform_mesh(mesh, Transform { .volume_scale = volume_scale });
}

double maximum_radius(const TriangleMesh& mesh, const Point3& center)
{
    static_cast<void>(validate_and_measure_mesh(mesh, structural_limits_for(mesh)));
    if (!is_finite(center)) {
        throw Error(ErrorCategory::invalid_configuration, "mesh radius center must be finite");
    }
    double maximum_squared = 0.0;
    for (const Point3& vertex : mesh.vertices) {
        maximum_squared = std::max(maximum_squared, squared_norm(subtract(vertex, center)));
    }
    if (!std::isfinite(maximum_squared)) {
        throw Error(ErrorCategory::invalid_mesh, "mesh maximum radius is not representable");
    }
    return std::sqrt(maximum_squared);
}

ClosedMeshQuery::ClosedMeshQuery(const TriangleMesh& mesh) : mesh_(mesh), volume_(0.0), bounds_ {}
{
    volume_ = validate_closed_surface_and_measure_volume(mesh_, bounds_);
}

double ClosedMeshQuery::volume() const noexcept { return volume_; }

const MeshBounds& ClosedMeshQuery::bounds() const noexcept { return bounds_; }

bool ClosedMeshQuery::contains(const Point3& point) const
{
    if (!is_finite(point)) {
        throw Error(ErrorCategory::invalid_configuration, "containment query point must be finite");
    }

    double solid_angle = 0.0;
    for (const Triangle& triangle : mesh_.triangles) {
        const Point3 first = subtract(mesh_.vertices[static_cast<std::size_t>(triangle[0])], point);
        const Point3 second = subtract(mesh_.vertices[static_cast<std::size_t>(triangle[1])], point);
        const Point3 third = subtract(mesh_.vertices[static_cast<std::size_t>(triangle[2])], point);
        const double first_norm = std::sqrt(squared_norm(first));
        const double second_norm = std::sqrt(squared_norm(second));
        const double third_norm = std::sqrt(squared_norm(third));
        if (first_norm == 0.0 || second_norm == 0.0 || third_norm == 0.0) {
            return true;
        }
        const double numerator = dot(first, cross(second, third));
        const double denominator = first_norm * second_norm * third_norm + dot(first, second) * third_norm +
                                   dot(second, third) * first_norm + dot(third, first) * second_norm;
        const double angle = 2.0 * std::atan2(numerator, denominator);
        if (!std::isfinite(angle)) {
            throw Error(ErrorCategory::invalid_mesh, "containment query became numerically invalid");
        }
        solid_angle += angle;
    }
    return std::abs(solid_angle) > 2.0 * std::numbers::pi_v<double>;
}

double ClosedMeshQuery::distance_to_surface(const Point3& point) const
{
    if (!is_finite(point)) {
        throw Error(ErrorCategory::invalid_configuration, "distance query point must be finite");
    }
    double minimum_squared = std::numeric_limits<double>::infinity();
    for (const Triangle& triangle : mesh_.triangles) {
        minimum_squared = std::min(
            minimum_squared, squared_distance_to_triangle(point, mesh_.vertices[static_cast<std::size_t>(triangle[0])],
                                                          mesh_.vertices[static_cast<std::size_t>(triangle[1])],
                                                          mesh_.vertices[static_cast<std::size_t>(triangle[2])]));
    }
    if (!std::isfinite(minimum_squared) || minimum_squared < 0.0) {
        throw Error(ErrorCategory::invalid_mesh, "distance-to-surface query became numerically invalid");
    }
    return std::sqrt(minimum_squared);
}

SurfaceIntersectionResult detail::query_validated_surface_intersection(const TriangleMesh& first,
                                                                       const TriangleMesh& second,
                                                                       const std::uint64_t maximum_triangle_pair_tests)
{
    if (maximum_triangle_pair_tests == 0) {
        throw Error(ErrorCategory::resource_limit, "surface-intersection triangle-pair limit is exhausted");
    }

    SurfaceIntersectionResult result;
    for (const Triangle& first_triangle : first.triangles) {
        const Point3& first_a = first.vertices[static_cast<std::size_t>(first_triangle[0])];
        const Point3& first_b = first.vertices[static_cast<std::size_t>(first_triangle[1])];
        const Point3& first_c = first.vertices[static_cast<std::size_t>(first_triangle[2])];
        const std::array<double, 3> first_a_array { first_a.x, first_a.y, first_a.z };
        const std::array<double, 3> first_b_array { first_b.x, first_b.y, first_b.z };
        const std::array<double, 3> first_c_array { first_c.x, first_c.y, first_c.z };
        for (const Triangle& second_triangle : second.triangles) {
            if (result.tested_triangle_pairs == maximum_triangle_pair_tests) {
                throw Error(ErrorCategory::resource_limit, "surface-intersection triangle-pair limit is exhausted");
            }
            ++result.tested_triangle_pairs;
            const Point3& second_a = second.vertices[static_cast<std::size_t>(second_triangle[0])];
            const Point3& second_b = second.vertices[static_cast<std::size_t>(second_triangle[1])];
            const Point3& second_c = second.vertices[static_cast<std::size_t>(second_triangle[2])];
            const std::array<double, 3> second_a_array { second_a.x, second_a.y, second_a.z };
            const std::array<double, 3> second_b_array { second_b.x, second_b.y, second_b.z };
            const std::array<double, 3> second_c_array { second_c.x, second_c.y, second_c.z };
            if (vtkTriangle::TrianglesIntersect(first_a_array.data(), first_b_array.data(), first_c_array.data(),
                                                second_a_array.data(), second_b_array.data(),
                                                second_c_array.data()) != 0) {
                result.intersects = true;
                return result;
            }
        }
    }
    return result;
}

SurfaceIntersectionResult query_surface_intersection(const TriangleMesh& first, const TriangleMesh& second,
                                                     const std::uint64_t maximum_triangle_pair_tests)
{
    static_cast<void>(validate_and_measure_mesh(first, structural_limits_for(first)));
    static_cast<void>(validate_and_measure_mesh(second, structural_limits_for(second)));
    return detail::query_validated_surface_intersection(first, second, maximum_triangle_pair_tests);
}

TriangleMesh combine_meshes(const std::vector<TriangleMesh>& meshes, const MeshLimits& limits)
{
    if (meshes.empty()) {
        throw Error(ErrorCategory::invalid_mesh, "cannot combine an empty mesh collection");
    }

    std::uint64_t vertex_count = 0;
    std::uint64_t triangle_count = 0;
    for (const TriangleMesh& mesh : meshes) {
        static_cast<void>(validate_and_measure_mesh(mesh, limits));
        const std::uint64_t mesh_vertices = static_cast<std::uint64_t>(mesh.vertices.size());
        const std::uint64_t mesh_triangles = static_cast<std::uint64_t>(mesh.triangles.size());
        if (mesh_vertices > limits.max_vertices - vertex_count) {
            const std::uint64_t exceeded = limits.max_vertices == std::numeric_limits<std::uint64_t>::max()
                                               ? limits.max_vertices
                                               : limits.max_vertices + 1;
            throw_combined_limit("vertex", exceeded, limits.max_vertices);
        }
        if (mesh_triangles > limits.max_triangles - triangle_count) {
            const std::uint64_t exceeded = limits.max_triangles == std::numeric_limits<std::uint64_t>::max()
                                               ? limits.max_triangles
                                               : limits.max_triangles + 1;
            throw_combined_limit("triangle", exceeded, limits.max_triangles);
        }
        vertex_count += mesh_vertices;
        triangle_count += mesh_triangles;
    }

    TriangleMesh combined;
    combined.vertices.reserve(static_cast<std::size_t>(vertex_count));
    combined.triangles.reserve(static_cast<std::size_t>(triangle_count));
    for (const TriangleMesh& mesh : meshes) {
        const MeshIndex offset = static_cast<MeshIndex>(combined.vertices.size());
        combined.vertices.insert(combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (const Triangle& triangle : mesh.triangles) {
            combined.triangles.push_back({ triangle[0] + offset, triangle[1] + offset, triangle[2] + offset });
        }
    }
    static_cast<void>(validate_and_measure_mesh(combined, limits));
    return combined;
}

}  // namespace irop
