#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct ObjectCollisionPair {
    std::uint64_t first = 0;
    std::uint64_t second = 0;

    bool operator==(const ObjectCollisionPair&) const noexcept = default;
};

struct SceneCollisionLimits {
    static constexpr std::uint64_t default_max_triangle_pair_tests = 100'000'000ULL;
    static constexpr std::uint64_t default_max_containment_triangle_visits = 100'000'000ULL;
    static constexpr std::uint64_t default_max_reported_violations = 1'000'000ULL;

    std::uint64_t max_triangle_pair_tests = default_max_triangle_pair_tests;
    std::uint64_t max_containment_triangle_visits = default_max_containment_triangle_visits;
    std::uint64_t max_reported_violations = default_max_reported_violations;
};

struct SceneCollisionWork {
    std::uint64_t object_pairs_examined = 0;
    std::uint64_t triangle_pairs_tested = 0;
    std::uint64_t containment_triangle_visits = 0;
};

struct SceneCollisionReport {
    std::vector<std::uint64_t> cat_violation_object_ids;
    std::vector<std::uint64_t> container_violation_object_ids;
    std::vector<ObjectCollisionPair> object_collisions;
    SceneCollisionWork work;

    [[nodiscard]] bool physical_scene_valid() const noexcept
    {
        return container_violation_object_ids.empty() && object_collisions.empty();
    }
};

// CAT surfaces may be empty to skip CAT reporting, otherwise one surface must
// correspond to each object. Physical validity is evaluated against the
// supplied object/container meshes; CAT contacts are diagnostic only.
[[nodiscard]] SceneCollisionReport validate_scene_collisions(std::span<const TriangleMesh> objects,
                                                             const TriangleMesh& container,
                                                             std::span<const TriangleMesh> cat_surfaces = {},
                                                             const SceneCollisionLimits& limits = {});

}  // namespace irop
