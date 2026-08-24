#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/collision.hpp"
#include "support/test_support.hpp"

namespace {

[[nodiscard]] irop::TriangleMesh translated(irop::TriangleMesh mesh, const double x, const double y = 0.0,
                                            const double z = 0.0)
{
    for (irop::Point3& point : mesh.vertices) {
        point.x += x;
        point.y += y;
        point.z += z;
    }
    return mesh;
}

TEST_CASE("scene collision validation accepts separated contained closed objects")
{
    const std::vector<irop::TriangleMesh> objects {
        translated(irop::test::cube_mesh(0.5), -1.5),
        translated(irop::test::cube_mesh(0.5), 1.5),
    };

    const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(5.0));

    CHECK(report.physical_scene_valid());
    CHECK(report.cat_violation_object_ids.empty());
    CHECK(report.container_violation_object_ids.empty());
    CHECK(report.object_collisions.empty());
    CHECK(report.work.object_pairs_examined == 1);
    CHECK(report.work.triangle_pairs_tested == 432);
    CHECK(report.work.containment_triangle_visits == 48);
}

TEST_CASE("scene collision validation reports crossing touching and nested object solids")
{
    const irop::TriangleMesh container = irop::test::cube_mesh(10.0);

    SECTION("crossing surfaces")
    {
        const std::vector<irop::TriangleMesh> objects {
            irop::test::cube_mesh(1.0),
            translated(irop::test::cube_mesh(1.0), 1.5),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
        REQUIRE(report.object_collisions.size() == 1);
        CHECK((report.object_collisions.front() == irop::ObjectCollisionPair { 0, 1 }));
    }

    SECTION("surface contact")
    {
        const std::vector<irop::TriangleMesh> objects {
            irop::test::cube_mesh(1.0),
            translated(irop::test::cube_mesh(1.0), 2.0),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
        REQUIRE(report.object_collisions.size() == 1);
        CHECK((report.object_collisions.front() == irop::ObjectCollisionPair { 0, 1 }));
    }

    SECTION("nested solids without surface contact")
    {
        const std::vector<irop::TriangleMesh> objects {
            irop::test::cube_mesh(2.0),
            irop::test::cube_mesh(0.5),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
        REQUIRE(report.object_collisions.size() == 1);
        CHECK((report.object_collisions.front() == irop::ObjectCollisionPair { 0, 1 }));
        CHECK_FALSE(report.physical_scene_valid());
    }
}

TEST_CASE("scene collision validation catches nonintersecting containment violations")
{
    SECTION("wholly outside object")
    {
        const std::vector<irop::TriangleMesh> objects {
            translated(irop::test::cube_mesh(0.5), 5.0),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(2.0));
        CHECK(report.container_violation_object_ids == std::vector<std::uint64_t> { 0 });
        CHECK_FALSE(report.physical_scene_valid());
    }

    SECTION("object encloses container")
    {
        const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(3.0) };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(2.0));
        CHECK(report.container_violation_object_ids == std::vector<std::uint64_t> { 0 });
        CHECK_FALSE(report.physical_scene_valid());
    }
}

TEST_CASE("CAT surface contacts are diagnostic and empty CAT entries are allowed")
{
    const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(1.0) };
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);

    SECTION("CAT contact does not invalidate the physical scene")
    {
        const std::vector<irop::TriangleMesh> cat_surfaces {
            translated(irop::test::cube_mesh(1.0), 1.5),
        };
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container, cat_surfaces);
        CHECK(report.cat_violation_object_ids == std::vector<std::uint64_t> { 0 });
        CHECK(report.physical_scene_valid());
    }

    SECTION("participant with no CAT polygons has an empty surface")
    {
        const std::vector<irop::TriangleMesh> cat_surfaces(1);
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container, cat_surfaces);
        CHECK(report.cat_violation_object_ids.empty());
        CHECK(report.physical_scene_valid());
        CHECK(report.work.triangle_pairs_tested == 144);
    }
}

TEST_CASE("scene collision validation reports deterministic pair order")
{
    const std::vector<irop::TriangleMesh> objects {
        irop::test::cube_mesh(3.0),
        irop::test::cube_mesh(2.0),
        irop::test::cube_mesh(1.0),
    };
    const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(10.0));

    REQUIRE(report.object_collisions.size() == 3);
    CHECK((report.object_collisions[0] == irop::ObjectCollisionPair { 0, 1 }));
    CHECK((report.object_collisions[1] == irop::ObjectCollisionPair { 0, 2 }));
    CHECK((report.object_collisions[2] == irop::ObjectCollisionPair { 1, 2 }));
}

TEST_CASE("scene collision validation enforces cumulative work limits")
{
    const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(0.5) };
    const irop::TriangleMesh container = irop::test::cube_mesh(2.0);

    SECTION("exact surface and containment budgets pass")
    {
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 144;
        limits.max_containment_triangle_visits = 12;
        const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container, {}, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.work.triangle_pairs_tested == 144);
        CHECK(report.work.containment_triangle_visits == 12);
    }

    SECTION("surface pair budget")
    {
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 143;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("surface pair budget is cumulative across comparisons")
    {
        const std::vector<irop::TriangleMesh> separated_objects {
            translated(irop::test::cube_mesh(0.5), -1.0),
            translated(irop::test::cube_mesh(0.5), 1.0),
        };
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 431;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(separated_objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);

        limits.max_triangle_pair_tests = 432;
        const irop::SceneCollisionReport report =
            irop::validate_scene_collisions(separated_objects, container, {}, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.work.triangle_pairs_tested == 432);
    }

    SECTION("containment visit budget")
    {
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 144;
        limits.max_containment_triangle_visits = 11;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("reported violation budget is cumulative")
    {
        const std::vector<irop::TriangleMesh> outside_objects {
            translated(irop::test::cube_mesh(0.5), -5.0),
            translated(irop::test::cube_mesh(0.5), 5.0),
        };
        irop::SceneCollisionLimits limits;
        limits.max_reported_violations = 1;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(outside_objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("scene collision validation rejects invalid inputs and limits")
{
    const std::vector<irop::TriangleMesh> objects { irop::test::cube_mesh(0.5) };
    const irop::TriangleMesh container = irop::test::cube_mesh(2.0);

    SECTION("empty object collection")
    {
        const std::vector<irop::TriangleMesh> empty;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(empty, container));
        }, irop::ErrorCategory::invalid_configuration);
    }

    SECTION("CAT count mismatch")
    {
        const std::vector<irop::TriangleMesh> cats(2);
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, cats));
        }, irop::ErrorCategory::invalid_configuration);
    }

    SECTION("partially empty CAT surface")
    {
        irop::TriangleMesh malformed_cat;
        malformed_cat.vertices.push_back({});
        const std::vector<irop::TriangleMesh> cats { malformed_cat };
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, cats));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("open physical object")
    {
        irop::TriangleMesh open = irop::test::tetrahedron_mesh();
        open.triangles.pop_back();
        const std::vector<irop::TriangleMesh> open_objects { open };
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(open_objects, container));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("zero work limit")
    {
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 0;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, {}, limits));
        }, irop::ErrorCategory::invalid_configuration);
    }
}

}  // namespace
