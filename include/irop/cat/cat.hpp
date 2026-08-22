#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "irop/tetrahedralization/tetrahedralization.hpp"

namespace irop {

struct CatConstructionLimits {
    static constexpr std::uint64_t default_max_participants = 1'000'000ULL;
    static constexpr std::uint64_t default_max_points = 15'000'000ULL;
    static constexpr std::uint64_t default_max_tetrahedra = 5'000'000ULL;
    static constexpr std::uint64_t default_max_relevant_tetrahedra = 5'000'000ULL;
    static constexpr std::uint64_t default_max_polygons = 20'000'000ULL;
    static constexpr std::uint64_t default_max_polygon_vertices = 80'000'000ULL;
    static constexpr std::uint64_t default_max_constraints = 20'000'000ULL;

    std::uint64_t max_participants = default_max_participants;
    std::uint64_t max_points = default_max_points;
    std::uint64_t max_tetrahedra = default_max_tetrahedra;
    std::uint64_t max_relevant_tetrahedra = default_max_relevant_tetrahedra;
    std::uint64_t max_polygons = default_max_polygons;
    std::uint64_t max_polygon_vertices = default_max_polygon_vertices;
    std::uint64_t max_constraints = default_max_constraints;
};

struct CatPolygon {
    std::array<Point3, 4> vertices {};
    std::uint8_t vertex_count = 0;
    ParticipantId owner = 0;
    MeshIndex tetrahedron = 0;
};

struct CatPlaneConstraint {
    ParticipantId owner = 0;
    MeshIndex source_point = 0;
    Point3 plane_point;
    Point3 inward_unit_normal;
    MeshIndex polygon = 0;
    MeshIndex tetrahedron = 0;
};

struct CatParticipantRange {
    std::uint64_t polygon_begin = 0;
    std::uint64_t polygon_count = 0;
    std::uint64_t constraint_begin = 0;
    std::uint64_t constraint_count = 0;
};

enum class CatConstructionStatus {
    success,
    invalid_input,
    resource_exhausted,
};

[[nodiscard]] const char* to_string(CatConstructionStatus status) noexcept;

struct CatConstructionWork {
    std::uint64_t points_examined = 0;
    std::uint64_t tetrahedra_examined = 0;
    std::uint64_t relevant_tetrahedra = 0;
    std::uint64_t skipped_single_participant_tetrahedra = 0;
    std::uint64_t polygons_generated = 0;
    std::uint64_t polygon_vertices_generated = 0;
    std::uint64_t constraints_generated = 0;
};

struct CatConstructionResult {
    CatConstructionStatus status = CatConstructionStatus::invalid_input;
    std::vector<CatPolygon> polygons;
    std::vector<CatPlaneConstraint> constraints;
    std::vector<CatParticipantRange> participant_ranges;
    CatConstructionWork work;
    std::string diagnostic;

    [[nodiscard]] bool succeeded() const noexcept { return status == CatConstructionStatus::success; }
};

// Constructs the Python-compatible CAT split geometry and inward plane constraints.
// Output vectors are grouped by participant using participant_ranges.
[[nodiscard]] CatConstructionResult build_cat(const TetrahedralMesh& mesh, const CatConstructionLimits& limits = {});

}  // namespace irop
