#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/packing/initialization.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

[[nodiscard]] irop::TriangleMesh centered_tetrahedron()
{
    return irop::center_mesh_at_vertex_centroid(irop::test::tetrahedron_mesh()).mesh;
}

[[nodiscard]] irop::TriangleMesh centered_octahedron()
{
    return {
        .vertices = {
            { 1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { -1.0, 0.0, 0.0 },
            { 0.0, -1.0, 0.0 },
            { 0.0, 0.0, 1.0 },
            { 0.0, 0.0, -1.0 },
        },
        .triangles = {
            { 4, 0, 1 },
            { 4, 1, 2 },
            { 4, 2, 3 },
            { 4, 3, 0 },
            { 5, 1, 0 },
            { 5, 2, 1 },
            { 5, 3, 2 },
            { 5, 0, 3 },
        },
    };
}

void require_same_transforms(const irop::PackingState& left, const irop::PackingState& right)
{
    REQUIRE(left.transforms.size() == right.transforms.size());
    for (std::size_t index = 0; index < left.transforms.size(); ++index) {
        const irop::Transform& first = left.transforms[index];
        const irop::Transform& second = right.transforms[index];
        CHECK(first.volume_scale == second.volume_scale);
        CHECK(first.rotation.x == second.rotation.x);
        CHECK(first.rotation.y == second.rotation.y);
        CHECK(first.rotation.z == second.rotation.z);
        CHECK(first.translation.x == second.translation.x);
        CHECK(first.translation.y == second.translation.y);
        CHECK(first.translation.z == second.translation.z);
    }
}

TEST_CASE("deterministic random state matches NumPy legacy MT19937 uniform values")
{
    irop::DeterministicRandomState random(0);
    CHECK(random.uniform(0.0, 1.0) == Approx(0.5488135039273248).epsilon(1.0e-15));
    CHECK(random.uniform(0.0, 1.0) == Approx(0.7151893663724195).epsilon(1.0e-15));
    CHECK(random.draw_count() == 2);
}

TEST_CASE("same seed and configuration reproduce initial transforms exactly")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingConfig config;
    config.object_count = 8;
    config.seed = 12345;

    const irop::PackingState first = irop::initialize_packing(object, container, config);
    const irop::PackingState second = irop::initialize_packing(object, container, config);

    require_same_transforms(first, second);
    CHECK(first.random_state.draw_count() == second.random_state.draw_count());
    CHECK(first.rejected_candidate_count == second.rejected_candidate_count);
}

TEST_CASE("seeded initialization matches the Python rejection and rotation golden")
{
    irop::PackingConfig config;
    config.object_count = 8;
    config.seed = 12345;
    const irop::PackingState state =
        irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), config);

    REQUIRE(state.transforms.size() == 8);
    CHECK(state.object_bounding_radius == Approx(0.3848602148049237).epsilon(1.0e-15));
    CHECK(state.rejected_candidate_count == 6);
    CHECK(state.random_state.draw_count() == 66);
    CHECK(state.transforms.front().translation.x == Approx(4.296160928171478).epsilon(1.0e-15));
    CHECK(state.transforms.front().translation.y == Approx(-1.836244454182141).epsilon(1.0e-15));
    CHECK(state.transforms.front().translation.z == Approx(-3.1608118832290555).epsilon(1.0e-15));
    CHECK(state.transforms.front().rotation.x == Approx(1.9489775358353327).epsilon(1.0e-15));
    CHECK(state.transforms.front().rotation.y == Approx(-2.5386046226798467).epsilon(1.0e-15));
    CHECK(state.transforms.front().rotation.z == Approx(-1.7658889558059756).epsilon(1.0e-15));
    CHECK(state.transforms.back().translation.x == Approx(2.282661803271173).epsilon(1.0e-15));
    CHECK(state.transforms.back().translation.y == Approx(3.183500113899145).epsilon(1.0e-15));
    CHECK(state.transforms.back().translation.z == Approx(0.0022275283444823657).epsilon(1.0e-15));
    CHECK(state.transforms.back().rotation.x == Approx(-1.9614971355011437).epsilon(1.0e-15));
    CHECK(state.transforms.back().rotation.y == Approx(-2.3509069143808645).epsilon(1.0e-15));
    CHECK(state.transforms.back().rotation.z == Approx(1.1786992059440777).epsilon(1.0e-15));
}

TEST_CASE("seeded initialization preserves the Python coordinate then rotation draw order")
{
    irop::PackingConfig config;
    config.object_count = 2;
    config.seed = 0;
    const irop::PackingState state =
        irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), config);
    REQUIRE(state.transforms.size() == 2);
    CHECK(state.rejected_candidate_count == 0);
    CHECK(state.random_state.draw_count() == 12);

    CHECK(state.transforms[0].translation.x == Approx(0.48813503927324753).epsilon(1.0e-15));
    CHECK(state.transforms[0].translation.y == Approx(2.151893663724195).epsilon(1.0e-15));
    CHECK(state.transforms[0].translation.z == Approx(1.027633760716439).epsilon(1.0e-15));
    CHECK(state.transforms[1].translation.x == Approx(0.4488318299689684).epsilon(1.0e-15));
    CHECK(state.transforms[1].translation.y == Approx(-0.7634520066109527).epsilon(1.0e-15));
    CHECK(state.transforms[1].translation.z == Approx(1.4589411306665614).epsilon(1.0e-15));
    CHECK(state.transforms[0].rotation.x == Approx(-0.39215111717435397).epsilon(1.0e-15));
    CHECK(state.transforms[0].rotation.y == Approx(2.4615823622636204).epsilon(1.0e-15));
    CHECK(state.transforms[0].rotation.z == Approx(2.9132790442663947).epsilon(1.0e-15));
    CHECK(state.transforms[1].rotation.x == Approx(-0.732358536341042).epsilon(1.0e-15));
    CHECK(state.transforms[1].rotation.y == Approx(1.8329624730174034).epsilon(1.0e-15));
    CHECK(state.transforms[1].rotation.z == Approx(0.18155213524358263).epsilon(1.0e-15));
}

TEST_CASE("packing runs own independent random state")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingConfig first_config;
    first_config.object_count = 5;
    first_config.seed = 17;
    irop::PackingConfig intervening_config = first_config;
    intervening_config.seed = 999;

    const irop::PackingState first = irop::initialize_packing(object, container, first_config);
    static_cast<void>(irop::initialize_packing(object, container, intervening_config));
    const irop::PackingState repeated = irop::initialize_packing(object, container, first_config);

    require_same_transforms(first, repeated);
}

TEST_CASE("different seeds change a general initial placement")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingConfig first_config;
    first_config.object_count = 3;
    first_config.seed = 1;
    irop::PackingConfig second_config = first_config;
    second_config.seed = 2;

    const irop::PackingState first = irop::initialize_packing(object, container, first_config);
    const irop::PackingState second = irop::initialize_packing(object, container, second_config);
    CHECK(first.transforms.front().translation.x != second.transforms.front().translation.x);
}

TEST_CASE("initialized objects satisfy strict containment and bounding-sphere spacing")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    irop::PackingConfig config;
    config.object_count = 12;
    config.seed = 1918;

    const irop::PackingState state = irop::initialize_packing(object, container, config);
    const irop::ClosedMeshQuery container_query(container);
    const std::vector<irop::TriangleMesh> objects = irop::instantiate_objects(object, state);
    REQUIRE(objects.size() == config.object_count);
    for (std::size_t index = 0; index < state.transforms.size(); ++index) {
        const irop::Point3 center = state.transforms[index].translation;
        CHECK(container_query.contains(center));
        CHECK(container_query.distance_to_surface(center) > state.object_bounding_radius);
        for (const irop::Point3& vertex : objects[index].vertices) {
            CHECK(container_query.contains(vertex));
        }
        for (std::size_t other = 0; other < index; ++other) {
            const irop::Point3 delta {
                center.x - state.transforms[other].translation.x,
                center.y - state.transforms[other].translation.y,
                center.z - state.transforms[other].translation.z,
            };
            CHECK(std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) > state.minimum_center_distance);
        }
    }
}

TEST_CASE("single object preserves the valid origin and zero-rotation reference case")
{
    const irop::PackingState state = irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), {});
    REQUIRE(state.transforms.size() == 1);
    CHECK(state.transforms[0].translation.x == 0.0);
    CHECK(state.transforms[0].translation.y == 0.0);
    CHECK(state.transforms[0].translation.z == 0.0);
    CHECK(state.transforms[0].rotation.x == 0.0);
    CHECK(state.transforms[0].rotation.y == 0.0);
    CHECK(state.transforms[0].rotation.z == 0.0);
    CHECK(state.random_state.draw_count() == 0);
}

TEST_CASE("single object preserves a contained origin even when its bounding sphere does not fit")
{
    irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    for (irop::Point3& vertex : object.vertices) {
        vertex.x *= 2.0;
        vertex.y *= 1.2;
        vertex.z *= 1.2;
    }
    const irop::PackingState state = irop::initialize_packing(object, irop::test::cube_mesh(1.0), {});
    REQUIRE(state.transforms.size() == 1);
    CHECK(state.object_bounding_radius > 1.0);
    CHECK(state.transforms.front().translation.x == 0.0);
    CHECK(state.transforms.front().rotation.x == 0.0);
    CHECK(state.random_state.draw_count() == 0);
    CHECK(state.surface_intersection_triangle_pairs > 0);
}

TEST_CASE("single object falls back to bounded sampling when the origin is invalid")
{
    irop::TriangleMesh shifted_container = irop::test::cube_mesh(5.0);
    for (irop::Point3& vertex : shifted_container.vertices) {
        vertex.x += 10.0;
    }
    irop::PackingConfig config;
    config.seed = 7;
    const irop::PackingState state = irop::initialize_packing(centered_tetrahedron(), shifted_container, config);
    REQUIRE(state.transforms.size() == 1);
    CHECK(state.transforms[0].translation.x > 5.0);
    CHECK(state.random_state.draw_count() >= 6);
}

TEST_CASE("initialization stops at the configured placement-attempt limit")
{
    irop::PackingConfig config;
    config.object_count = 2;
    config.initial_volume_scale = 1.0;
    config.max_sampling_attempts = 2;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(irop::test::cube_mesh(2.0), irop::test::cube_mesh(1.0), config));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("initialization observes cooperative cancellation during bounded work")
{
    irop::PackingConfig config;
    config.object_count = 2;
    std::uint64_t polls = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(
            irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), config, [&polls]() {
            ++polls;
            return polls > 5;
        }));
    }, irop::ErrorCategory::cancelled);
    CHECK(polls > 5);
}

TEST_CASE("cancellation takes precedence when the final placement attempt is rejected")
{
    irop::PackingConfig config;
    config.object_count = 2;
    config.initial_volume_scale = 1.0;
    config.max_sampling_attempts = 2;
    std::uint64_t polls = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(
            irop::initialize_packing(irop::test::cube_mesh(2.0), irop::test::cube_mesh(1.0), config, [&polls]() {
            ++polls;
            return polls >= 10;
        }));
    }, irop::ErrorCategory::cancelled);
    CHECK(polls >= 10);
}

TEST_CASE("initialization stops at the geometry-query work limit")
{
    irop::PackingConfig config;
    config.object_count = 2;
    config.max_geometry_query_triangle_visits = 11;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), config));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("initialization stops at the pairwise-distance work limit")
{
    irop::PackingConfig config;
    config.object_count = 3;
    config.seed = 0;
    config.max_pairwise_distance_checks = 1;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(50.0), config));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("one-object exact containment stops at the surface-pair work limit")
{
    irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    for (irop::Point3& vertex : object.vertices) {
        vertex.x *= 2.0;
        vertex.y *= 1.2;
        vertex.z *= 1.2;
    }
    irop::PackingConfig config;
    config.max_surface_intersection_triangle_pairs = 1;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(object, irop::test::cube_mesh(1.0), config));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("initialization rejects invalid configuration before sampling")
{
    irop::PackingConfig config;
    config.object_count = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(centered_tetrahedron(), irop::test::cube_mesh(5.0), config));
    }, irop::ErrorCategory::invalid_configuration);
}

TEST_CASE("initial-state validation rejects exact-threshold center spacing")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    irop::PackingConfig config;
    config.object_count = 2;
    irop::PackingState state(config);
    const double radius = irop::maximum_radius(object, {}) * std::cbrt(config.initial_volume_scale);
    state.transforms = {
        { .volume_scale = config.initial_volume_scale, .translation = { 0.0, 0.0, 0.0 }          },
        { .volume_scale = config.initial_volume_scale, .translation = { 2.0 * radius, 0.0, 0.0 } },
    };
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, irop::test::cube_mesh(5.0), state);
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("initial-state validation rejects exact-threshold boundary clearance")
{
    const irop::TriangleMesh object = centered_octahedron();
    irop::PackingConfig config;
    config.initial_volume_scale = 1.0;
    irop::PackingState state(config);
    const double radius = irop::maximum_radius(object, {});
    REQUIRE(radius == 1.0);
    state.transforms = {
        { .volume_scale = config.initial_volume_scale, .translation = { 5.0 - radius, 0.0, 0.0 } },
    };
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, irop::test::cube_mesh(5.0), state);
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("initial-state validation rejects non-finite rotations")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    irop::PackingConfig config;
    irop::PackingState state(config);
    state.transforms = {
        { .volume_scale = config.initial_volume_scale,
         .rotation = { std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 } },
    };
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, irop::test::cube_mesh(5.0), state);
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("initial-state validation rejects a wholly outside nonintersecting object")
{
    const irop::TriangleMesh object = centered_tetrahedron();
    irop::PackingConfig config;
    irop::PackingState state(config);
    state.transforms = {
        { .volume_scale = config.initial_volume_scale, .translation = { 100.0, 100.0, 100.0 } },
    };
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, irop::test::cube_mesh(5.0), state);
    }, irop::ErrorCategory::invalid_mesh);
}

}  // namespace
