#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "irop/geometry/transform.hpp"
#include "irop/packing/packing.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;

[[nodiscard]] std::vector<irop::TriangleMesh> instantiate(const irop::TriangleMesh& object,
                                                          const std::vector<irop::Transform>& transforms)
{
    std::vector<irop::TriangleMesh> result;
    result.reserve(transforms.size());
    for (const irop::Transform& transform : transforms) {
        result.push_back(irop::transform_mesh(object, transform));
    }
    return result;
}

TEST_CASE("physical correction selectively shrinks a container violator until the scene converges",
          "[packing][correction]")
{
    constexpr double correction_factor = 0.93;
    constexpr std::uint64_t expected_passes = 10;
    const irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    const irop::TriangleMesh container = irop::test::cube_mesh(4.0);
    const std::vector<irop::Transform> candidates {
        { .volume_scale = 1.0, .translation = { 3.2, 0.0, 0.0 }  },
        { .volume_scale = 1.0, .translation = { -2.0, 0.0, 0.0 } },
    };

    irop::SceneCollisionWork collision_work;
    std::uint64_t cumulative_correction_passes = 0;
    const irop::detail::PhysicalCollisionCorrectionResult result = irop::detail::correct_physical_collisions(
        object, container, {}, candidates, correction_factor, 16, {}, {}, collision_work, cumulative_correction_passes);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    CHECK(result.correction_passes == expected_passes);
    CHECK(cumulative_correction_passes == expected_passes);
    REQUIRE(result.transforms.size() == 2);
    CHECK(result.transforms[0].volume_scale ==
          Approx(std::pow(correction_factor, static_cast<double>(expected_passes))).epsilon(1.0e-15));
    CHECK(result.transforms[1].volume_scale == 1.0);
    CHECK(result.transforms[0].translation.x == 3.2);
    CHECK(result.transforms[1].translation.x == -2.0);
    CHECK(result.collision.physical_scene_valid());
    CHECK(collision_work.object_pairs_examined == expected_passes + 1);
    CHECK(result.collision.work.object_pairs_examined == 1);
    CHECK(collision_work.triangle_pairs_tested > result.collision.work.triangle_pairs_tested);
    CHECK(collision_work.containment_triangle_visits > result.collision.work.containment_triangle_visits);
}

TEST_CASE("physical correction returns its bounded limit with completed work retained", "[packing][correction][limits]")
{
    constexpr double correction_factor = 0.93;
    const irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    const irop::TriangleMesh container = irop::test::cube_mesh(0.5);
    const std::vector<irop::Transform> candidates { irop::Transform { .volume_scale = 1.0 } };

    const irop::SceneCollisionReport initial =
        irop::validate_scene_collisions(instantiate(object, candidates), container);
    std::vector<irop::Transform> once_corrected = candidates;
    once_corrected[0].volume_scale *= correction_factor;
    const irop::SceneCollisionReport after_one_pass =
        irop::validate_scene_collisions(instantiate(object, once_corrected), container);
    REQUIRE_FALSE(initial.physical_scene_valid());
    REQUIRE_FALSE(after_one_pass.physical_scene_valid());

    irop::SceneCollisionWork collision_work;
    std::uint64_t cumulative_correction_passes = 7;
    const irop::detail::PhysicalCollisionCorrectionResult result = irop::detail::correct_physical_collisions(
        object, container, {}, candidates, correction_factor, 1, {}, {}, collision_work, cumulative_correction_passes);

    CHECK(result.status == irop::PackingStatus::correction_limit);
    CHECK(result.diagnostic == "packing collision-correction pass limit was exhausted");
    CHECK(result.correction_passes == 1);
    CHECK(cumulative_correction_passes == 8);
    REQUIRE(result.transforms.size() == 1);
    CHECK(result.transforms[0].volume_scale == Approx(correction_factor));
    CHECK_FALSE(result.collision.physical_scene_valid());
    CHECK(collision_work.object_pairs_examined ==
          initial.work.object_pairs_examined + after_one_pass.work.object_pairs_examined);
    CHECK(collision_work.triangle_pairs_tested ==
          initial.work.triangle_pairs_tested + after_one_pass.work.triangle_pairs_tested);
    CHECK(collision_work.containment_triangle_visits ==
          initial.work.containment_triangle_visits + after_one_pass.work.containment_triangle_visits);
}

TEST_CASE("physical correction reduces both members of an overlapping pair", "[packing][correction]")
{
    constexpr double correction_factor = 0.93;
    const irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    const std::vector<irop::Transform> candidates {
        { .volume_scale = 1.0, .translation = { -0.99, 0.0, 0.0 } },
        { .volume_scale = 1.0, .translation = { 0.99, 0.0, 0.0 }  },
    };

    irop::SceneCollisionWork collision_work;
    std::uint64_t cumulative_correction_passes = 0;
    const irop::detail::PhysicalCollisionCorrectionResult result = irop::detail::correct_physical_collisions(
        object, container, {}, candidates, correction_factor, 1, {}, {}, collision_work, cumulative_correction_passes);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    CHECK(result.correction_passes == 1);
    CHECK(cumulative_correction_passes == 1);
    REQUIRE(result.transforms.size() == 2);
    CHECK(result.transforms[0].volume_scale == Approx(correction_factor));
    CHECK(result.transforms[1].volume_scale == Approx(correction_factor));
    CHECK(result.transforms[0].translation.x == -0.99);
    CHECK(result.transforms[1].translation.x == 0.99);
    CHECK(result.collision.physical_scene_valid());
    CHECK(result.collision.object_collisions.empty());
    CHECK(collision_work.object_pairs_examined == 2);
}

TEST_CASE("CAT-only contact remains diagnostic and does not select correction", "[packing][correction][cat]")
{
    const irop::TriangleMesh object = irop::test::cube_mesh(1.0);
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    const std::vector<irop::Transform> candidates {
        {
         .volume_scale = 0.8,
         .rotation = { .x = 0.1, .y = -0.2, .z = 0.15 },
         .translation = { 0.2, -0.1, 0.05 },
         },
    };
    const std::vector<irop::TriangleMesh> cat_surfaces {
        {
         .vertices = { { -2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } },
         .triangles = { { 0, 1, 2 } },
         },
    };

    irop::SceneCollisionWork collision_work;
    std::uint64_t cumulative_correction_passes = 3;
    const irop::detail::PhysicalCollisionCorrectionResult result = irop::detail::correct_physical_collisions(
        object, container, cat_surfaces, candidates, 0.93, 1, {}, {}, collision_work, cumulative_correction_passes);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    CHECK(result.correction_passes == 0);
    CHECK(cumulative_correction_passes == 3);
    REQUIRE(result.transforms.size() == 1);
    CHECK(result.transforms[0].volume_scale == candidates[0].volume_scale);
    CHECK(result.transforms[0].rotation.x == candidates[0].rotation.x);
    CHECK(result.transforms[0].rotation.y == candidates[0].rotation.y);
    CHECK(result.transforms[0].rotation.z == candidates[0].rotation.z);
    CHECK(result.transforms[0].translation.x == candidates[0].translation.x);
    CHECK(result.transforms[0].translation.y == candidates[0].translation.y);
    CHECK(result.transforms[0].translation.z == candidates[0].translation.z);
    CHECK(result.collision.physical_scene_valid());
    CHECK(result.collision.cat_violation_object_ids == std::vector<std::uint64_t> { 0 });
    CHECK(result.collision.container_violation_object_ids.empty());
    CHECK(result.collision.object_collisions.empty());
}

}  // namespace
