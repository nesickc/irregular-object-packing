#include "irop/geometry/collision.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/model/mesh_validation.hpp"
#include "mesh_geometry_internal.hpp"

namespace irop {
namespace {

[[nodiscard]] MeshLimits structural_limits_for(const TriangleMesh& mesh) noexcept
{
    return {
        .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_vertices = static_cast<std::uint64_t>(mesh.vertices.size()),
        .max_triangles = static_cast<std::uint64_t>(mesh.triangles.size()),
    };
}

void validate_limits(const SceneCollisionLimits& limits)
{
    if (limits.max_object_pair_checks == 0 || limits.max_triangle_pair_tests == 0 ||
        limits.max_containment_triangle_visits == 0 || limits.max_reported_violations == 0) {
        throw Error(ErrorCategory::invalid_configuration, "scene-collision limits must be positive");
    }
}

void consume_containment_visits(const TriangleMesh& queried_surface, const SceneCollisionLimits& limits,
                                SceneCollisionWork& work)
{
    const std::uint64_t visits = static_cast<std::uint64_t>(queried_surface.triangles.size());
    if (work.containment_triangle_visits > limits.max_containment_triangle_visits ||
        visits > limits.max_containment_triangle_visits - work.containment_triangle_visits) {
        throw Error(ErrorCategory::resource_limit,
                    "scene collision validation exhausted the containment triangle-visit limit");
    }
    work.containment_triangle_visits += visits;
}

[[nodiscard]] bool bounded_contains(const ClosedMeshQuery& query, const TriangleMesh& queried_surface,
                                    const Point3& point, const SceneCollisionLimits& limits, SceneCollisionWork& work)
{
    consume_containment_visits(queried_surface, limits, work);
    return query.contains(point);
}

[[nodiscard]] bool bounded_surface_intersection(const TriangleMesh& first, const TriangleMesh& second,
                                                const SceneCollisionLimits& limits, SceneCollisionWork& work)
{
    if (work.triangle_pairs_tested >= limits.max_triangle_pair_tests) {
        throw Error(ErrorCategory::resource_limit,
                    "scene collision validation exhausted the surface triangle-pair limit");
    }
    const std::uint64_t remaining = limits.max_triangle_pair_tests - work.triangle_pairs_tested;
    // validate_scene_collisions_impl() validates every supplied mesh once
    // before reaching this pairwise hot path. Reusing the internal primitive
    // keeps validation O(total mesh size), rather than repeating structural
    // scans for every surface comparison.
    const SurfaceIntersectionResult intersection =
        detail::query_validated_surface_intersection(first, second, remaining);
    if (intersection.tested_triangle_pairs > remaining) {
        throw Error(ErrorCategory::internal, "surface intersection exceeded its supplied work limit");
    }
    work.triangle_pairs_tested += intersection.tested_triangle_pairs;
    return intersection.intersects;
}

[[nodiscard]] bool bounds_are_disjoint(const MeshBounds& first, const MeshBounds& second) noexcept
{
    // Bounds come from the validated closed-mesh snapshots. Strict comparisons
    // retain face, edge, and vertex contacts for the exact narrow phase, while
    // also retaining all possible nesting when the surfaces do not intersect.
    return first.maximum.x < second.minimum.x || second.maximum.x < first.minimum.x ||
           first.maximum.y < second.minimum.y || second.maximum.y < first.minimum.y ||
           first.maximum.z < second.minimum.z || second.maximum.z < first.minimum.z;
}

// DEVIATION(IROP-DEV-0027): Bound enumerated pairs even when the broad phase
// skips all narrow-phase geometry work. See docs/COMPATIBILITY.md.
void increment_object_pairs(const SceneCollisionLimits& limits, SceneCollisionWork& work)
{
    // Broad-phase rejections still consume pair work: separated populations
    // must not bypass the bound on the quadratic enumeration itself.
    if (work.object_pairs_examined >= limits.max_object_pair_checks) {
        throw Error(ErrorCategory::resource_limit, "scene collision validation exhausted the object-pair check limit");
    }
    ++work.object_pairs_examined;
}

void record_violation(std::uint64_t& reported_violation_count, const SceneCollisionLimits& limits)
{
    if (reported_violation_count >= limits.max_reported_violations) {
        throw Error(ErrorCategory::resource_limit, "scene collision validation exhausted the reported-violation limit");
    }
    ++reported_violation_count;
}

[[nodiscard]] bool objects_overlap_without_surface_contact(const TriangleMesh& first,
                                                           const ClosedMeshQuery& first_query,
                                                           const TriangleMesh& second,
                                                           const ClosedMeshQuery& second_query,
                                                           const SceneCollisionLimits& limits, SceneCollisionWork& work)
{
    // DEVIATION(IROP-DEV-0019): Python reports only tolerance-based surface
    // contacts. Treat disjoint nested solids as collisions for physical output
    // validity, using the bounded exact project geometry predicate.
    // See docs/COMPATIBILITY.md.
    // Both inputs are connected closed surfaces. Once their surfaces are known
    // to be disjoint, one representative point per direction is sufficient to
    // detect either solid being wholly nested in the other.
    if (bounded_contains(first_query, first, second.vertices.front(), limits, work)) {
        return true;
    }
    return bounded_contains(second_query, second, first.vertices.front(), limits, work);
}

[[nodiscard]] SceneCollisionReport validate_scene_collisions_impl(const std::span<const TriangleMesh> objects,
                                                                  const TriangleMesh& container,
                                                                  const std::span<const TriangleMesh> cat_surfaces,
                                                                  const SceneCollisionLimits& limits)
{
    validate_limits(limits);
    if (objects.empty()) {
        throw Error(ErrorCategory::invalid_configuration, "scene collision validation requires at least one object");
    }
    if (!cat_surfaces.empty() && cat_surfaces.size() != objects.size()) {
        throw Error(ErrorCategory::invalid_configuration,
                    "scene collision CAT surface count must be zero or equal the object count");
    }

    const ClosedMeshQuery container_query(container);
    std::vector<ClosedMeshQuery> object_queries;
    object_queries.reserve(objects.size());
    for (const TriangleMesh& object : objects) {
        object_queries.emplace_back(object);
    }
    for (const TriangleMesh& cat_surface : cat_surfaces) {
        // CAT cells are diagnostic polygon soups and need not be closed, but
        // their triangulated representation must still be structurally safe.
        if (cat_surface.vertices.empty() && cat_surface.triangles.empty()) {
            continue;
        }
        static_cast<void>(validate_and_measure_mesh(cat_surface, structural_limits_for(cat_surface)));
    }

    SceneCollisionReport report;
    std::uint64_t reported_violation_count = 0;

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const bool has_cat_surface = !cat_surfaces.empty() && !cat_surfaces[object_index].vertices.empty() &&
                                     !cat_surfaces[object_index].triangles.empty();
        if (has_cat_surface &&
            bounded_surface_intersection(objects[object_index], cat_surfaces[object_index], limits, report.work)) {
            record_violation(reported_violation_count, limits);
            report.cat_violation_object_ids.push_back(static_cast<std::uint64_t>(object_index));
        }

        // DEVIATION(IROP-DEV-0019): A wholly outside or container-enclosing
        // object is invalid even when no two surface triangles intersect.
        // See docs/COMPATIBILITY.md.
        bool container_violation = bounded_surface_intersection(objects[object_index], container, limits, report.work);
        if (!container_violation) {
            container_violation = !bounded_contains(container_query, container, objects[object_index].vertices.front(),
                                                    limits, report.work);
        }
        if (container_violation) {
            record_violation(reported_violation_count, limits);
            report.container_violation_object_ids.push_back(static_cast<std::uint64_t>(object_index));
        }
    }

    for (std::size_t first = 0; first < objects.size(); ++first) {
        for (std::size_t second = first + 1; second < objects.size(); ++second) {
            increment_object_pairs(limits, report.work);
            if (bounds_are_disjoint(object_queries[first].bounds(), object_queries[second].bounds())) {
                continue;
            }
            bool collision = bounded_surface_intersection(objects[first], objects[second], limits, report.work);
            if (!collision) {
                collision =
                    objects_overlap_without_surface_contact(objects[first], object_queries[first], objects[second],
                                                            object_queries[second], limits, report.work);
            }
            if (collision) {
                record_violation(reported_violation_count, limits);
                report.object_collisions.push_back(
                    { static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(second) });
            }
        }
    }
    return report;
}

}  // namespace

SceneCollisionReport validate_scene_collisions(const std::span<const TriangleMesh> objects,
                                               const TriangleMesh& container,
                                               const std::span<const TriangleMesh> cat_surfaces,
                                               const SceneCollisionLimits& limits)
{
    try {
        return validate_scene_collisions_impl(objects, container, cat_surfaces, limits);
    }
    catch (const Error&) {
        throw;
    }
    catch (const std::bad_alloc&) {
        throw Error(ErrorCategory::resource_limit, "scene collision validation could not allocate bounded work");
    }
    catch (const std::length_error&) {
        throw Error(ErrorCategory::resource_limit, "scene collision validation exceeds addressable memory");
    }
    catch (const std::exception&) {
        throw Error(ErrorCategory::internal, "scene collision validation failed unexpectedly");
    }
    catch (...) {
        throw Error(ErrorCategory::internal, "scene collision validation failed with an unknown error");
    }
}

}  // namespace irop
