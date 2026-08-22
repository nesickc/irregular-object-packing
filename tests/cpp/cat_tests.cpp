#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "irop/cat/cat.hpp"

namespace {

using Catch::Approx;
using ExpectedPolygon = std::vector<irop::Point3>;
using ExpectedPolygons = std::vector<ExpectedPolygon>;

[[nodiscard]] irop::TetrahedralMesh single_tetrahedron(const std::array<irop::ParticipantId, 4>& owners)
{
    irop::TetrahedralMesh mesh;
    mesh.points = {
        { 0.0,  0.0,  0.0  },
        { 36.0, 0.0,  0.0  },
        { 0.0,  36.0, 0.0  },
        { 0.0,  0.0,  36.0 }
    };
    mesh.tetrahedra = {
        { 0, 1, 2, 3 }
    };
    mesh.point_owners.assign(owners.begin(), owners.end());
    mesh.participant_count = static_cast<std::uint64_t>(*std::max_element(owners.begin(), owners.end())) + 1;
    return mesh;
}

[[nodiscard]] std::span<const irop::CatPolygon> polygons_for(const irop::CatConstructionResult& result,
                                                             const irop::ParticipantId participant)
{
    const irop::CatParticipantRange& range = result.participant_ranges[participant];
    return { result.polygons.data() + static_cast<std::size_t>(range.polygon_begin),
             static_cast<std::size_t>(range.polygon_count) };
}

[[nodiscard]] std::span<const irop::CatPlaneConstraint> constraints_for(const irop::CatConstructionResult& result,
                                                                        const irop::ParticipantId participant)
{
    const irop::CatParticipantRange& range = result.participant_ranges[participant];
    return { result.constraints.data() + static_cast<std::size_t>(range.constraint_begin),
             static_cast<std::size_t>(range.constraint_count) };
}

void check_point(const irop::Point3& actual, const irop::Point3& expected)
{
    CHECK(actual.x == Approx(expected.x).margin(1.0e-12));
    CHECK(actual.y == Approx(expected.y).margin(1.0e-12));
    CHECK(actual.z == Approx(expected.z).margin(1.0e-12));
}

void check_polygons(const irop::CatConstructionResult& result, const irop::ParticipantId participant,
                    const ExpectedPolygons& expected)
{
    const std::span<const irop::CatPolygon> actual = polygons_for(result, participant);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t polygon_index = 0; polygon_index < actual.size(); ++polygon_index) {
        CAPTURE(participant, polygon_index);
        CHECK(actual[polygon_index].owner == participant);
        CHECK(actual[polygon_index].tetrahedron == 0);
        REQUIRE(actual[polygon_index].vertex_count == expected[polygon_index].size());
        for (std::size_t vertex_index = 0; vertex_index < expected[polygon_index].size(); ++vertex_index) {
            check_point(actual[polygon_index].vertices[vertex_index], expected[polygon_index][vertex_index]);
        }
    }
}

[[nodiscard]] double dot(const irop::Point3& left, const irop::Point3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] irop::Point3 subtract(const irop::Point3& left, const irop::Point3& right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] irop::TetrahedralMesh all_partition_fixture()
{
    irop::TetrahedralMesh mesh;
    mesh.participant_count = 4;
    const std::array<std::array<irop::ParticipantId, 4>, 5> owners {
        std::array<irop::ParticipantId, 4> { 0, 0, 0, 0 },
          std::array<irop::ParticipantId, 4> { 0, 0, 0, 1 },
        std::array<irop::ParticipantId, 4> { 0, 0, 1, 1 },
          std::array<irop::ParticipantId, 4> { 0, 0, 1, 2 },
        std::array<irop::ParticipantId, 4> { 0, 1, 2, 3 },
    };

    for (std::size_t cell = 0; cell < owners.size(); ++cell) {
        const double offset = static_cast<double>(cell) * 100.0;
        const irop::MeshIndex first = static_cast<irop::MeshIndex>(mesh.points.size());
        mesh.points.push_back({ offset, 0.0, 0.0 });
        mesh.points.push_back({ offset + 36.0, 0.0, 0.0 });
        mesh.points.push_back({ offset, 36.0, 0.0 });
        mesh.points.push_back({ offset, 0.0, 36.0 });
        mesh.tetrahedra.push_back({ first, first + 1, first + 2, first + 3 });
        mesh.point_owners.insert(mesh.point_owners.end(), owners[cell].begin(), owners[cell].end());
    }
    return mesh;
}

}  // namespace

TEST_CASE("CAT four-participant split matches the complete Python golden")
{
    const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 3, 2, 1, 0 }));
    REQUIRE(result.succeeded());

    check_polygons(result, 3,
                   {
                       { { 9, 9, 9 }, { 18, 0, 0 },  { 12, 12, 0 } },
                       { { 9, 9, 9 }, { 12, 12, 0 }, { 0, 18, 0 }  },
                       { { 9, 9, 9 }, { 0, 18, 0 },  { 0, 12, 12 } },
                       { { 9, 9, 9 }, { 0, 12, 12 }, { 0, 0, 18 }  },
                       { { 9, 9, 9 }, { 0, 0, 18 },  { 12, 0, 12 } },
                       { { 9, 9, 9 }, { 12, 0, 12 }, { 18, 0, 0 }  },
    });
    check_polygons(result, 2,
                   {
                       { { 9, 9, 9 }, { 18, 0, 0 },   { 12, 12, 0 }  },
                       { { 9, 9, 9 }, { 12, 12, 0 },  { 18, 18, 0 }  },
                       { { 9, 9, 9 }, { 18, 18, 0 },  { 12, 12, 12 } },
                       { { 9, 9, 9 }, { 12, 12, 12 }, { 18, 0, 18 }  },
                       { { 9, 9, 9 }, { 18, 0, 18 },  { 12, 0, 12 }  },
                       { { 9, 9, 9 }, { 12, 0, 12 },  { 18, 0, 0 }   },
    });
    check_polygons(result, 1,
                   {
                       { { 9, 9, 9 }, { 0, 18, 0 },   { 12, 12, 0 }  },
                       { { 9, 9, 9 }, { 12, 12, 0 },  { 18, 18, 0 }  },
                       { { 9, 9, 9 }, { 18, 18, 0 },  { 12, 12, 12 } },
                       { { 9, 9, 9 }, { 12, 12, 12 }, { 0, 18, 18 }  },
                       { { 9, 9, 9 }, { 0, 18, 18 },  { 0, 12, 12 }  },
                       { { 9, 9, 9 }, { 0, 12, 12 },  { 0, 18, 0 }   },
    });
    check_polygons(result, 0,
                   {
                       { { 9, 9, 9 }, { 0, 0, 18 },   { 12, 0, 12 }  },
                       { { 9, 9, 9 }, { 12, 0, 12 },  { 18, 0, 18 }  },
                       { { 9, 9, 9 }, { 18, 0, 18 },  { 12, 12, 12 } },
                       { { 9, 9, 9 }, { 12, 12, 12 }, { 0, 18, 18 }  },
                       { { 9, 9, 9 }, { 0, 18, 18 },  { 0, 12, 12 }  },
                       { { 9, 9, 9 }, { 0, 12, 12 },  { 0, 0, 18 }   },
    });

    CHECK(result.work.polygons_generated == 24);
    CHECK(result.work.constraints_generated == 24);
    for (irop::ParticipantId owner = 0; owner < 4; ++owner) {
        CHECK(constraints_for(result, owner).size() == 6);
    }
}

TEST_CASE("CAT two-one-one split matches the complete Python golden")
{
    const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 0, 0, 2, 1 }));
    REQUIRE(result.succeeded());

    const ExpectedPolygon first {
        { 0,  12, 12 },
        { 12, 12, 12 },
        { 18, 18, 0  },
        { 0,  18, 0  }
    };
    const ExpectedPolygon second {
        { 0,  12, 12 },
        { 12, 12, 12 },
        { 18, 0,  18 },
        { 0,  0,  18 }
    };
    const ExpectedPolygon singletons {
        { 0,  12, 12 },
        { 12, 12, 12 },
        { 0,  18, 18 }
    };
    check_polygons(result, 0, { first, second });
    check_polygons(result, 1, { second, singletons });
    check_polygons(result, 2, { first, singletons });

    CHECK(constraints_for(result, 0).size() == 4);
    CHECK(constraints_for(result, 1).size() == 2);
    CHECK(constraints_for(result, 2).size() == 2);
    CHECK(result.work.polygons_generated == 6);
    CHECK(result.work.polygon_vertices_generated == 22);
    CHECK(result.work.constraints_generated == 8);
}

TEST_CASE("CAT three-one split matches the complete Python golden")
{
    const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 1, 1, 1, 0 }));
    REQUIRE(result.succeeded());

    const ExpectedPolygon face {
        { 0,  0,  18 },
        { 18, 0,  18 },
        { 0,  18, 18 }
    };
    check_polygons(result, 0, { face });
    check_polygons(result, 1, { face });
    CHECK(constraints_for(result, 0).size() == 1);
    CHECK(constraints_for(result, 1).size() == 3);
    CHECK(result.work.polygons_generated == 2);
    CHECK(result.work.constraints_generated == 4);
}

TEST_CASE("CAT two-two split matches the complete Python golden")
{
    const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 1, 1, 0, 0 }));
    REQUIRE(result.succeeded());

    const ExpectedPolygon face {
        { 0,  18, 0  },
        { 0,  0,  18 },
        { 18, 0,  18 },
        { 18, 18, 0  }
    };
    check_polygons(result, 0, { face });
    check_polygons(result, 1, { face });
    CHECK(constraints_for(result, 0).size() == 2);
    CHECK(constraints_for(result, 1).size() == 2);
    CHECK(result.work.polygons_generated == 2);
    CHECK(result.work.constraints_generated == 4);
}

TEST_CASE("CAT filtering covers every ownership partition and groups output by participant")
{
    const irop::CatConstructionResult result = irop::build_cat(all_partition_fixture());
    REQUIRE(result.succeeded());

    CHECK(result.work.points_examined == 20);
    CHECK(result.work.tetrahedra_examined == 5);
    CHECK(result.work.skipped_single_participant_tetrahedra == 1);
    CHECK(result.work.relevant_tetrahedra == 4);
    CHECK(result.work.polygons_generated == 34);
    CHECK(result.work.polygon_vertices_generated == 108);
    CHECK(result.work.constraints_generated == 40);

    REQUIRE(result.participant_ranges.size() == 4);
    CHECK(result.participant_ranges[0].polygon_begin == 0);
    CHECK(result.participant_ranges[0].polygon_count == 10);
    CHECK(result.participant_ranges[0].constraint_begin == 0);
    CHECK(result.participant_ranges[0].constraint_count == 15);
    CHECK(result.participant_ranges[1].polygon_begin == 10);
    CHECK(result.participant_ranges[1].polygon_count == 10);
    CHECK(result.participant_ranges[1].constraint_begin == 15);
    CHECK(result.participant_ranges[1].constraint_count == 11);
    CHECK(result.participant_ranges[2].polygon_begin == 20);
    CHECK(result.participant_ranges[2].polygon_count == 8);
    CHECK(result.participant_ranges[2].constraint_begin == 26);
    CHECK(result.participant_ranges[2].constraint_count == 8);
    CHECK(result.participant_ranges[3].polygon_begin == 28);
    CHECK(result.participant_ranges[3].polygon_count == 6);
    CHECK(result.participant_ranges[3].constraint_begin == 34);
    CHECK(result.participant_ranges[3].constraint_count == 6);
}

TEST_CASE("CAT plane normals are finite unit inward and coplanar with their polygons")
{
    // Includes skipped 4+0 plus 3+1, 2+2, 2+1+1, and 1+1+1+1 cells.
    const irop::TetrahedralMesh mesh = all_partition_fixture();
    const irop::CatConstructionResult result = irop::build_cat(mesh);
    REQUIRE(result.succeeded());

    for (const irop::CatPlaneConstraint& constraint : result.constraints) {
        CAPTURE(constraint.owner, constraint.source_point, constraint.polygon);
        REQUIRE(constraint.source_point < mesh.points.size());
        REQUIRE(constraint.polygon < result.polygons.size());
        const irop::CatPolygon& polygon = result.polygons[static_cast<std::size_t>(constraint.polygon)];
        CHECK(polygon.owner == constraint.owner);
        check_point(constraint.plane_point, polygon.vertices[0]);

        const double length = std::sqrt(dot(constraint.inward_unit_normal, constraint.inward_unit_normal));
        CHECK(length == Approx(1.0).margin(1.0e-12));
        const double source_clearance =
            dot(subtract(mesh.points[static_cast<std::size_t>(constraint.source_point)], constraint.plane_point),
                constraint.inward_unit_normal);
        CHECK(source_clearance > 0.0);
        for (std::size_t vertex = 0; vertex < polygon.vertex_count; ++vertex) {
            CHECK(dot(subtract(polygon.vertices[vertex], constraint.plane_point), constraint.inward_unit_normal) ==
                  Approx(0.0).margin(1.0e-12));
        }
    }

    const irop::Point3 first_normal = constraints_for(result, 0).front().inward_unit_normal;
    const irop::Point3 second_normal = constraints_for(result, 1).front().inward_unit_normal;
    CHECK(dot(first_normal, second_normal) == Approx(-1.0).margin(1.0e-12));
}

TEST_CASE("CAT rejects unsafe tetrahedral meshes with project-owned status")
{
    SECTION("ownership count")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.point_owners.pop_back();
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
    SECTION("owner range")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.point_owners[0] = 2;
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
    SECTION("non-finite point")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.points[0].x = std::numeric_limits<double>::infinity();
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
    SECTION("out-of-range index")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.tetrahedra[0][3] = 4;
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
    SECTION("repeated index")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.tetrahedra[0][3] = mesh.tetrahedra[0][2];
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
    SECTION("degenerate tetrahedron")
    {
        irop::TetrahedralMesh mesh = single_tetrahedron({ 1, 1, 0, 0 });
        mesh.points[3] = { 36.0, 36.0, 0.0 };
        CHECK(irop::build_cat(mesh).status == irop::CatConstructionStatus::invalid_input);
    }
}

TEST_CASE("CAT construction enforces each configured work and output limit")
{
    SECTION("points")
    {
        irop::CatConstructionLimits limits;
        limits.max_points = 3;
        CHECK(irop::build_cat(single_tetrahedron({ 1, 1, 0, 0 }), limits).status ==
              irop::CatConstructionStatus::resource_exhausted);
    }
    SECTION("tetrahedra")
    {
        irop::CatConstructionLimits limits;
        limits.max_tetrahedra = 4;
        CHECK(irop::build_cat(all_partition_fixture(), limits).status ==
              irop::CatConstructionStatus::resource_exhausted);
    }
    SECTION("relevant tetrahedra")
    {
        irop::CatConstructionLimits limits;
        limits.max_relevant_tetrahedra = 3;
        const irop::CatConstructionResult result = irop::build_cat(all_partition_fixture(), limits);
        CHECK(result.status == irop::CatConstructionStatus::resource_exhausted);
        CHECK(result.work.relevant_tetrahedra == 3);
    }
    SECTION("polygons")
    {
        irop::CatConstructionLimits limits;
        limits.max_polygons = 1;
        const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 1, 1, 1, 0 }), limits);
        CHECK(result.status == irop::CatConstructionStatus::resource_exhausted);
        CHECK(result.work.polygons_generated == 1);
    }
    SECTION("polygon vertices")
    {
        irop::CatConstructionLimits limits;
        limits.max_polygon_vertices = 5;
        const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 1, 1, 1, 0 }), limits);
        CHECK(result.status == irop::CatConstructionStatus::resource_exhausted);
        CHECK(result.work.polygon_vertices_generated == 3);
    }
    SECTION("constraints")
    {
        irop::CatConstructionLimits limits;
        limits.max_constraints = 2;
        const irop::CatConstructionResult result = irop::build_cat(single_tetrahedron({ 1, 1, 1, 0 }), limits);
        CHECK(result.status == irop::CatConstructionStatus::resource_exhausted);
        CHECK(result.work.constraints_generated == 2);
    }
}

TEST_CASE("CAT construction status names are stable")
{
    CHECK(std::string(irop::to_string(irop::CatConstructionStatus::success)) == "success");
    CHECK(std::string(irop::to_string(irop::CatConstructionStatus::invalid_input)) == "invalid_input");
    CHECK(std::string(irop::to_string(irop::CatConstructionStatus::resource_exhausted)) == "resource_exhausted");
    CHECK(std::string(irop::to_string(static_cast<irop::CatConstructionStatus>(999))) == "unknown");
}
