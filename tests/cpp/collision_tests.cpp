#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
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
    CHECK(report.work.triangle_pairs_tested == 288);
    CHECK(report.work.containment_triangle_visits == 24);
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
        for (const irop::Point3 offset : {
                 irop::Point3 { 2.0, 0.0, 0.0 },
                  irop::Point3 { 2.0, 2.0, 0.0 },
                  irop::Point3 { 2.0, 2.0, 2.0 }
        }) {
            CAPTURE(offset.x, offset.y, offset.z);
            const std::vector<irop::TriangleMesh> objects {
                irop::test::cube_mesh(1.0),
                translated(irop::test::cube_mesh(1.0), offset.x, offset.y, offset.z),
            };
            const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, container);
            REQUIRE(report.object_collisions.size() == 1);
            CHECK((report.object_collisions.front() == irop::ObjectCollisionPair { 0, 1 }));
        }
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

TEST_CASE("CAT diagnostic budgets leave physical validation complete", "[collision][cat-diagnostics]")
{
    const irop::TriangleMesh container = irop::test::cube_mesh(5.0);
    const std::vector<irop::TriangleMesh> objects {
        translated(irop::test::cube_mesh(0.5), -1.5),
        translated(irop::test::cube_mesh(0.5), 1.5),
    };
    const std::vector<irop::TriangleMesh> disjoint_cats {
        translated(irop::test::cube_mesh(0.5), -10.0),
        translated(irop::test::cube_mesh(0.5), 10.0),
    };
    irop::SceneCollisionLimits limits;
    limits.max_triangle_pair_tests = 288;
    limits.max_containment_triangle_visits = 24;
    limits.max_cat_triangle_pair_tests = 288;

    SECTION("complete CAT work has its own exact allowance")
    {
        const auto report = irop::validate_scene_collisions(objects, container, disjoint_cats, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.cat_diagnostics_complete);
        CHECK(report.work.triangle_pairs_tested == 288);
        CHECK(report.work.cat_triangle_pairs_tested == 288);
        CHECK(report.work.containment_triangle_visits == 24);
    }

    SECTION("a CAT query that cannot fit is skipped and completeness is explicit")
    {
        limits.max_cat_triangle_pair_tests = 287;
        const auto report = irop::validate_scene_collisions(objects, container, disjoint_cats, limits);
        CHECK(report.physical_scene_valid());
        CHECK_FALSE(report.cat_diagnostics_complete);
        CHECK(report.cat_violation_object_ids.empty());
        CHECK(report.work.triangle_pairs_tested == 288);
        CHECK(report.work.cat_triangle_pairs_tested == 144);
        CHECK(report.work.containment_triangle_visits == 24);
    }

    SECTION("an exhausted CAT allowance still evaluates all physical geometry")
    {
        limits.max_cat_triangle_pair_tests = 0;
        const std::vector<irop::TriangleMesh> outside_objects {
            objects.front(),
            translated(objects.back(), 10.0),
        };
        const auto report = irop::validate_scene_collisions(outside_objects, container, disjoint_cats, limits);
        CHECK_FALSE(report.physical_scene_valid());
        CHECK(report.container_violation_object_ids == std::vector<std::uint64_t> { 1 });
        CHECK_FALSE(report.cat_diagnostics_complete);
        CHECK(report.work.triangle_pairs_tested == 288);
        CHECK(report.work.cat_triangle_pairs_tested == 0);
    }

    SECTION("no requested CAT queries remain complete with a zero allowance")
    {
        limits.max_cat_triangle_pair_tests = 0;
        const std::vector<irop::TriangleMesh> empty_cats(objects.size());
        const auto report = irop::validate_scene_collisions(objects, container, empty_cats, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.cat_diagnostics_complete);
        CHECK(report.work.cat_triangle_pairs_tested == 0);
    }

    SECTION("CAT omission never swallows physical resource exhaustion")
    {
        limits.max_cat_triangle_pair_tests = 0;
        limits.max_triangle_pair_tests = 287;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, disjoint_cats, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("all CAT inputs are validated even if diagnostics are skipped")
    {
        limits.max_cat_triangle_pair_tests = 0;
        auto malformed_cats = disjoint_cats;
        malformed_cats.back().triangles.front()[0] = static_cast<std::uint64_t>(malformed_cats.back().vertices.size());
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, malformed_cats, limits));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("CAT contact reporting leaves the physical violation allowance available", "[collision][cat-diagnostics]")
{
    const std::vector<irop::TriangleMesh> objects {
        irop::test::cube_mesh(0.5),
        translated(irop::test::cube_mesh(0.5), 10.0),
    };
    const auto& cat_surfaces = objects;
    irop::SceneCollisionLimits limits;
    limits.max_reported_violations = 1;
    limits.max_triangle_pair_tests = 288;
    const auto report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(5.0), cat_surfaces, limits);
    CHECK(report.cat_violation_object_ids == std::vector<std::uint64_t> { 0 });
    CHECK_FALSE(report.cat_diagnostics_complete);
    CHECK(report.container_violation_object_ids == std::vector<std::uint64_t> { 1 });
    CHECK_FALSE(report.physical_scene_valid());
    CHECK(report.work.triangle_pairs_tested == 288);
    CHECK(report.work.cat_triangle_pairs_tested > 0);
    CHECK(report.work.cat_triangle_pairs_tested <= 144);
}

TEST_CASE("scene collision validation reports deterministic pair order")
{
    const std::vector<irop::TriangleMesh> objects {
        irop::test::cube_mesh(3.0),
        translated(irop::test::cube_mesh(0.25), 5.0),
        irop::test::cube_mesh(2.0),
        irop::test::cube_mesh(1.0),
    };
    const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(10.0));

    REQUIRE(report.object_collisions.size() == 3);
    CHECK((report.object_collisions[0] == irop::ObjectCollisionPair { 0, 2 }));
    CHECK((report.object_collisions[1] == irop::ObjectCollisionPair { 0, 3 }));
    CHECK((report.object_collisions[2] == irop::ObjectCollisionPair { 2, 3 }));
    CHECK(report.work.object_pairs_examined == 6);
}

TEST_CASE("scene collision broad phase skips separated objects within exact narrow-phase budgets")
{
    std::vector<irop::TriangleMesh> objects;
    for (const double x : { -2.0, 0.0, 2.0 }) {
        for (const double y : { -2.0, 0.0, 2.0 }) {
            for (const double z : { -2.0, 0.0, 2.0 }) {
                objects.push_back(translated(irop::test::cube_mesh(0.25), x, y, z));
            }
        }
    }

    irop::SceneCollisionLimits limits;
    limits.max_object_pair_checks = 351;
    limits.max_triangle_pair_tests = 27 * 144;
    limits.max_containment_triangle_visits = 27 * 12;
    const irop::SceneCollisionReport report =
        irop::validate_scene_collisions(objects, irop::test::cube_mesh(5.0), {}, limits);

    CHECK(report.physical_scene_valid());
    CHECK(report.work.object_pairs_examined == 351);
    CHECK(report.work.triangle_pairs_tested == 27 * 144);
    CHECK(report.work.containment_triangle_visits == 27 * 12);
}

TEST_CASE("scene collision broad phase retains representable gaps on each axis")
{
    const double separation = std::nextafter(2.0, 3.0);
    for (const irop::Point3 offset : {
             irop::Point3 { separation,  0.0,         0.0         },
             irop::Point3 { -separation, 0.0,         0.0         },
             irop::Point3 { 0.0,         separation,  0.0         },
             irop::Point3 { 0.0,         -separation, 0.0         },
             irop::Point3 { 0.0,         0.0,         separation  },
             irop::Point3 { 0.0,         0.0,         -separation },
    }) {
        CAPTURE(offset.x, offset.y, offset.z);
        const std::vector<irop::TriangleMesh> objects {
            irop::test::cube_mesh(1.0),
            translated(irop::test::cube_mesh(1.0), offset.x, offset.y, offset.z),
        };
        irop::SceneCollisionLimits limits;
        limits.max_triangle_pair_tests = 288;
        limits.max_containment_triangle_visits = 24;
        const irop::SceneCollisionReport report =
            irop::validate_scene_collisions(objects, irop::test::cube_mesh(5.0), {}, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.work.object_pairs_examined == 1);
        CHECK(report.work.triangle_pairs_tested == 288);
        CHECK(report.work.containment_triangle_visits == 24);
    }
}

TEST_CASE("scene collision broad phase leaves overlapping bounds to the narrow phase")
{
    const std::vector<irop::TriangleMesh> objects {
        irop::test::tetrahedron_mesh(),
        translated(irop::test::tetrahedron_mesh(), 0.75, 0.75, 0.75),
    };
    const irop::SceneCollisionReport report = irop::validate_scene_collisions(objects, irop::test::cube_mesh(5.0));

    CHECK(report.physical_scene_valid());
    CHECK(report.work.object_pairs_examined == 1);
    CHECK(report.work.triangle_pairs_tested == 2 * 4 * 12 + 4 * 4);
    CHECK(report.work.containment_triangle_visits == 2 * 12 + 2 * 4);
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
        limits.max_triangle_pair_tests = 287;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(separated_objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);

        limits.max_triangle_pair_tests = 288;
        const irop::SceneCollisionReport report =
            irop::validate_scene_collisions(separated_objects, container, {}, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.work.triangle_pairs_tested == 288);
    }

    SECTION("broad-phase rejections consume the object-pair budget")
    {
        const std::vector<irop::TriangleMesh> separated_objects {
            translated(irop::test::cube_mesh(0.25), -1.0),
            irop::test::cube_mesh(0.25),
            translated(irop::test::cube_mesh(0.25), 1.0),
        };
        irop::SceneCollisionLimits limits;
        limits.max_object_pair_checks = 2;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(separated_objects, container, {}, limits));
        }, irop::ErrorCategory::resource_limit);

        limits.max_object_pair_checks = 3;
        const irop::SceneCollisionReport report =
            irop::validate_scene_collisions(separated_objects, container, {}, limits);
        CHECK(report.physical_scene_valid());
        CHECK(report.work.object_pairs_examined == 3);
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

    SECTION("zero object-pair limit")
    {
        irop::SceneCollisionLimits limits;
        limits.max_object_pair_checks = 0;
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(objects, container, {}, limits));
        }, irop::ErrorCategory::invalid_configuration);
    }

    SECTION("separated objects are validated before broad-phase rejection")
    {
        irop::TriangleMesh malformed = translated(irop::test::cube_mesh(0.5), 100.0);
        malformed.vertices.front().x = std::numeric_limits<double>::infinity();
        const std::vector<irop::TriangleMesh> malformed_objects { objects.front(), malformed };
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::validate_scene_collisions(malformed_objects, container));
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
