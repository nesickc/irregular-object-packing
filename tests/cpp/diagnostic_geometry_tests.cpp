#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <limits>

#include "irop/cat/cat.hpp"
#include "irop/error.hpp"
#include "irop/io/diagnostic_geometry.hpp"
#include "irop/tetrahedralization/tetrahedralization.hpp"
#include "support/test_support.hpp"

namespace {

[[nodiscard]] irop::TetrahedralMesh diagnostic_tetrahedral_mesh()
{
    return {
        .points = {
            { 0.0, 0.0, 0.0 },
            { 1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 1.0 },
        },
        .tetrahedra = { { 0, 1, 2, 3 } },
        .point_owners = { 0, 0, 1, 1 },
        .participant_count = 2,
    };
}

[[nodiscard]] irop::CatConstructionResult diagnostic_cat()
{
    return {
        .status = irop::CatConstructionStatus::success,
        .polygons = {
            {
                .vertices = {
                    irop::Point3 { 0.0, 0.0, 0.0 },
                    irop::Point3 { 1.0, 0.0, 0.0 },
                    irop::Point3 { 0.0, 1.0, 0.0 },
                    irop::Point3 {},
                },
                .vertex_count = 3,
                .owner = 0,
                .tetrahedron = 7,
            },
        },
        .constraints = {
            {
                .owner = 0,
                .source_point = 0,
                .plane_point = { 0.0, 0.0, 0.0 },
                .inward_unit_normal = { 0.0, 0.0, 1.0 },
                .polygon = 0,
                .tetrahedron = 7,
            },
        },
        .participant_ranges = {
            {
                .polygon_begin = 0,
                .polygon_count = 1,
                .constraint_begin = 0,
                .constraint_count = 1,
            },
            {
                .polygon_begin = 1,
                .polygon_count = 0,
                .constraint_begin = 1,
                .constraint_count = 0,
            },
        },
    };
}

}  // namespace

TEST_CASE("tetrahedral diagnostics write readable VTU geometry and ownership metadata")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "tetrahedralization.vtu";

    irop::write_tetrahedralization_vtu(output, diagnostic_tetrahedral_mesh());

    vtkNew<vtkXMLUnstructuredGridReader> reader;
    reader->SetFileName(output.string().c_str());
    reader->Update();
    vtkUnstructuredGrid* grid = reader->GetOutput();
    REQUIRE(grid != nullptr);
    CHECK(grid->GetNumberOfPoints() == 4);
    CHECK(grid->GetNumberOfCells() == 1);
    REQUIRE(grid->GetPointData()->GetArray("participant_id") != nullptr);
    CHECK(grid->GetPointData()->GetArray("participant_id")->GetTuple1(2) == 1.0);
    REQUIRE(grid->GetCellData()->GetArray("tetrahedron_id") != nullptr);
    CHECK(grid->GetCellData()->GetArray("tetrahedron_id")->GetTuple1(0) == 0.0);
}

TEST_CASE("CAT diagnostics write readable VTP polygons and source metadata")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "cat.vtp";

    irop::write_cat_vtp(output, diagnostic_cat());

    vtkNew<vtkXMLPolyDataReader> reader;
    reader->SetFileName(output.string().c_str());
    reader->Update();
    vtkPolyData* poly_data = reader->GetOutput();
    REQUIRE(poly_data != nullptr);
    CHECK(poly_data->GetNumberOfPoints() == 3);
    CHECK(poly_data->GetNumberOfPolys() == 1);
    REQUIRE(poly_data->GetCellData()->GetArray("participant_id") != nullptr);
    CHECK(poly_data->GetCellData()->GetArray("participant_id")->GetTuple1(0) == 0.0);
    REQUIRE(poly_data->GetCellData()->GetArray("tetrahedron_id") != nullptr);
    CHECK(poly_data->GetCellData()->GetArray("tetrahedron_id")->GetTuple1(0) == 7.0);
    REQUIRE(poly_data->GetCellData()->GetArray("constraint_count") != nullptr);
    CHECK(poly_data->GetCellData()->GetArray("constraint_count")->GetTuple1(0) == 1.0);
}

TEST_CASE("diagnostic writers reject overwrite and malformed project geometry")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "tetrahedralization.vtu";
    const irop::TetrahedralMesh valid = diagnostic_tetrahedral_mesh();
    irop::write_tetrahedralization_vtu(output, valid);

    irop::test::require_error_category([&]() {
        irop::write_tetrahedralization_vtu(output, valid);
    }, irop::ErrorCategory::output_io);

    irop::TetrahedralMesh invalid = valid;
    invalid.points[0].x = std::numeric_limits<double>::quiet_NaN();
    irop::test::require_error_category([&]() {
        irop::write_tetrahedralization_vtu(temporary.path() / "invalid.vtu", invalid);
    }, irop::ErrorCategory::invalid_mesh);

    irop::CatConstructionResult failed = diagnostic_cat();
    failed.status = irop::CatConstructionStatus::invalid_input;
    irop::test::require_error_category([&]() {
        irop::write_cat_vtp(temporary.path() / "invalid.vtp", failed);
    }, irop::ErrorCategory::invalid_mesh);

    irop::CatConstructionResult wrong_tetrahedron = diagnostic_cat();
    wrong_tetrahedron.constraints[0].tetrahedron = 8;
    irop::test::require_error_category([&]() {
        irop::write_cat_vtp(temporary.path() / "wrong-tetrahedron.vtp", wrong_tetrahedron);
    }, irop::ErrorCategory::invalid_mesh);

    irop::CatConstructionResult wrong_owner = diagnostic_cat();
    irop::CatPolygon second_owner_polygon = wrong_owner.polygons[0];
    second_owner_polygon.owner = 1;
    wrong_owner.polygons.push_back(second_owner_polygon);
    wrong_owner.participant_ranges[1].polygon_count = 1;
    wrong_owner.constraints[0].polygon = 1;
    irop::test::require_error_category([&]() {
        irop::write_cat_vtp(temporary.path() / "wrong-owner.vtp", wrong_owner);
    }, irop::ErrorCategory::invalid_mesh);
}
