#include "irop/cat/cat.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace irop {
namespace {

struct CellVertex {
    MeshIndex point = 0;
    ParticipantId owner = 0;
    std::size_t multiplicity = 0;
};

struct GeneratedFace {
    std::array<Point3, 4> vertices {};
    std::uint8_t vertex_count = 0;
};

struct VertexFaces {
    std::array<GeneratedFace, 6> faces {};
    std::size_t count = 0;
};

using SplitFaces = std::array<VertexFaces, 4>;

struct ClassifiedCell {
    std::array<CellVertex, 4> vertices {};
    std::array<std::size_t, 4> run_lengths {};
    std::size_t run_count = 0;
};

struct WorkingParticipant {
    std::vector<CatPolygon> polygons;
    std::vector<CatPlaneConstraint> constraints;
};

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] Point3 average_two(const Point3& first, const Point3& second) noexcept
{
    return {
        static_cast<double>((static_cast<long double>(first.x) + static_cast<long double>(second.x)) / 2.0L),
        static_cast<double>((static_cast<long double>(first.y) + static_cast<long double>(second.y)) / 2.0L),
        static_cast<double>((static_cast<long double>(first.z) + static_cast<long double>(second.z)) / 2.0L),
    };
}

[[nodiscard]] Point3 average_three(const Point3& first, const Point3& second, const Point3& third) noexcept
{
    return {
        static_cast<double>((static_cast<long double>(first.x) + static_cast<long double>(second.x) +
                             static_cast<long double>(third.x)) /
                            3.0L),
        static_cast<double>((static_cast<long double>(first.y) + static_cast<long double>(second.y) +
                             static_cast<long double>(third.y)) /
                            3.0L),
        static_cast<double>((static_cast<long double>(first.z) + static_cast<long double>(second.z) +
                             static_cast<long double>(third.z)) /
                            3.0L),
    };
}

[[nodiscard]] Point3 average_four(const Point3& first, const Point3& second, const Point3& third,
                                  const Point3& fourth) noexcept
{
    return {
        static_cast<double>((static_cast<long double>(first.x) + static_cast<long double>(second.x) +
                             static_cast<long double>(third.x) + static_cast<long double>(fourth.x)) /
                            4.0L),
        static_cast<double>((static_cast<long double>(first.y) + static_cast<long double>(second.y) +
                             static_cast<long double>(third.y) + static_cast<long double>(fourth.y)) /
                            4.0L),
        static_cast<double>((static_cast<long double>(first.z) + static_cast<long double>(second.z) +
                             static_cast<long double>(third.z) + static_cast<long double>(fourth.z)) /
                            4.0L),
    };
}

[[nodiscard]] GeneratedFace triangle(const Point3& first, const Point3& second, const Point3& third) noexcept
{
    GeneratedFace face;
    face.vertices = { first, second, third, {} };
    face.vertex_count = 3;
    return face;
}

[[nodiscard]] GeneratedFace quad(const Point3& first, const Point3& second, const Point3& third,
                                 const Point3& fourth) noexcept
{
    GeneratedFace face;
    face.vertices = { first, second, third, fourth };
    face.vertex_count = 4;
    return face;
}

template <typename... FaceTypes>
[[nodiscard]] VertexFaces face_set(FaceTypes... faces) noexcept
{
    static_assert(sizeof...(faces) <= 6);
    VertexFaces result;
    ((result.faces[result.count++] = faces), ...);
    return result;
}

[[nodiscard]] SplitFaces split_four_participants(const std::array<Point3, 4>& points) noexcept
{
    const Point3 center = average_four(points[0], points[1], points[2], points[3]);
    const Point3 edge_01 = average_two(points[0], points[1]);
    const Point3 edge_02 = average_two(points[0], points[2]);
    const Point3 edge_03 = average_two(points[0], points[3]);
    const Point3 edge_12 = average_two(points[1], points[2]);
    const Point3 edge_13 = average_two(points[1], points[3]);
    const Point3 edge_23 = average_two(points[2], points[3]);
    const Point3 face_012 = average_three(points[0], points[1], points[2]);
    const Point3 face_013 = average_three(points[0], points[1], points[3]);
    const Point3 face_023 = average_three(points[0], points[2], points[3]);
    const Point3 face_123 = average_three(points[1], points[2], points[3]);

    return {
        face_set(triangle(center, edge_01, face_012), triangle(center, face_012, edge_02),
                 triangle(center, edge_02, face_023), triangle(center, face_023, edge_03),
                 triangle(center, edge_03, face_013), triangle(center, face_013, edge_01)),
        face_set(triangle(center, edge_01, face_012), triangle(center, face_012, edge_12),
                 triangle(center, edge_12, face_123), triangle(center, face_123, edge_13),
                 triangle(center, edge_13, face_013), triangle(center, face_013, edge_01)),
        face_set(triangle(center, edge_02, face_012), triangle(center, face_012, edge_12),
                 triangle(center, edge_12, face_123), triangle(center, face_123, edge_23),
                 triangle(center, edge_23, face_023), triangle(center, face_023, edge_02)),
        face_set(triangle(center, edge_03, face_013), triangle(center, face_013, edge_13),
                 triangle(center, edge_13, face_123), triangle(center, face_123, edge_23),
                 triangle(center, edge_23, face_023), triangle(center, face_023, edge_03)),
    };
}

[[nodiscard]] SplitFaces split_two_one_one(const std::array<Point3, 4>& points) noexcept
{
    const Point3 face_023 = average_three(points[0], points[2], points[3]);
    const Point3 face_123 = average_three(points[1], points[2], points[3]);
    const Point3 edge_02 = average_two(points[0], points[2]);
    const Point3 edge_03 = average_two(points[0], points[3]);
    const Point3 edge_12 = average_two(points[1], points[2]);
    const Point3 edge_13 = average_two(points[1], points[3]);
    const Point3 edge_23 = average_two(points[2], points[3]);

    const GeneratedFace shared_with_second = quad(face_023, face_123, edge_12, edge_02);
    const GeneratedFace shared_with_third = quad(face_023, face_123, edge_13, edge_03);
    const GeneratedFace between_singletons = triangle(face_023, face_123, edge_23);

    return {
        face_set(shared_with_second, shared_with_third),
        face_set(shared_with_second, shared_with_third),
        face_set(shared_with_second, between_singletons),
        face_set(shared_with_third, between_singletons),
    };
}

[[nodiscard]] SplitFaces split_three_one(const std::array<Point3, 4>& points) noexcept
{
    const GeneratedFace face = triangle(average_two(points[0], points[3]), average_two(points[1], points[3]),
                                        average_two(points[2], points[3]));
    return { face_set(face), face_set(face), face_set(face), face_set(face) };
}

[[nodiscard]] SplitFaces split_two_two(const std::array<Point3, 4>& points) noexcept
{
    const GeneratedFace face = quad(average_two(points[0], points[2]), average_two(points[0], points[3]),
                                    average_two(points[1], points[3]), average_two(points[1], points[2]));
    return { face_set(face), face_set(face), face_set(face), face_set(face) };
}

[[nodiscard]] bool tetrahedron_is_nondegenerate(const std::array<Point3, 4>& points) noexcept
{
    const long double ab_x = static_cast<long double>(points[1].x) - static_cast<long double>(points[0].x);
    const long double ab_y = static_cast<long double>(points[1].y) - static_cast<long double>(points[0].y);
    const long double ab_z = static_cast<long double>(points[1].z) - static_cast<long double>(points[0].z);
    const long double ac_x = static_cast<long double>(points[2].x) - static_cast<long double>(points[0].x);
    const long double ac_y = static_cast<long double>(points[2].y) - static_cast<long double>(points[0].y);
    const long double ac_z = static_cast<long double>(points[2].z) - static_cast<long double>(points[0].z);
    const long double ad_x = static_cast<long double>(points[3].x) - static_cast<long double>(points[0].x);
    const long double ad_y = static_cast<long double>(points[3].y) - static_cast<long double>(points[0].y);
    const long double ad_z = static_cast<long double>(points[3].z) - static_cast<long double>(points[0].z);

    const long double cross_x = ac_y * ad_z - ac_z * ad_y;
    const long double cross_y = ac_z * ad_x - ac_x * ad_z;
    const long double cross_z = ac_x * ad_y - ac_y * ad_x;
    const long double six_volume = ab_x * cross_x + ab_y * cross_y + ab_z * cross_z;
    return std::isfinite(six_volume) && six_volume != 0.0L;
}

[[nodiscard]] bool compute_inward_unit_normal(const GeneratedFace& face, const Point3& source, Point3& normal) noexcept
{
    const Point3& first = face.vertices[0];
    const Point3& second = face.vertices[1];
    const Point3& third = face.vertices[2];
    const long double first_edge_x = static_cast<long double>(second.x) - static_cast<long double>(first.x);
    const long double first_edge_y = static_cast<long double>(second.y) - static_cast<long double>(first.y);
    const long double first_edge_z = static_cast<long double>(second.z) - static_cast<long double>(first.z);
    const long double second_edge_x = static_cast<long double>(third.x) - static_cast<long double>(first.x);
    const long double second_edge_y = static_cast<long double>(third.y) - static_cast<long double>(first.y);
    const long double second_edge_z = static_cast<long double>(third.z) - static_cast<long double>(first.z);

    long double normal_x = first_edge_y * second_edge_z - first_edge_z * second_edge_y;
    long double normal_y = first_edge_z * second_edge_x - first_edge_x * second_edge_z;
    long double normal_z = first_edge_x * second_edge_y - first_edge_y * second_edge_x;
    const long double squared_length = normal_x * normal_x + normal_y * normal_y + normal_z * normal_z;
    if (!std::isfinite(squared_length) || squared_length <= 0.0L) {
        return false;
    }

    const long double source_x = static_cast<long double>(source.x) - static_cast<long double>(first.x);
    const long double source_y = static_cast<long double>(source.y) - static_cast<long double>(first.y);
    const long double source_z = static_cast<long double>(source.z) - static_cast<long double>(first.z);
    const long double orientation = normal_x * source_x + normal_y * source_y + normal_z * source_z;
    if (!std::isfinite(orientation) || orientation == 0.0L) {
        return false;
    }
    if (orientation < 0.0L) {
        normal_x = -normal_x;
        normal_y = -normal_y;
        normal_z = -normal_z;
    }

    const long double length = std::sqrt(squared_length);
    normal = {
        static_cast<double>(normal_x / length),
        static_cast<double>(normal_y / length),
        static_cast<double>(normal_z / length),
    };
    return is_finite(normal);
}

[[nodiscard]] ClassifiedCell classify_cell(const Tetrahedron& tetrahedron,
                                           const std::vector<ParticipantId>& point_owners)
{
    ClassifiedCell classified;
    for (std::size_t index = 0; index < classified.vertices.size(); ++index) {
        const MeshIndex point = tetrahedron[index];
        classified.vertices[index] = {
            .point = point,
            .owner = point_owners[static_cast<std::size_t>(point)],
            .multiplicity = 0,
        };
    }
    for (CellVertex& vertex : classified.vertices) {
        vertex.multiplicity = static_cast<std::size_t>(std::count_if(
            classified.vertices.begin(), classified.vertices.end(), [&vertex](const CellVertex& candidate) {
            return candidate.owner == vertex.owner;
        }));
    }

    // Python uses a stable descending sort by (owner occurrence count, owner ID).
    std::stable_sort(classified.vertices.begin(), classified.vertices.end(),
                     [](const CellVertex& left, const CellVertex& right) {
        if (left.multiplicity != right.multiplicity) {
            return left.multiplicity > right.multiplicity;
        }
        return left.owner > right.owner;
    });

    std::size_t index = 0;
    while (index < classified.vertices.size()) {
        const ParticipantId owner = classified.vertices[index].owner;
        std::size_t end = index + 1;
        while (end < classified.vertices.size() && classified.vertices[end].owner == owner) {
            ++end;
        }
        classified.run_lengths[classified.run_count++] = end - index;
        index = end;
    }
    return classified;
}

[[nodiscard]] bool limits_are_positive(const CatConstructionLimits& limits) noexcept
{
    return limits.max_participants > 0 && limits.max_points > 0 && limits.max_tetrahedra > 0 &&
           limits.max_relevant_tetrahedra > 0 && limits.max_polygons > 0 && limits.max_polygon_vertices > 0 &&
           limits.max_constraints > 0;
}

[[nodiscard]] bool can_add(const std::uint64_t current, const std::uint64_t amount,
                           const std::uint64_t maximum) noexcept
{
    return current <= maximum && amount <= maximum - current;
}

[[nodiscard]] CatConstructionResult failure(const CatConstructionStatus status, std::string diagnostic,
                                            const CatConstructionWork& work)
{
    CatConstructionResult result;
    result.status = status;
    result.work = work;
    result.diagnostic = std::move(diagnostic);
    return result;
}

[[nodiscard]] CatConstructionResult build_cat_impl(const TetrahedralMesh& mesh, const CatConstructionLimits& limits,
                                                   CatConstructionWork& work)
{
    if (!limits_are_positive(limits)) {
        return failure(CatConstructionStatus::invalid_input, "CAT construction limits must be positive", work);
    }

    // DEVIATION(IROP-DEV-0013): Validate ownership, indices, finite geometry, and nondegeneracy instead of
    // inheriting the Python path's unchecked array assumptions. See docs/COMPATIBILITY.md.
    if (mesh.participant_count == 0) {
        return failure(CatConstructionStatus::invalid_input, "CAT construction requires at least one participant",
                       work);
    }
    if (mesh.participant_count > limits.max_participants) {
        return failure(CatConstructionStatus::resource_exhausted, "CAT participant limit is exhausted", work);
    }
    if (mesh.participant_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return failure(CatConstructionStatus::resource_exhausted, "CAT participant count exceeds addressable memory",
                       work);
    }
    if (mesh.points.size() != mesh.point_owners.size()) {
        return failure(CatConstructionStatus::invalid_input, "CAT point ownership count must match the point count",
                       work);
    }
    if (mesh.points.empty()) {
        return failure(CatConstructionStatus::invalid_input, "CAT tetrahedral mesh contains no points", work);
    }
    if (static_cast<std::uint64_t>(mesh.points.size()) > limits.max_points) {
        return failure(CatConstructionStatus::resource_exhausted, "CAT point limit is exhausted", work);
    }
    if (static_cast<std::uint64_t>(mesh.tetrahedra.size()) > limits.max_tetrahedra) {
        return failure(CatConstructionStatus::resource_exhausted, "CAT tetrahedron examination limit is exhausted",
                       work);
    }

    for (std::size_t point_index = 0; point_index < mesh.points.size(); ++point_index) {
        ++work.points_examined;
        if (!is_finite(mesh.points[point_index])) {
            return failure(CatConstructionStatus::invalid_input, "CAT tetrahedral mesh contains a non-finite point",
                           work);
        }
        if (static_cast<std::uint64_t>(mesh.point_owners[point_index]) >= mesh.participant_count) {
            return failure(CatConstructionStatus::invalid_input, "CAT point owner is outside the participant range",
                           work);
        }
    }

    std::vector<WorkingParticipant> working(static_cast<std::size_t>(mesh.participant_count));
    for (std::size_t tetrahedron_index = 0; tetrahedron_index < mesh.tetrahedra.size(); ++tetrahedron_index) {
        ++work.tetrahedra_examined;
        const Tetrahedron& tetrahedron = mesh.tetrahedra[tetrahedron_index];
        const std::uint64_t point_count = static_cast<std::uint64_t>(mesh.points.size());
        if (tetrahedron[0] >= point_count || tetrahedron[1] >= point_count || tetrahedron[2] >= point_count ||
            tetrahedron[3] >= point_count) {
            return failure(CatConstructionStatus::invalid_input, "CAT tetrahedron contains an out-of-range point index",
                           work);
        }
        for (std::size_t first = 0; first < tetrahedron.size(); ++first) {
            for (std::size_t second = first + 1; second < tetrahedron.size(); ++second) {
                if (tetrahedron[first] == tetrahedron[second]) {
                    return failure(CatConstructionStatus::invalid_input,
                                   "CAT tetrahedron contains repeated point indices", work);
                }
            }
        }

        std::array<Point3, 4> original_points {};
        for (std::size_t index = 0; index < original_points.size(); ++index) {
            original_points[index] = mesh.points[static_cast<std::size_t>(tetrahedron[index])];
        }
        if (!tetrahedron_is_nondegenerate(original_points)) {
            return failure(CatConstructionStatus::invalid_input, "CAT tetrahedron is degenerate or numerically invalid",
                           work);
        }

        const ClassifiedCell cell = classify_cell(tetrahedron, mesh.point_owners);
        if (cell.run_count == 1) {
            ++work.skipped_single_participant_tetrahedra;
            continue;
        }

        // DEVIATION(IROP-DEV-0014): Bound relevant-cell and generated CAT work that is unbounded in Python.
        // See docs/COMPATIBILITY.md.
        if (!can_add(work.relevant_tetrahedra, 1, limits.max_relevant_tetrahedra)) {
            return failure(CatConstructionStatus::resource_exhausted, "CAT relevant-tetrahedron limit is exhausted",
                           work);
        }
        ++work.relevant_tetrahedra;

        std::array<Point3, 4> sorted_points {};
        for (std::size_t index = 0; index < sorted_points.size(); ++index) {
            sorted_points[index] = mesh.points[static_cast<std::size_t>(cell.vertices[index].point)];
        }

        SplitFaces split_faces;
        if (cell.run_count == 4) {
            split_faces = split_four_participants(sorted_points);
        }
        else if (cell.run_count == 3 && cell.run_lengths[0] == 2) {
            split_faces = split_two_one_one(sorted_points);
        }
        else if (cell.run_count == 2 && cell.run_lengths[0] == 3) {
            split_faces = split_three_one(sorted_points);
        }
        else if (cell.run_count == 2 && cell.run_lengths[0] == 2 && cell.run_lengths[1] == 2) {
            split_faces = split_two_two(sorted_points);
        }
        else {
            assert(false && "four vertices must form a recognized ownership partition");
            return failure(CatConstructionStatus::invalid_input,
                           "CAT tetrahedron has an unrecognized ownership partition", work);
        }

        std::array<std::array<MeshIndex, 6>, 4> polygon_indices {};
        for (std::size_t vertex_index = 0; vertex_index < cell.vertices.size(); ++vertex_index) {
            const CellVertex& cell_vertex = cell.vertices[vertex_index];
            WorkingParticipant& participant = working[static_cast<std::size_t>(cell_vertex.owner)];
            const VertexFaces& vertex_faces = split_faces[vertex_index];

            std::size_t first_owner_vertex = vertex_index;
            for (std::size_t prior = 0; prior < vertex_index; ++prior) {
                if (cell.vertices[prior].owner == cell_vertex.owner) {
                    first_owner_vertex = prior;
                    break;
                }
            }
            const bool first_for_owner = first_owner_vertex == vertex_index;

            for (std::size_t face_index = 0; face_index < vertex_faces.count; ++face_index) {
                const GeneratedFace& face = vertex_faces.faces[face_index];
                for (std::size_t face_vertex = 0; face_vertex < face.vertex_count; ++face_vertex) {
                    if (!is_finite(face.vertices[face_vertex])) {
                        return failure(CatConstructionStatus::invalid_input,
                                       "CAT split generated a non-finite face point", work);
                    }
                }

                if (first_for_owner) {
                    if (!can_add(work.polygons_generated, 1, limits.max_polygons)) {
                        return failure(CatConstructionStatus::resource_exhausted, "CAT polygon limit is exhausted",
                                       work);
                    }
                    if (!can_add(work.polygon_vertices_generated, face.vertex_count, limits.max_polygon_vertices)) {
                        return failure(CatConstructionStatus::resource_exhausted,
                                       "CAT polygon-vertex limit is exhausted", work);
                    }

                    const MeshIndex polygon_index = static_cast<MeshIndex>(participant.polygons.size());
                    CatPolygon polygon;
                    polygon.vertices = face.vertices;
                    polygon.vertex_count = face.vertex_count;
                    polygon.owner = cell_vertex.owner;
                    polygon.tetrahedron = static_cast<MeshIndex>(tetrahedron_index);
                    participant.polygons.push_back(polygon);
                    polygon_indices[vertex_index][face_index] = polygon_index;
                    ++work.polygons_generated;
                    work.polygon_vertices_generated += face.vertex_count;
                }
                else {
                    assert(vertex_faces.count == split_faces[first_owner_vertex].count);
                    polygon_indices[vertex_index][face_index] = polygon_indices[first_owner_vertex][face_index];
                }

                Point3 inward_normal;
                const Point3& source = mesh.points[static_cast<std::size_t>(cell_vertex.point)];
                if (!compute_inward_unit_normal(face, source, inward_normal)) {
                    return failure(CatConstructionStatus::invalid_input,
                                   "CAT split produced a degenerate or unoriented face", work);
                }
                if (!can_add(work.constraints_generated, 1, limits.max_constraints)) {
                    return failure(CatConstructionStatus::resource_exhausted, "CAT plane-constraint limit is exhausted",
                                   work);
                }

                participant.constraints.push_back({
                    .owner = cell_vertex.owner,
                    .source_point = cell_vertex.point,
                    .plane_point = face.vertices[0],
                    .inward_unit_normal = inward_normal,
                    .polygon = polygon_indices[vertex_index][face_index],
                    .tetrahedron = static_cast<MeshIndex>(tetrahedron_index),
                });
                ++work.constraints_generated;
            }
        }
    }

    CatConstructionResult result;
    result.status = CatConstructionStatus::success;
    result.work = work;
    result.participant_ranges.resize(static_cast<std::size_t>(mesh.participant_count));
    result.polygons.reserve(static_cast<std::size_t>(work.polygons_generated));
    result.constraints.reserve(static_cast<std::size_t>(work.constraints_generated));

    for (std::size_t participant_index = 0; participant_index < working.size(); ++participant_index) {
        WorkingParticipant& participant = working[participant_index];
        CatParticipantRange& range = result.participant_ranges[participant_index];
        range.polygon_begin = static_cast<std::uint64_t>(result.polygons.size());
        range.polygon_count = static_cast<std::uint64_t>(participant.polygons.size());
        range.constraint_begin = static_cast<std::uint64_t>(result.constraints.size());
        range.constraint_count = static_cast<std::uint64_t>(participant.constraints.size());

        for (CatPolygon& polygon : participant.polygons) {
            result.polygons.push_back(std::move(polygon));
        }
        for (CatPlaneConstraint constraint : participant.constraints) {
            assert(can_add(range.polygon_begin, constraint.polygon, std::numeric_limits<MeshIndex>::max()));
            constraint.polygon += range.polygon_begin;
            result.constraints.push_back(constraint);
        }
    }
    return result;
}

}  // namespace

const char* to_string(const CatConstructionStatus status) noexcept
{
    switch (status) {
    case CatConstructionStatus::success:
        return "success";
    case CatConstructionStatus::invalid_input:
        return "invalid_input";
    case CatConstructionStatus::resource_exhausted:
        return "resource_exhausted";
    }
    return "unknown";
}

CatConstructionResult build_cat(const TetrahedralMesh& mesh, const CatConstructionLimits& limits)
{
    CatConstructionWork work;
    try {
        return build_cat_impl(mesh, limits, work);
    }
    catch (const std::bad_alloc&) {
        return failure(CatConstructionStatus::resource_exhausted,
                       "CAT construction could not allocate bounded working memory", work);
    }
    catch (const std::length_error&) {
        return failure(CatConstructionStatus::resource_exhausted, "CAT construction output exceeds addressable memory",
                       work);
    }
}

}  // namespace irop
