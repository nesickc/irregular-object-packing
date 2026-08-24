#include "irop/packing/initialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/model/mesh_validation.hpp"

namespace irop {
namespace {

void throw_if_cancelled(const std::function<bool()>& cancellation_requested)
{
    if (!cancellation_requested) {
        return;
    }
    bool requested = false;
    try {
        requested = cancellation_requested();
    }
    catch (...) {
        throw Error(ErrorCategory::internal, "packing cancellation callback failed");
    }
    if (requested) {
        throw Error(ErrorCategory::cancelled, "packing initialization was cancelled");
    }
}

[[nodiscard]] Point3 subtract(const Point3& left, const Point3& right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] double squared_norm(const Point3& point) noexcept
{
    return point.x * point.x + point.y * point.y + point.z * point.z;
}

void validate_output_size(const TriangleMesh& object, const PackingConfig& config)
{
    const std::uint64_t vertices = static_cast<std::uint64_t>(object.vertices.size());
    const std::uint64_t triangles = static_cast<std::uint64_t>(object.triangles.size());
    if (vertices != 0 && config.object_count > config.output_mesh_limits.max_vertices / vertices) {
        throw Error(ErrorCategory::resource_limit, "initialized objects exceed the configured output vertex limit");
    }
    if (triangles != 0 && config.object_count > config.output_mesh_limits.max_triangles / triangles) {
        throw Error(ErrorCategory::resource_limit, "initialized objects exceed the configured output triangle limit");
    }
}

void consume_work(const std::uint64_t amount, const std::uint64_t limit, std::uint64_t& consumed,
                  const char* description)
{
    if (consumed > limit || amount > limit - consumed) {
        throw Error(ErrorCategory::resource_limit, std::string("initialization exhausted the ") + description);
    }
    consumed += amount;
}

[[nodiscard]] bool bounded_contains(const ClosedMeshQuery& query, const Point3& point,
                                    const std::uint64_t triangle_count, const PackingConfig& config,
                                    std::uint64_t& triangle_visits)
{
    consume_work(triangle_count, config.max_geometry_query_triangle_visits, triangle_visits,
                 "geometry-query triangle-visit limit");
    return query.contains(point);
}

[[nodiscard]] double bounded_distance_to_surface(const ClosedMeshQuery& query, const Point3& point,
                                                 const std::uint64_t triangle_count, const PackingConfig& config,
                                                 std::uint64_t& triangle_visits)
{
    consume_work(triangle_count, config.max_geometry_query_triangle_visits, triangle_visits,
                 "geometry-query triangle-visit limit");
    return query.distance_to_surface(point);
}

[[nodiscard]] bool is_far_enough_from_centers(const Point3& candidate, const std::vector<Transform>& transforms,
                                              const double minimum_distance_squared, const PackingConfig& config,
                                              std::uint64_t& pairwise_checks,
                                              const std::function<bool()>& cancellation_requested)
{
    for (const Transform& transform : transforms) {
        throw_if_cancelled(cancellation_requested);
        consume_work(1, config.max_pairwise_distance_checks, pairwise_checks, "pairwise-distance-check limit");
        if (!(squared_norm(subtract(candidate, transform.translation)) > minimum_distance_squared)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_finite(const EulerRotationRadians& rotation) noexcept
{
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z);
}

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool bounding_sphere_is_contained(const Transform& transform, const ClosedMeshQuery& container_query,
                                                const std::uint64_t container_triangles, const double radius,
                                                const PackingConfig& config, std::uint64_t& triangle_visits,
                                                const std::function<bool()>& cancellation_requested)
{
    throw_if_cancelled(cancellation_requested);
    if (!bounded_contains(container_query, transform.translation, container_triangles, config, triangle_visits)) {
        return false;
    }
    throw_if_cancelled(cancellation_requested);
    const bool contained = bounded_distance_to_surface(container_query, transform.translation, container_triangles,
                                                       config, triangle_visits) > radius;
    throw_if_cancelled(cancellation_requested);
    return contained;
}

[[nodiscard]] bool transformed_object_is_contained(const TriangleMesh& centered_object, const Matrix4& matrix,
                                                   const ClosedMeshQuery& container_query,
                                                   const TriangleMesh& container,
                                                   const std::uint64_t container_triangles, const PackingConfig& config,
                                                   std::uint64_t& triangle_visits, std::uint64_t& surface_pair_tests,
                                                   const std::function<bool()>& cancellation_requested)
{
    throw_if_cancelled(cancellation_requested);
    TriangleMesh transformed_object = transform_mesh(centered_object, matrix);
    throw_if_cancelled(cancellation_requested);
    for (const Point3& vertex : transformed_object.vertices) {
        throw_if_cancelled(cancellation_requested);
        if (!bounded_contains(container_query, vertex, container_triangles, config, triangle_visits) ||
            !(bounded_distance_to_surface(container_query, vertex, container_triangles, config, triangle_visits) >
              0.0)) {
            return false;
        }
    }
    if (surface_pair_tests > config.max_surface_intersection_triangle_pairs) {
        throw Error(ErrorCategory::resource_limit, "surface-intersection triangle-pair limit is exhausted");
    }
    throw_if_cancelled(cancellation_requested);
    const SurfaceIntersectionResult intersection = query_surface_intersection(
        transformed_object, container, config.max_surface_intersection_triangle_pairs - surface_pair_tests);
    throw_if_cancelled(cancellation_requested);
    surface_pair_tests += intersection.tested_triangle_pairs;
    return !intersection.intersects;
}

[[nodiscard]] bool is_reference_origin_transform(const Transform& transform) noexcept
{
    return transform.rotation.x == 0.0 && transform.rotation.y == 0.0 && transform.rotation.z == 0.0 &&
           transform.translation.x == 0.0 && transform.translation.y == 0.0 && transform.translation.z == 0.0;
}

void require_centered_object(const TriangleMesh& object, const double radius)
{
    const Point3 centroid = vertex_centroid(object);
    const double tolerance = std::max(1.0, radius) * 64.0 * std::numeric_limits<double>::epsilon();
    if (std::abs(centroid.x) > tolerance || std::abs(centroid.y) > tolerance || std::abs(centroid.z) > tolerance) {
        throw Error(ErrorCategory::invalid_configuration,
                    "object template must be centered at its vertex centroid before initialization");
    }
}

void validate_initial_state_bounded(const TriangleMesh& centered_object, const TriangleMesh& container,
                                    const PackingState& state, std::uint64_t& triangle_visits,
                                    std::uint64_t& pairwise_checks, std::uint64_t& surface_pair_tests,
                                    const std::function<bool()>& cancellation_requested)
{
    validate_packing_config(state.config);
    throw_if_cancelled(cancellation_requested);
    if (state.transforms.size() != static_cast<std::size_t>(state.config.object_count)) {
        throw Error(ErrorCategory::invalid_mesh, "initial state transform count differs from its configuration");
    }
    const ClosedMeshQuery container_query(container);
    throw_if_cancelled(cancellation_requested);
    const std::uint64_t container_triangles = static_cast<std::uint64_t>(container.triangles.size());
    const double radius = maximum_radius(centered_object, {}) * std::cbrt(state.config.initial_volume_scale);
    const double minimum_distance = 2.0 * radius;
    const double minimum_distance_squared = minimum_distance * minimum_distance;

    for (std::size_t index = 0; index < state.transforms.size(); ++index) {
        throw_if_cancelled(cancellation_requested);
        const Transform& transform = state.transforms[index];
        if (transform.volume_scale != state.config.initial_volume_scale) {
            throw Error(ErrorCategory::invalid_mesh, "initial transform has an unexpected volume scale");
        }
        if (!is_finite(transform.rotation) || !is_finite(transform.translation)) {
            throw Error(ErrorCategory::invalid_mesh, "initial transform rotation and translation must be finite");
        }
        const Matrix4 matrix = matrix_for(transform);

        // DEVIATION(IROP-DEV-0008): The Python validator only checks surface
        // intersections. Prefer a bounding-sphere containment proof and fall
        // back to strict transformed-vertex containment for the compatible
        // one-object origin case when its conservative sphere does not fit.
        // See docs/COMPATIBILITY.md.
        bool contained = bounding_sphere_is_contained(transform, container_query, container_triangles, radius,
                                                      state.config, triangle_visits, cancellation_requested);
        if (!contained && state.config.object_count == 1 && is_reference_origin_transform(transform)) {
            contained = transformed_object_is_contained(centered_object, matrix, container_query, container,
                                                        container_triangles, state.config, triangle_visits,
                                                        surface_pair_tests, cancellation_requested);
        }
        if (!contained) {
            throw Error(ErrorCategory::invalid_mesh, "initial object violates container containment or clearance");
        }
        for (std::size_t other = 0; other < index; ++other) {
            throw_if_cancelled(cancellation_requested);
            consume_work(1, state.config.max_pairwise_distance_checks, pairwise_checks,
                         "pairwise-distance-check limit");
            if (!(squared_norm(subtract(transform.translation, state.transforms[other].translation)) >
                  minimum_distance_squared)) {
                throw Error(ErrorCategory::invalid_mesh, "initial object centers violate minimum spacing");
            }
        }
    }
}

}  // namespace

void validate_packing_config(const PackingConfig& config)
{
    if (config.object_count == 0) {
        throw Error(ErrorCategory::invalid_configuration, "packing object count must be positive");
    }
    if (config.object_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw Error(ErrorCategory::resource_limit, "packing object count exceeds addressable memory");
    }
    if (!std::isfinite(config.initial_volume_scale) || config.initial_volume_scale <= 0.0 ||
        config.initial_volume_scale > 1.0) {
        throw Error(ErrorCategory::invalid_configuration,
                    "initial volume scale must be finite and in the interval (0, 1]");
    }
    if (config.max_sampling_attempts == 0 || config.max_sampling_attempts < config.object_count) {
        throw Error(ErrorCategory::invalid_configuration,
                    "maximum sampling attempts must be positive and at least the object count");
    }
    if (config.max_geometry_query_triangle_visits == 0 || config.max_pairwise_distance_checks == 0 ||
        config.max_surface_intersection_triangle_pairs == 0) {
        throw Error(ErrorCategory::invalid_configuration, "initialization work limits must be positive");
    }
    if (config.output_mesh_limits.max_vertices == 0 || config.output_mesh_limits.max_triangles == 0) {
        throw Error(ErrorCategory::invalid_configuration, "output mesh count limits must be positive");
    }
}

DeterministicRandomState::DeterministicRandomState(const std::uint32_t seed) : seed_(seed), engine_(seed) {}

std::uint32_t DeterministicRandomState::seed() const noexcept { return seed_; }

std::uint64_t DeterministicRandomState::draw_count() const noexcept { return draw_count_; }

double DeterministicRandomState::uniform(const double lower, const double upper)
{
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(lower < upper) || !std::isfinite(upper - lower)) {
        throw Error(ErrorCategory::invalid_configuration, "random sampling interval must be finite and increasing");
    }
    if (draw_count_ == std::numeric_limits<std::uint64_t>::max()) {
        throw Error(ErrorCategory::resource_limit, "random draw counter exhausted");
    }

    // DEVIATION(IROP-DEV-0006): The Python path mutates NumPy's process-global
    // RNG. Each C++ run owns this fixed, implementation-independent mapping.
    // See docs/COMPATIBILITY.md.
    const std::uint32_t first = static_cast<std::uint32_t>(engine_() >> 5U);
    const std::uint32_t second = static_cast<std::uint32_t>(engine_() >> 6U);
    ++draw_count_;
    const double unit =
        (static_cast<double>(first) * 67'108'864.0 + static_cast<double>(second)) / 9'007'199'254'740'992.0;
    return lower + (upper - lower) * unit;
}

PackingState::PackingState(PackingConfig configuration) : config(std::move(configuration)), random_state(config.seed) {}

PackingState initialize_packing(const TriangleMesh& centered_object, const TriangleMesh& container,
                                const PackingConfig& config, const std::function<bool()>& cancellation_requested)
{
    validate_packing_config(config);
    throw_if_cancelled(cancellation_requested);
    static_cast<void>(validate_and_measure_mesh(centered_object, config.output_mesh_limits));
    validate_output_size(centered_object, config);
    throw_if_cancelled(cancellation_requested);

    const ClosedMeshQuery object_query(centered_object);
    throw_if_cancelled(cancellation_requested);
    const ClosedMeshQuery container_query(container);
    throw_if_cancelled(cancellation_requested);
    const double full_radius = maximum_radius(centered_object, {});
    require_centered_object(centered_object, full_radius);
    throw_if_cancelled(cancellation_requested);

    PackingState state(config);
    state.object_volume = object_query.volume();
    state.container_volume = container_query.volume();
    state.initial_linear_scale = std::cbrt(config.initial_volume_scale);
    state.object_bounding_radius = full_radius * state.initial_linear_scale;
    state.minimum_center_distance = 2.0 * state.object_bounding_radius;
    if (!std::isfinite(state.object_bounding_radius) || state.object_bounding_radius <= 0.0 ||
        !std::isfinite(state.minimum_center_distance)) {
        throw Error(ErrorCategory::invalid_configuration, "initial object clearance is not representable");
    }
    state.transforms.reserve(static_cast<std::size_t>(config.object_count));
    const std::uint64_t container_triangles = static_cast<std::uint64_t>(container.triangles.size());

    const Transform origin_transform { .volume_scale = config.initial_volume_scale };
    const Matrix4 origin_matrix = matrix_for(origin_transform);
    // COMPATIBILITY(IROP-COMPAT-0004): The live Python setup special-cases a
    // single object at the origin with zero rotation and consumes no RNG draws.
    // Preserve that result whenever the transformed object itself is contained,
    // even when its conservative bounding sphere does not fit.
    // See docs/COMPATIBILITY.md.
    if (config.object_count == 1 &&
        (bounding_sphere_is_contained(origin_transform, container_query, container_triangles,
                                      state.object_bounding_radius, config, state.geometry_query_triangle_visits,
                                      cancellation_requested) ||
         transformed_object_is_contained(centered_object, origin_matrix, container_query, container,
                                         container_triangles, config, state.geometry_query_triangle_visits,
                                         state.surface_intersection_triangle_pairs, cancellation_requested))) {
        state.transforms.push_back(origin_transform);
        validate_initial_state_bounded(centered_object, container, state, state.geometry_query_triangle_visits,
                                       state.pairwise_distance_checks, state.surface_intersection_triangle_pairs,
                                       cancellation_requested);
        return state;
    }
    // DEVIATION(IROP-DEV-0009): If the Python one-object origin shortcut is
    // invalid, use the normal bounded sampler instead of emitting an invalid scene.
    // See docs/COMPATIBILITY.md.

    const MeshBounds& bounds = container_query.bounds();
    const double minimum_distance_squared = state.minimum_center_distance * state.minimum_center_distance;
    if (!std::isfinite(minimum_distance_squared)) {
        throw Error(ErrorCategory::invalid_configuration, "initial center spacing is not representable");
    }

    // DEVIATION(IROP-DEV-0005): The unused and defective Python grid optimizer
    // is omitted; initialization follows the live uniform-rejection path.
    // See docs/COMPATIBILITY.md.
    // DEVIATION(IROP-DEV-0007): The Python rejection loop is unbounded on its
    // normal path. The C++ run stops at an explicit candidate-attempt limit.
    // See docs/COMPATIBILITY.md.
    std::uint64_t attempts = 0;
    while (state.transforms.size() < static_cast<std::size_t>(config.object_count) &&
           attempts < config.max_sampling_attempts) {
        throw_if_cancelled(cancellation_requested);
        ++attempts;
        const Point3 candidate {
            state.random_state.uniform(bounds.minimum.x, bounds.maximum.x),
            state.random_state.uniform(bounds.minimum.y, bounds.maximum.y),
            state.random_state.uniform(bounds.minimum.z, bounds.maximum.z),
        };
        if (!bounded_contains(container_query, candidate, container_triangles, config,
                              state.geometry_query_triangle_visits)) {
            continue;
        }
        if (!is_far_enough_from_centers(candidate, state.transforms, minimum_distance_squared, config,
                                        state.pairwise_distance_checks, cancellation_requested)) {
            continue;
        }
        throw_if_cancelled(cancellation_requested);
        if (!(bounded_distance_to_surface(container_query, candidate, container_triangles, config,
                                          state.geometry_query_triangle_visits) > state.object_bounding_radius)) {
            continue;
        }
        state.transforms.push_back(Transform { .volume_scale = config.initial_volume_scale, .translation = candidate });
    }
    state.rejected_candidate_count = attempts - static_cast<std::uint64_t>(state.transforms.size());
    throw_if_cancelled(cancellation_requested);
    if (state.transforms.size() != static_cast<std::size_t>(config.object_count)) {
        throw Error(ErrorCategory::resource_limit,
                    "initial placement exhausted " + std::to_string(config.max_sampling_attempts) +
                        " candidate attempts after placing " + std::to_string(state.transforms.size()) + " of " +
                        std::to_string(config.object_count) + " objects");
    }

    for (Transform& transform : state.transforms) {
        throw_if_cancelled(cancellation_requested);
        transform.rotation = {
            state.random_state.uniform(-std::numbers::pi_v<double>, std::numbers::pi_v<double>),
            state.random_state.uniform(-std::numbers::pi_v<double>, std::numbers::pi_v<double>),
            state.random_state.uniform(-std::numbers::pi_v<double>, std::numbers::pi_v<double>),
        };
    }

    validate_initial_state_bounded(centered_object, container, state, state.geometry_query_triangle_visits,
                                   state.pairwise_distance_checks, state.surface_intersection_triangle_pairs,
                                   cancellation_requested);
    throw_if_cancelled(cancellation_requested);
    return state;
}

void validate_initial_state(const TriangleMesh& centered_object, const TriangleMesh& container,
                            const PackingState& state, const std::function<bool()>& cancellation_requested)
{
    std::uint64_t triangle_visits = 0;
    std::uint64_t pairwise_checks = 0;
    std::uint64_t surface_pair_tests = 0;
    validate_initial_state_bounded(centered_object, container, state, triangle_visits, pairwise_checks,
                                   surface_pair_tests, cancellation_requested);
}

std::vector<TriangleMesh> instantiate_objects(const TriangleMesh& centered_object, const PackingState& state)
{
    std::vector<TriangleMesh> objects;
    objects.reserve(state.transforms.size());
    for (const Transform& transform : state.transforms) {
        objects.push_back(transform_mesh(centered_object, transform));
    }
    return objects;
}

}  // namespace irop
