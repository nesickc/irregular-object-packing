#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <future>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "../../src/tetrahedralization/output_validation.hpp"
#include "irop/cat/cat.hpp"
#include "irop/model/triangle_mesh.hpp"
#include "irop/tetrahedralization/tetrahedralization.hpp"
#include "support/test_support.hpp"

namespace {

[[nodiscard]] irop::TriangleMesh shifted(irop::TriangleMesh mesh, const irop::Point3& offset)
{
    for (irop::Point3& point : mesh.vertices) {
        point.x += offset.x;
        point.y += offset.y;
        point.z += offset.z;
    }
    return mesh;
}

[[nodiscard]] std::vector<irop::TriangleMesh> one_tetrahedron() { return { irop::test::tetrahedron_mesh() }; }

[[nodiscard]] irop::TriangleMesh disconnected_tetrahedra()
{
    irop::TriangleMesh combined = irop::test::tetrahedron_mesh();
    const irop::TriangleMesh second = shifted(irop::test::tetrahedron_mesh(), { 3.0, 0.0, 0.0 });
    const irop::MeshIndex offset = static_cast<irop::MeshIndex>(combined.vertices.size());
    combined.vertices.insert(combined.vertices.end(), second.vertices.begin(), second.vertices.end());
    for (const irop::Triangle& triangle : second.triangles) {
        combined.triangles.push_back({ triangle[0] + offset, triangle[1] + offset, triangle[2] + offset });
    }
    return combined;
}

[[nodiscard]] irop::TetrahedralMesh recovery_test_mesh()
{
    irop::TetrahedralMesh mesh;
    mesh.participant_count = 2;
    mesh.points = {
        { 0.0,  0.0, 0.0 },
        { 1.0,  0.0, 0.0 },
        { 0.0,  1.0, 0.0 },
        { 0.0,  0.0, 1.0 },
        { 10.0, 0.0, 0.0 },
        { 11.0, 0.0, 0.0 },
        { 10.0, 1.0, 0.0 },
        { 11.0, 1.0, 0.0 }
    };
    mesh.point_owners = { 0, 0, 0, 1, 0, 0, 0, 0 };
    return mesh;
}

}  // namespace

TEST_CASE("tetrahedralization status names are stable")
{
    CHECK(std::string_view(irop::to_string(irop::TetrahedralizationStatus::success)) == "success");
    CHECK(std::string_view(irop::to_string(irop::TetrahedralizationStatus::invalid_input)) == "invalid_input");
    CHECK(std::string_view(irop::to_string(irop::TetrahedralizationStatus::resource_exhausted)) ==
          "resource_exhausted");
    CHECK(std::string_view(irop::to_string(irop::TetrahedralizationStatus::dependency_failure)) ==
          "dependency_failure");
}

TEST_CASE("single-participant zero-volume output omission requires explicit opt-in")
{
    irop::TetrahedralMesh mesh = recovery_test_mesh();
    irop::TetrahedralizationWork work;
    work.output_tetrahedra = 1;
    irop::TetrahedralizationOptions options;
    CHECK_FALSE(options.omit_degenerate_single_participant_tetrahedra);
    constexpr irop::Tetrahedron zero_cell { 4, 5, 6, 7 };
    CHECK_FALSE(irop::detail::append_validated_tetrahedron(zero_cell, mesh, options, work));
    CHECK(mesh.tetrahedra.empty());
    CHECK(work.omitted_single_participant_tetrahedra == 0);

    options.omit_degenerate_single_participant_tetrahedra = true;
    CHECK(irop::detail::append_validated_tetrahedron(zero_cell, mesh, options, work));
    CHECK(mesh.tetrahedra.empty());
    CHECK(work.omitted_single_participant_tetrahedra == 1);
    CHECK(work.output_tetrahedra == 1);
}

TEST_CASE("single-participant recovery cannot conceal invalid or mixed-participant output")
{
    irop::TetrahedralMesh mesh = recovery_test_mesh();
    irop::Tetrahedron cell { 4, 5, 6, 7 };
    irop::TetrahedralizationWork work;
    work.output_tetrahedra = 1;
    const irop::TetrahedralizationOptions options { .omit_degenerate_single_participant_tetrahedra = true };
    SECTION("mixed-participant zero volume") { mesh.point_owners[7] = 1; }
    SECTION("duplicate indices") { cell[3] = cell[2]; }
    SECTION("out-of-range indices") { cell[3] = 8; }
    SECTION("missing ownership") { mesh.point_owners.pop_back(); }
    SECTION("invalid ownership") { mesh.point_owners[7] = 2; }
    SECTION("nonfinite coordinates") { mesh.points[7].z = std::numeric_limits<double>::infinity(); }
    SECTION("finite coordinates with overflowing determinant arithmetic")
    {
        mesh.points[4].x = -std::numeric_limits<double>::max();
        mesh.points[5].x = std::numeric_limits<double>::max();
    }
    CHECK_FALSE(irop::detail::append_validated_tetrahedron(cell, mesh, options, work));
    CHECK(mesh.tetrahedra.empty());
    CHECK(work.omitted_single_participant_tetrahedra == 0);
}

TEST_CASE("omitted single-participant tetrahedra cannot generate CAT constraints")
{
    irop::TetrahedralMesh mesh = recovery_test_mesh();
    constexpr irop::Tetrahedron mixed_cell { 0, 1, 2, 3 };
    constexpr irop::Tetrahedron zero_cell { 4, 5, 6, 7 };
    irop::TetrahedralizationWork work;
    work.output_tetrahedra = 2;
    const irop::TetrahedralizationOptions options { .omit_degenerate_single_participant_tetrahedra = true };
    REQUIRE(irop::detail::append_validated_tetrahedron(mixed_cell, mesh, options, work));
    REQUIRE(irop::detail::append_validated_tetrahedron(zero_cell, mesh, options, work));
    REQUIRE(mesh.tetrahedra.size() == 1);
    CHECK(mesh.tetrahedra.front() == mixed_cell);
    CHECK(work.omitted_single_participant_tetrahedra == 1);
    CHECK(work.output_tetrahedra == 2);
    const auto cat = irop::build_cat(mesh);
    INFO(cat.diagnostic);
    REQUIRE(cat.succeeded());
    CHECK(cat.work.tetrahedra_examined == 1);
    CHECK(cat.work.relevant_tetrahedra == 1);
    REQUIRE_FALSE(cat.constraints.empty());
    for (const auto& constraint : cat.constraints) {
        CHECK(constraint.source_point < 4);
    }
    for (const auto& polygon : cat.polygons) {
        CHECK(polygon.tetrahedron == 0);
    }
}

TEST_CASE("tetrahedralization validates its participant boundary")
{
    SECTION("at least one participant is required")
    {
        const std::vector<irop::TriangleMesh> participants;
        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK_FALSE(result.succeeded());
        CHECK(result.mesh.points.empty());
        CHECK(result.mesh.tetrahedra.empty());
    }

    SECTION("all limits must be positive")
    {
        const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        irop::TetrahedralizationLimits limits;
        limits.max_output_tetrahedra = 0;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK(result.work.input_participants == 0);
    }

    SECTION("empty participant meshes are rejected")
    {
        const std::array participants { irop::TriangleMesh {} };
        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK_FALSE(result.diagnostic.empty());
    }

    SECTION("non-finite participant points are rejected")
    {
        std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        participants.front().vertices.front().x = std::numeric_limits<double>::infinity();

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK(result.diagnostic.find("non-finite") != std::string::npos);
    }

    SECTION("invalid triangle indices are rejected")
    {
        std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        participants.front().triangles.front()[2] = 99;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK(result.diagnostic.find("out-of-range") != std::string::npos);
    }

    SECTION("degenerate input triangles are rejected")
    {
        std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        participants.front().vertices[2] = participants.front().vertices[1];

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK(result.diagnostic.find("degenerate") != std::string::npos);
    }

    SECTION("open participants are rejected before the TetGen boundary")
    {
        std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        participants.front().triangles.pop_back();

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK_FALSE(result.diagnostic.empty());
        CHECK(result.mesh.tetrahedra.empty());
    }

    SECTION("multiple closed components are rejected as one ambiguous participant")
    {
        const std::array participants { disconnected_tetrahedra() };

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::invalid_input);
        CHECK(result.diagnostic.find("connected surface component") != std::string::npos);
    }

    SECTION("TetGen output failures are translated after project validation")
    {
        const std::array participants { irop::test::tetrahedron_mesh(), irop::test::tetrahedron_mesh() };

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

        CHECK(result.status == irop::TetrahedralizationStatus::dependency_failure);
        CHECK_FALSE(result.diagnostic.empty());
        CHECK(result.mesh.tetrahedra.empty());
    }
}

TEST_CASE("tetrahedralization accepts locally inconsistent input winding")
{
    std::vector<irop::TriangleMesh> participants = one_tetrahedron();
    std::swap(participants.front().triangles.front()[1], participants.front().triangles.front()[2]);

    const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

    CHECK(result.succeeded());
}

TEST_CASE("tetrahedralization enforces aggregate input limits before TetGen")
{
    SECTION("participant count")
    {
        const std::array participants { irop::test::tetrahedron_mesh(),
                                        shifted(irop::test::tetrahedron_mesh(), { 3.0, 0.0, 0.0 }) };
        irop::TetrahedralizationLimits limits;
        limits.max_participants = 1;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.input_participants == 2);
        CHECK(result.work.input_points == 0);
    }

    SECTION("point count")
    {
        const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        irop::TetrahedralizationLimits limits;
        limits.max_input_points = 3;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.input_participants == 1);
    }

    SECTION("triangle count")
    {
        const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        irop::TetrahedralizationLimits limits;
        limits.max_input_triangles = 3;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.input_participants == 1);
    }

    SECTION("limits apply to the sum rather than each participant")
    {
        const std::array participants { irop::test::tetrahedron_mesh(),
                                        shifted(irop::test::tetrahedron_mesh(), { 3.0, 0.0, 0.0 }) };
        irop::TetrahedralizationLimits limits;
        limits.max_input_points = 7;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.input_points == 4);
    }
}

TEST_CASE("TetGen produces a project-owned tetrahedral mesh for a tetrahedron")
{
    const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
    irop::TetrahedralizationLimits exact_limits;
    exact_limits.max_participants = 1;
    exact_limits.max_input_points = 4;
    exact_limits.max_input_triangles = 4;
    exact_limits.max_output_points = 4;
    exact_limits.max_output_tetrahedra = 1;
    irop::TetrahedralizationOptions options;
    SECTION("strict default") {}
    SECTION("packing recovery opt-in") { options.omit_degenerate_single_participant_tetrahedra = true; }
    const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, exact_limits, options);

    REQUIRE(result.succeeded());
    CHECK(result.status == irop::TetrahedralizationStatus::success);
    CHECK_FALSE(result.diagnostic.empty());
    CHECK(result.work.input_participants == 1);
    CHECK(result.work.input_points == 4);
    CHECK(result.work.input_triangles == 4);
    CHECK(result.work.output_points == 4);
    CHECK(result.work.output_tetrahedra == 1);
    CHECK(result.work.omitted_single_participant_tetrahedra == 0);
    CHECK(result.mesh.participant_count == 1);
    REQUIRE(result.mesh.points.size() == participants.front().vertices.size());
    for (std::size_t index = 0; index < result.mesh.points.size(); ++index) {
        CHECK(result.mesh.points[index].x == participants.front().vertices[index].x);
        CHECK(result.mesh.points[index].y == participants.front().vertices[index].y);
        CHECK(result.mesh.points[index].z == participants.front().vertices[index].z);
    }
    CHECK(result.mesh.point_owners == std::vector<irop::ParticipantId>(4, 0));
    REQUIRE(result.mesh.tetrahedra.size() == 1);

    std::array<irop::MeshIndex, 4> indices = result.mesh.tetrahedra.front();
    std::ranges::sort(indices);
    CHECK(indices == std::array<irop::MeshIndex, 4> { 0, 1, 2, 3 });
}

TEST_CASE("tetrahedralization enforces output limits")
{
    SECTION("output point count")
    {
        const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        irop::TetrahedralizationLimits limits;
        limits.max_output_points = 3;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.input_points == 4);
        CHECK(result.work.output_points == 0);
        CHECK(result.mesh.points.empty());
    }

    SECTION("output tetrahedron count")
    {
        const std::array participants { irop::test::cube_mesh() };
        irop::TetrahedralizationLimits limits;
        limits.max_output_tetrahedra = 1;

        const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants, limits);

        CHECK(result.status == irop::TetrahedralizationStatus::resource_exhausted);
        CHECK(result.work.output_tetrahedra > 1);
        CHECK(result.mesh.tetrahedra.empty());
    }
}

TEST_CASE("TetGen preserves ordered participant point ownership")
{
    const std::array participants { irop::test::tetrahedron_mesh(),
                                    shifted(irop::test::tetrahedron_mesh(), { 3.0, 0.0, 0.0 }) };
    const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

    REQUIRE(result.succeeded());
    CHECK(result.mesh.participant_count == 2);
    REQUIRE(result.mesh.points.size() == 8);
    REQUIRE(result.mesh.point_owners.size() == result.mesh.points.size());
    CHECK(std::ranges::all_of(std::span(result.mesh.point_owners).first(4), [](const irop::ParticipantId owner) {
        return owner == 0;
    }));
    CHECK(std::ranges::all_of(std::span(result.mesh.point_owners).subspan(4), [](const irop::ParticipantId owner) {
        return owner == 1;
    }));
    CHECK(result.work.output_tetrahedra >= 2);
}

TEST_CASE("TetGen preserves the Python literal-switch point-union behavior")
{
    const std::array participants { irop::test::tetrahedron_mesh(),
                                    shifted(irop::test::tetrahedron_mesh(), { 3.0, 0.0, 0.0 }) };
    const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

    REQUIRE(result.succeeded());
    CHECK(result.diagnostic.find("O0/0Q") != std::string::npos);
    CHECK(std::ranges::any_of(result.mesh.tetrahedra, [&](const irop::Tetrahedron& tetrahedron) {
        const irop::ParticipantId first_owner = result.mesh.point_owners[static_cast<std::size_t>(tetrahedron.front())];
        return std::ranges::any_of(tetrahedron, [&](const irop::MeshIndex point_index) {
            return result.mesh.point_owners[static_cast<std::size_t>(point_index)] != first_owner;
        });
    }));
}

TEST_CASE("concurrent public tetrahedralization calls are serialized safely")
{
    const auto tetrahedralize_one = [] {
        const std::vector<irop::TriangleMesh> participants = one_tetrahedron();
        return irop::tetrahedralize_surfaces(participants);
    };

    std::future<irop::TetrahedralizationResult> first = std::async(std::launch::async, tetrahedralize_one);
    std::future<irop::TetrahedralizationResult> second = std::async(std::launch::async, tetrahedralize_one);

    CHECK(first.get().succeeded());
    CHECK(second.get().succeeded());
}

TEST_CASE("TetGen tetrahedralizes an object tetrahedron strictly inside a cube container")
{
    const std::array participants { irop::test::tetrahedron_mesh(), irop::test::cube_mesh(2.0) };
    const irop::TetrahedralizationResult result = irop::tetrahedralize_surfaces(participants);

    REQUIRE(result.succeeded());
    CHECK(result.mesh.participant_count == 2);
    REQUIRE(result.mesh.points.size() == 12);
    REQUIRE(result.mesh.point_owners.size() == result.mesh.points.size());
    CHECK(std::ranges::all_of(std::span(result.mesh.point_owners).first(4), [](const irop::ParticipantId owner) {
        return owner == 0;
    }));
    CHECK(std::ranges::all_of(std::span(result.mesh.point_owners).subspan(4), [](const irop::ParticipantId owner) {
        return owner == 1;
    }));
    REQUIRE_FALSE(result.mesh.tetrahedra.empty());
    bool has_mixed_participant_tetrahedron = false;
    for (const irop::Tetrahedron& tetrahedron : result.mesh.tetrahedra) {
        CHECK(std::ranges::all_of(tetrahedron, [&](const irop::MeshIndex point_index) {
            return point_index < result.mesh.points.size();
        }));
        const irop::ParticipantId first_owner = result.mesh.point_owners[static_cast<std::size_t>(tetrahedron.front())];
        has_mixed_participant_tetrahedron =
            has_mixed_participant_tetrahedron || std::ranges::any_of(tetrahedron, [&](const irop::MeshIndex index) {
            return result.mesh.point_owners[static_cast<std::size_t>(index)] != first_owner;
        });
    }
    CHECK(has_mixed_participant_tetrahedron);
}
