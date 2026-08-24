#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "irop/geometry/transform.hpp"
#include "irop/model/triangle_mesh.hpp"

namespace irop {

class DeterministicRandomState final {
public:
    explicit DeterministicRandomState(std::uint32_t seed);

    [[nodiscard]] std::uint32_t seed() const noexcept;
    [[nodiscard]] std::uint64_t draw_count() const noexcept;
    [[nodiscard]] double uniform(double lower, double upper);

private:
    std::uint32_t seed_;
    std::uint64_t draw_count_ = 0;
    std::mt19937 engine_;
};

struct PackingConfig {
    static constexpr std::uint64_t default_max_sampling_attempts = 1'000'000ULL;
    static constexpr std::uint64_t default_max_geometry_query_triangle_visits = 100'000'000ULL;
    static constexpr std::uint64_t default_max_pairwise_distance_checks = 100'000'000ULL;
    static constexpr std::uint64_t default_max_surface_intersection_triangle_pairs = 100'000'000ULL;

    std::uint64_t object_count = 1;
    double initial_volume_scale = 0.1;
    std::uint32_t seed = 1918;
    std::uint64_t max_sampling_attempts = default_max_sampling_attempts;
    std::uint64_t max_geometry_query_triangle_visits = default_max_geometry_query_triangle_visits;
    std::uint64_t max_pairwise_distance_checks = default_max_pairwise_distance_checks;
    std::uint64_t max_surface_intersection_triangle_pairs = default_max_surface_intersection_triangle_pairs;
    MeshLimits output_mesh_limits;

    // DEVIATION(IROP-DEV-0003): The unused Python `new_cat` tuple/Boolean
    // option is deliberately absent from the project-owned configuration.
    // See docs/COMPATIBILITY.md.
};

struct PackingState {
    explicit PackingState(PackingConfig configuration);

    // DEVIATION(IROP-DEV-0002): Every run owns its state and random stream;
    // mutable history is never shared between packing instances.
    // See docs/COMPATIBILITY.md.
    PackingConfig config;
    DeterministicRandomState random_state;
    std::vector<Transform> transforms;
    double object_volume = 0.0;
    double container_volume = 0.0;
    double initial_linear_scale = 0.0;
    double object_bounding_radius = 0.0;
    double minimum_center_distance = 0.0;
    std::uint64_t rejected_candidate_count = 0;
    std::uint64_t geometry_query_triangle_visits = 0;
    std::uint64_t pairwise_distance_checks = 0;
    std::uint64_t surface_intersection_triangle_pairs = 0;
};

// Validates the project-owned initialization configuration without inspecting
// mesh data. Application services can use this before performing input I/O.
void validate_packing_config(const PackingConfig& config);

// `centered_object` is the full-size object template centered at the origin.
[[nodiscard]] PackingState initialize_packing(const TriangleMesh& centered_object, const TriangleMesh& container,
                                              const PackingConfig& config,
                                              const std::function<bool()>& cancellation_requested = {});

void validate_initial_state(const TriangleMesh& centered_object, const TriangleMesh& container,
                            const PackingState& state, const std::function<bool()>& cancellation_requested = {});

[[nodiscard]] std::vector<TriangleMesh> instantiate_objects(const TriangleMesh& centered_object,
                                                            const PackingState& state);

}  // namespace irop
