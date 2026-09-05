#include "irop/packing/initialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
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

[[nodiscard]] MeshBounds transformed_bounds(const TriangleMesh& object, const Matrix4& matrix,
                                            const std::function<bool()>& cancellation_requested)
{
    if (object.vertices.empty()) {
        throw Error(ErrorCategory::invalid_mesh, "structured initialization requires object vertices");
    }
    const Point3 first = transform_point(matrix, object.vertices.front());
    MeshBounds bounds { first, first };
    for (const Point3& vertex : object.vertices) {
        throw_if_cancelled(cancellation_requested);
        const Point3 point = transform_point(matrix, vertex);
        bounds.minimum.x = std::min(bounds.minimum.x, point.x);
        bounds.minimum.y = std::min(bounds.minimum.y, point.y);
        bounds.minimum.z = std::min(bounds.minimum.z, point.z);
        bounds.maximum.x = std::max(bounds.maximum.x, point.x);
        bounds.maximum.y = std::max(bounds.maximum.y, point.y);
        bounds.maximum.z = std::max(bounds.maximum.z, point.z);
    }
    return bounds;
}

[[nodiscard]] bool strictly_separated(const MeshBounds& first, const MeshBounds& second) noexcept
{
    return first.maximum.x < second.minimum.x || second.maximum.x < first.minimum.x ||
           first.maximum.y < second.minimum.y || second.maximum.y < first.minimum.y ||
           first.maximum.z < second.minimum.z || second.maximum.z < first.minimum.z;
}

struct StructuredAxis {
    std::uint64_t count = 0;
    double first_translation = 0.0;
    double pitch = 0.0;
};

[[nodiscard]] StructuredAxis structured_axis(const double container_min, const double container_max,
                                             const double object_min, const double object_max,
                                             const std::uint64_t count_limit)
{
    const double width = container_max - container_min;
    const double extent = object_max - object_min;
    // Leave more than float32 rounding error between envelopes and the AABB
    // boundary. This is a numerical margin in input units, not a fit tolerance.
    const double magnitude = std::max({ std::abs(container_min), std::abs(container_max), extent });
    const double margin = magnitude * (8.0 * std::numeric_limits<float>::epsilon());
    const double pitch = extent + margin;
    const double available = width - margin;
    if (!std::isfinite(width) || !std::isfinite(extent) || !std::isfinite(pitch) || !std::isfinite(available) ||
        extent <= 0.0 || margin <= 0.0 || !(pitch > extent) || !(available < width) || available < pitch) {
        return {};
    }
    const double quotient = std::floor(available / pitch);
    // Clamp in floating point before converting; a huge finite container/tiny
    // object ratio can exceed uint64_t even though we only need a few cells.
    const std::uint64_t count =
        quotient >= static_cast<double>(count_limit) ? count_limit : static_cast<std::uint64_t>(quotient);
    if (count == 0) {
        return {};
    }
    const double occupied = static_cast<double>(count) * pitch - margin;
    const double first = std::midpoint(container_min, container_max) - occupied / 2.0 - object_min;
    if (!std::isfinite(occupied) || !std::isfinite(first) || !(occupied < width)) {
        return {};
    }
    return { count, first, pitch };
}

[[nodiscard]] bool structured_capacity_suffices(const std::array<StructuredAxis, 3>& axes,
                                                const std::uint64_t required) noexcept
{
    std::uint64_t capacity = 1;
    for (const StructuredAxis& axis : axes) {
        if (axis.count == 0) {
            return false;
        }
        capacity = axis.count > required / capacity ? required : std::min(required, capacity * axis.count);
    }
    return capacity >= required;
}

[[nodiscard]] bool initialize_structured(const TriangleMesh& object, const TriangleMesh& container,
                                         const ClosedMeshQuery& container_query, PackingState& state,
                                         const std::function<bool()>& cancellation_requested)
{
    // DEVIATION(IROP-DEV-0026): Only an exhausted reference candidate search
    // triggers this bounded deterministic restart. Its partial prefix and RNG
    // consumption remain reported; successful reference placements are unchanged.
    // See docs/COMPATIBILITY.md.
    constexpr double half_pi = std::numbers::pi_v<double> / 2.0;
    constexpr std::array<EulerRotationRadians, 6> orientations {
        EulerRotationRadians {},
        EulerRotationRadians { .y = half_pi },
        EulerRotationRadians { .x = half_pi },
        EulerRotationRadians { .z = half_pi },
        EulerRotationRadians { .x = half_pi, .z = half_pi },
        EulerRotationRadians { .x = half_pi, .y = half_pi },
    };
    const PackingConfig& config = state.config;
    const MeshBounds& container_bounds = container_query.bounds();
    const std::uint64_t container_triangles = static_cast<std::uint64_t>(container.triangles.size());
    const std::uint64_t count_limit = std::min(config.object_count, config.max_structured_candidates);
    std::vector<Transform> candidates;
    std::vector<MeshBounds> accepted_bounds;
    candidates.reserve(static_cast<std::size_t>(config.object_count));
    accepted_bounds.reserve(static_cast<std::size_t>(config.object_count));
    for (const EulerRotationRadians& rotation : orientations) {
        throw_if_cancelled(cancellation_requested);
        ++state.orientations_examined;
        candidates.clear();
        accepted_bounds.clear();
        Transform candidate { .volume_scale = config.initial_volume_scale, .rotation = rotation };
        const MeshBounds bounds = transformed_bounds(object, matrix_for(candidate), cancellation_requested);
        const std::array<StructuredAxis, 3> axes {
            structured_axis(container_bounds.minimum.x, container_bounds.maximum.x, bounds.minimum.x, bounds.maximum.x,
                            count_limit),
            structured_axis(container_bounds.minimum.y, container_bounds.maximum.y, bounds.minimum.y, bounds.maximum.y,
                            count_limit),
            structured_axis(container_bounds.minimum.z, container_bounds.maximum.z, bounds.minimum.z, bounds.maximum.z,
                            count_limit),
        };
        if (!structured_capacity_suffices(axes, config.object_count)) {
            continue;
        }
        for (std::uint64_t z = 0; z < axes[2].count; ++z) {
            for (std::uint64_t y = 0; y < axes[1].count; ++y) {
                for (std::uint64_t x = 0; x < axes[0].count; ++x) {
                    throw_if_cancelled(cancellation_requested);
                    consume_work(1, config.max_structured_candidates, state.structured_candidates,
                                 "structured-candidate limit");
                    candidate.translation = {
                        axes[0].first_translation + static_cast<double>(x) * axes[0].pitch,
                        axes[1].first_translation + static_cast<double>(y) * axes[1].pitch,
                        axes[2].first_translation + static_cast<double>(z) * axes[2].pitch,
                    };
                    const Matrix4 matrix = matrix_for(candidate);
                    const MeshBounds actual = transformed_bounds(object, matrix, cancellation_requested);
                    bool separated = true;
                    for (const MeshBounds& previous : accepted_bounds) {
                        throw_if_cancelled(cancellation_requested);
                        consume_work(1, config.max_pairwise_distance_checks, state.pairwise_distance_checks,
                                     "pairwise-distance-check limit");
                        if (!strictly_separated(actual, previous)) {
                            separated = false;
                            break;
                        }
                    }
                    if (!separated || !transformed_object_is_contained(
                                          object, matrix, container_query, container, container_triangles, config,
                                          state.geometry_query_triangle_visits,
                                          state.surface_intersection_triangle_pairs, cancellation_requested)) {
                        continue;
                    }
                    candidates.push_back(candidate);
                    accepted_bounds.push_back(actual);
                    if (candidates.size() == static_cast<std::size_t>(config.object_count)) {
                        state.transforms = std::move(candidates);
                        state.initialization_method = InitializationMethod::structured_grid;
                        return true;
                    }
                }
            }
        }
    }
    throw_if_cancelled(cancellation_requested);
    return false;
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
    if (state.initialization_method != InitializationMethod::random_rejection &&
        state.initialization_method != InitializationMethod::reference_origin &&
        state.initialization_method != InitializationMethod::structured_grid) {
        throw Error(ErrorCategory::invalid_configuration, "initial state has an unknown initialization method");
    }
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

    std::vector<MeshBounds> structured_bounds;
    if (state.initialization_method == InitializationMethod::structured_grid) {
        // Recompute envelopes from the supplied geometry/transforms. Method
        // metadata selects the proof, but never substitutes for physical checks.
        static_cast<void>(ClosedMeshQuery(centered_object));
        structured_bounds.reserve(state.transforms.size());
    }

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

        if (state.initialization_method == InitializationMethod::structured_grid) {
            const MeshBounds actual = transformed_bounds(centered_object, matrix, cancellation_requested);
            for (const MeshBounds& previous : structured_bounds) {
                throw_if_cancelled(cancellation_requested);
                consume_work(1, state.config.max_pairwise_distance_checks, pairwise_checks,
                             "pairwise-distance-check limit");
                if (!strictly_separated(actual, previous)) {
                    throw Error(ErrorCategory::invalid_mesh, "structured initial object envelopes overlap or touch");
                }
            }
            if (!transformed_object_is_contained(centered_object, matrix, container_query, container,
                                                 container_triangles, state.config, triangle_visits, surface_pair_tests,
                                                 cancellation_requested)) {
                throw Error(ErrorCategory::invalid_mesh, "structured initial object violates strict containment");
            }
            structured_bounds.push_back(actual);
            continue;
        }

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
    if (config.max_structured_candidates == 0 || config.max_geometry_query_triangle_visits == 0 ||
        config.max_pairwise_distance_checks == 0 || config.max_surface_intersection_triangle_pairs == 0) {
        throw Error(ErrorCategory::invalid_configuration, "initialization work limits must be positive");
    }
    if (config.output_mesh_limits.max_vertices == 0 || config.output_mesh_limits.max_triangles == 0) {
        throw Error(ErrorCategory::invalid_configuration, "output mesh count limits must be positive");
    }
}

const char* to_string(const InitializationMethod method) noexcept
{
    switch (method) {
    case InitializationMethod::random_rejection:
        return "random_rejection";
    case InitializationMethod::reference_origin:
        return "reference_origin";
    case InitializationMethod::structured_grid:
        return "structured_grid";
    }
    return "unknown";
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
        state.initialization_method = InitializationMethod::reference_origin;
        state.reference_accepted_count = 1;
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
    state.sampling_attempts = attempts;
    state.reference_accepted_count = static_cast<std::uint64_t>(state.transforms.size());
    state.rejected_candidate_count = attempts - state.reference_accepted_count;
    throw_if_cancelled(cancellation_requested);
    if (state.transforms.size() != static_cast<std::size_t>(config.object_count)) {
        if (config.enable_structured_fallback &&
            initialize_structured(centered_object, container, container_query, state, cancellation_requested)) {
            validate_initial_state_bounded(centered_object, container, state, state.geometry_query_triangle_visits,
                                           state.pairwise_distance_checks, state.surface_intersection_triangle_pairs,
                                           cancellation_requested);
            throw_if_cancelled(cancellation_requested);
            return state;
        }
        throw Error(ErrorCategory::resource_limit,
                    "initial placement exhausted " + std::to_string(config.max_sampling_attempts) +
                        " candidate attempts after placing " + std::to_string(state.reference_accepted_count) + " of " +
                        std::to_string(config.object_count) + " objects" +
                        (config.enable_structured_fallback ? "; bounded structured search also found no layout" : ""));
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
