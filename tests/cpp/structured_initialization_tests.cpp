#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/collision.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/packing/initialization.hpp"
#include "support/test_support.hpp"

namespace {

[[nodiscard]] irop::TriangleMesh dense_cylinder()
{
    return irop::center_mesh_at_vertex_centroid(irop::test::cylinder_mesh(45.23, 52.7535, 12)).mesh;
}

[[nodiscard]] irop::PackingConfig dense_config(const std::uint64_t count)
{
    irop::PackingConfig config;
    config.object_count = count;
    config.initial_volume_scale = 1.0;
    config.max_sampling_attempts = count;
    return config;
}

void require_same_transforms(const irop::PackingState& first, const irop::PackingState& second)
{
    REQUIRE(first.transforms.size() == second.transforms.size());
    for (std::size_t index = 0; index < first.transforms.size(); ++index) {
        CHECK(irop::matrix_for(first.transforms[index]).values == irop::matrix_for(second.transforms[index]).values);
    }
}

[[nodiscard]] irop::TriangleMesh concave_container()
{
    // Extruded L: two 2.2-unit-wide arms in a 6 by 6 envelope.
    constexpr std::array<std::array<double, 2>, 6> polygon {
        std::array<double, 2> { 0.0, 0.0 },
          { 6.0, 0.0 },
          { 6.0, 2.2 },
          { 2.2, 2.2 },
          { 2.2, 6.0 },
          { 0.0, 6.0 },
    };
    irop::TriangleMesh mesh;
    for (const double z : { -2.0, 2.0 }) {
        for (const auto& point : polygon) {
            mesh.vertices.push_back({ point[0], point[1], z });
        }
    }
    constexpr std::array<irop::Triangle, 4> cap {
        irop::Triangle { 0, 1, 2 },
        { 0, 2, 3 },
        { 0, 3, 5 },
        { 3, 4, 5 },
    };
    for (const auto& triangle : cap) {
        mesh.triangles.push_back({ triangle[0], triangle[2], triangle[1] });
        mesh.triangles.push_back({ triangle[0] + 6, triangle[1] + 6, triangle[2] + 6 });
    }
    for (irop::MeshIndex index = 0; index < 6; ++index) {
        const irop::MeshIndex next = (index + 1) % 6;
        mesh.triangles.push_back({ index, next, next + 6 });
        mesh.triangles.push_back({ index, next + 6, index + 6 });
    }
    return mesh;
}

TEST_CASE("structured fallback places ten and 36 full-size cylinders with strict physical validity",
          "[initialization][structured][integration]")
{
    const irop::TriangleMesh object = dense_cylinder();
    const irop::TriangleMesh container = irop::test::box_mesh(175.0, 200.0, 142.5);
    for (const std::uint64_t count : { 10ULL, 36ULL }) {
        CAPTURE(count);
        const irop::PackingConfig config = dense_config(count);
        const irop::PackingState state = irop::initialize_packing(object, container, config);
        REQUIRE(state.initialization_method == irop::InitializationMethod::structured_grid);
        REQUIRE(state.transforms.size() == count);
        CHECK(state.sampling_attempts == config.max_sampling_attempts);
        CHECK(state.reference_accepted_count < count);
        CHECK(state.rejected_candidate_count == state.sampling_attempts - state.reference_accepted_count);
        CHECK(state.random_state.draw_count() == 3 * state.sampling_attempts);
        CHECK(state.structured_candidates == count);
        CHECK(state.orientations_examined == (count == 10 ? 1 : 2));
        CHECK(state.surface_intersection_triangle_pairs > 0);
        irop::validate_initial_state(object, container, state);
        auto objects = irop::instantiate_objects(object, state);
        CHECK(irop::validate_scene_collisions(objects, container).physical_scene_valid());
        for (auto& mesh : objects) {
            for (auto& point : mesh.vertices) {
                point = { static_cast<double>(static_cast<float>(point.x)),
                          static_cast<double>(static_cast<float>(point.y)),
                          static_cast<double>(static_cast<float>(point.z)) };
            }
        }
        CHECK(irop::validate_scene_collisions(objects, container).physical_scene_valid());
        const irop::PackingState repeated = irop::initialize_packing(object, container, config);
        require_same_transforms(state, repeated);
    }
}

TEST_CASE("fallback preserves successful reference transforms RNG and work exactly",
          "[initialization][structured][compatibility]")
{
    const irop::TriangleMesh object = irop::center_mesh_at_vertex_centroid(irop::test::tetrahedron_mesh()).mesh;
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    for (const std::uint64_t count : { 1ULL, 8ULL }) {
        irop::PackingConfig enabled;
        enabled.object_count = count;
        enabled.seed = 12345;
        irop::PackingConfig disabled = enabled;
        disabled.enable_structured_fallback = false;
        const auto first = irop::initialize_packing(object, container, enabled);
        const auto second = irop::initialize_packing(object, container, disabled);
        require_same_transforms(first, second);
        CHECK(first.random_state.draw_count() == second.random_state.draw_count());
        CHECK(first.rejected_candidate_count == second.rejected_candidate_count);
        CHECK(first.geometry_query_triangle_visits == second.geometry_query_triangle_visits);
        CHECK(first.pairwise_distance_checks == second.pairwise_distance_checks);
        CHECK(first.surface_intersection_triangle_pairs == second.surface_intersection_triangle_pairs);
        CHECK(first.structured_candidates == 0);
        CHECK(first.orientations_examined == 0);
        CHECK(first.initialization_method == (count == 1 ? irop::InitializationMethod::reference_origin
                                                         : irop::InitializationMethod::random_rejection));
    }
}

TEST_CASE("structured fallback respects shifted coordinates and a concave container",
          "[initialization][structured][geometry]")
{
    SECTION("shifted container")
    {
        auto container = irop::test::box_mesh(175.0, 200.0, 142.5);
        for (auto& point : container.vertices) {
            point.x += 10000.0;
            point.y -= 20000.0;
            point.z += 30000.0;
        }
        const auto object = dense_cylinder();
        const auto state = irop::initialize_packing(object, container, dense_config(36));
        CHECK(state.initialization_method == irop::InitializationMethod::structured_grid);
        CHECK(irop::validate_scene_collisions(irop::instantiate_objects(object, state), container)
                  .physical_scene_valid());
    }
    SECTION("L-shaped solid")
    {
        const auto object = irop::test::cube_mesh(0.9);
        const auto container = concave_container();
        const auto state = irop::initialize_packing(object, container, dense_config(10));
        CHECK(state.initialization_method == irop::InitializationMethod::structured_grid);
        CHECK(irop::validate_scene_collisions(irop::instantiate_objects(object, state), container)
                  .physical_scene_valid());
        CHECK(state.structured_candidates > state.transforms.size());
    }
}

TEST_CASE("structured validation never trusts method metadata as a physical proof",
          "[initialization][structured][security]")
{
    const auto object = dense_cylinder();
    const auto container = irop::test::box_mesh(175.0, 200.0, 142.5);
    auto state = irop::initialize_packing(object, container, dense_config(10));
    SECTION("mutated objects overlap") { state.transforms[1] = state.transforms[0]; }
    SECTION("mutated object lies wholly outside") { state.transforms[0].translation = { 10000.0, 10000.0, 10000.0 }; }
    SECTION("mutated rotation is non-finite")
    {
        state.transforms[0].rotation.x = std::numeric_limits<double>::infinity();
    }
    SECTION("forged clearance metadata cannot suppress checks")
    {
        state.object_bounding_radius = 0.0;
        state.minimum_center_distance = 0.0;
        state.transforms[1] = state.transforms[0];
    }
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, container, state);
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("structured validation rejects a surface crossing a concavity with contained vertices",
          "[initialization][structured][geometry][security]")
{
    const auto container = concave_container();
    const auto object = irop::test::box_mesh(2.8, 0.1, 0.2);
    irop::PackingState state(dense_config(1));
    state.initialization_method = irop::InitializationMethod::structured_grid;
    state.transforms = {
        { .volume_scale = 1.0,
         .rotation = { .z = -std::numbers::pi_v<double> / 4.0 },
         .translation = { 3.0, 3.0, 0.0 } }
    };
    const auto transformed = irop::transform_mesh(object, state.transforms.front());
    const irop::ClosedMeshQuery query(container);
    for (const auto& point : transformed.vertices) {
        REQUIRE(query.contains(point));
    }
    irop::test::require_error_category([&]() {
        irop::validate_initial_state(object, container, state);
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("structured fallback has separate candidate limits and shared geometry and pair work",
          "[initialization][structured][limits]")
{
    const auto object = dense_cylinder();
    const auto container = irop::test::box_mesh(175.0, 200.0, 142.5);
    auto config = dense_config(10);
    SECTION("disabled fallback preserves bounded reference failure") { config.enable_structured_fallback = false; }
    SECTION("structured candidate limit") { config.max_structured_candidates = 9; }
    SECTION("surface limit") { config.max_surface_intersection_triangle_pairs = 1; }
    SECTION("geometry query limit") { config.max_geometry_query_triangle_visits = 12; }
    SECTION("pair limit shared with the random prefix") { config.max_pairwise_distance_checks = 1; }
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(object, container, config));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("structured initialization validates limits methods and unsafe mesh values",
          "[initialization][structured][security]")
{
    auto object = dense_cylinder();
    const auto container = irop::test::box_mesh(175.0, 200.0, 142.5);
    auto config = dense_config(10);
    SECTION("zero structured limit")
    {
        config.max_structured_candidates = 0;
        irop::test::require_error_category([&]() {
            irop::validate_packing_config(config);
        }, irop::ErrorCategory::invalid_configuration);
    }
    SECTION("unknown method")
    {
        auto state = irop::initialize_packing(object, container, config);
        state.initialization_method = static_cast<irop::InitializationMethod>(99);
        CHECK(std::string(irop::to_string(state.initialization_method)) == "unknown");
        irop::test::require_error_category([&]() {
            irop::validate_initial_state(object, container, state);
        }, irop::ErrorCategory::invalid_configuration);
    }
    SECTION("non-finite source coordinate")
    {
        object.vertices.front().x = std::numeric_limits<double>::quiet_NaN();
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::initialize_packing(object, container, config));
        }, irop::ErrorCategory::invalid_mesh);
    }
    SECTION("extreme count rejected before allocation")
    {
        config.object_count = std::numeric_limits<std::uint64_t>::max();
        config.max_sampling_attempts = config.object_count;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::initialize_packing(object, container, config));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("cancellation interrupts the structured phase after reference exhaustion",
          "[initialization][structured][cancellation]")
{
    const auto object = dense_cylinder();
    const auto container = irop::test::box_mesh(175.0, 200.0, 142.5);
    auto config = dense_config(10);
    config.enable_structured_fallback = false;
    std::uint64_t reference_polls = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(object, container, config, [&]() {
            ++reference_polls;
            return false;
        }));
    }, irop::ErrorCategory::resource_limit);
    config.enable_structured_fallback = true;
    std::uint64_t polls = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_packing(object, container, config, [&]() {
            return ++polls > reference_polls + 30;
        }));
    }, irop::ErrorCategory::cancelled);
    CHECK(polls > reference_polls);
}

}  // namespace
