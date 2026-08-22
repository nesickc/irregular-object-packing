#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

using ParticipantId = std::uint32_t;
using Tetrahedron = std::array<MeshIndex, 4>;

struct TetrahedralMesh {
    std::vector<Point3> points;
    std::vector<Tetrahedron> tetrahedra;
    std::vector<ParticipantId> point_owners;
    std::uint64_t participant_count = 0;
};

struct TetrahedralizationLimits {
    static constexpr std::uint64_t default_max_participants = 1'000'000ULL;
    static constexpr std::uint64_t default_max_input_points = 15'000'000ULL;
    static constexpr std::uint64_t default_max_input_triangles = 5'000'000ULL;
    static constexpr std::uint64_t default_max_output_points = 15'000'000ULL;
    static constexpr std::uint64_t default_max_output_tetrahedra = 5'000'000ULL;

    std::uint64_t max_participants = default_max_participants;
    std::uint64_t max_input_points = default_max_input_points;
    std::uint64_t max_input_triangles = default_max_input_triangles;
    std::uint64_t max_output_points = default_max_output_points;
    std::uint64_t max_output_tetrahedra = default_max_output_tetrahedra;
};

enum class TetrahedralizationStatus {
    success,
    invalid_input,
    resource_exhausted,
    dependency_failure,
};

[[nodiscard]] const char* to_string(TetrahedralizationStatus status) noexcept;

struct TetrahedralizationWork {
    std::uint64_t input_participants = 0;
    std::uint64_t input_points = 0;
    std::uint64_t input_triangles = 0;
    std::uint64_t output_points = 0;
    std::uint64_t output_tetrahedra = 0;
};

struct TetrahedralizationResult {
    TetrahedralizationStatus status = TetrahedralizationStatus::invalid_input;
    TetrahedralMesh mesh;
    TetrahedralizationWork work;
    std::string diagnostic;

    [[nodiscard]] bool succeeded() const noexcept { return status == TetrahedralizationStatus::success; }
};

// Participants are ordered object surfaces followed by the container surface.
// Point ownership in the result uses the corresponding zero-based participant ID.
[[nodiscard]] TetrahedralizationResult tetrahedralize_surfaces(std::span<const TriangleMesh> participants,
                                                               const TetrahedralizationLimits& limits = {});

}  // namespace irop
