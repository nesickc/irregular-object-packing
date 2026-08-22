#include "irop/io/diagnostic_geometry.hpp"

#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkCommand.h>
#include <vtkErrorCode.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedIntArray.h>
#include <vtkUnsignedLongLongArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLUnstructuredGridWriter.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "irop/error.hpp"

namespace irop {
namespace {

#ifdef _WIN32
class ScopedWindowsHandle final {
public:
    explicit ScopedWindowsHandle(const HANDLE handle) noexcept : handle_(handle) {}

    ScopedWindowsHandle(const ScopedWindowsHandle&) = delete;
    ScopedWindowsHandle& operator=(const ScopedWindowsHandle&) = delete;
    ScopedWindowsHandle(ScopedWindowsHandle&& other) noexcept :
        handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
    {
    }
    ScopedWindowsHandle& operator=(ScopedWindowsHandle&&) = delete;

    ~ScopedWindowsHandle() { close(); }

    [[nodiscard]] bool is_valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void close() noexcept
    {
        if (is_valid()) {
            static_cast<void>(CloseHandle(handle_));
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

[[nodiscard]] ScopedWindowsHandle reserve_output_path(const std::filesystem::path& path)
{
    ScopedWindowsHandle handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.is_valid()) {
        throw Error(ErrorCategory::output_io, "failed to reserve a new diagnostic output path (Windows error " +
                                                  std::to_string(GetLastError()) + ")");
    }
    return handle;
}
#endif

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] Point3 subtract(const Point3& left, const Point3& right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Point3 cross(const Point3& left, const Point3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double dot(const Point3& left, const Point3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] bool is_nondegenerate(const Tetrahedron& tetrahedron, const std::vector<Point3>& points) noexcept
{
    const Point3& first = points[static_cast<std::size_t>(tetrahedron[0])];
    const Point3& second = points[static_cast<std::size_t>(tetrahedron[1])];
    const Point3& third = points[static_cast<std::size_t>(tetrahedron[2])];
    const Point3& fourth = points[static_cast<std::size_t>(tetrahedron[3])];
    const double signed_volume_factor =
        dot(subtract(second, first), cross(subtract(third, first), subtract(fourth, first)));
    return std::isfinite(signed_volume_factor) && signed_volume_factor != 0.0;
}

[[nodiscard]] std::filesystem::path resolve_output_path(const std::filesystem::path& path)
{
    if (path.filename().empty()) {
        throw Error(ErrorCategory::output_io, "diagnostic output path must include a filename");
    }

    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        std::error_code current_error;
        parent = std::filesystem::current_path(current_error);
        if (current_error) {
            throw Error(ErrorCategory::output_io, "failed to resolve the current output directory");
        }
    }

    std::error_code error;
    parent = std::filesystem::canonical(parent, error);
    if (error || !std::filesystem::is_directory(parent, error)) {
        throw Error(ErrorCategory::output_io, "diagnostic output directory does not exist or cannot be resolved");
    }
    return parent / path.filename();
}

[[nodiscard]] std::string path_for_vtk(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

void mark_vtk_error(vtkObject*, unsigned long, void* client_data, void*) { *static_cast<bool*>(client_data) = true; }

void validate_vtk_counts(const std::uint64_t points, const std::uint64_t cells)
{
    const auto vtk_max = static_cast<std::uint64_t>(std::numeric_limits<vtkIdType>::max());
    if (points > vtk_max || cells > vtk_max) {
        throw Error(ErrorCategory::resource_limit, "diagnostic geometry exceeds the configured VTK index range");
    }
}

void validate_tetrahedral_mesh(const TetrahedralMesh& mesh)
{
    validate_vtk_counts(static_cast<std::uint64_t>(mesh.points.size()),
                        static_cast<std::uint64_t>(mesh.tetrahedra.size()));
    if (mesh.participant_count == 0) {
        throw Error(ErrorCategory::invalid_mesh, "tetrahedral diagnostic geometry has no participants");
    }
    if (mesh.points.empty() || mesh.tetrahedra.empty()) {
        throw Error(ErrorCategory::invalid_mesh, "tetrahedral diagnostic geometry must contain points and tetrahedra");
    }
    if (mesh.point_owners.size() != mesh.points.size()) {
        throw Error(ErrorCategory::invalid_mesh,
                    "tetrahedral diagnostic point ownership does not match the point count");
    }

    for (std::size_t index = 0; index < mesh.points.size(); ++index) {
        if (!is_finite(mesh.points[index])) {
            throw Error(ErrorCategory::invalid_mesh, "tetrahedral diagnostic geometry contains a non-finite point");
        }
        if (static_cast<std::uint64_t>(mesh.point_owners[index]) >= mesh.participant_count) {
            throw Error(ErrorCategory::invalid_mesh, "tetrahedral diagnostic geometry contains an invalid point owner");
        }
    }

    for (const Tetrahedron& tetrahedron : mesh.tetrahedra) {
        for (const MeshIndex point : tetrahedron) {
            if (point >= mesh.points.size()) {
                throw Error(ErrorCategory::invalid_mesh,
                            "tetrahedral diagnostic geometry contains an out-of-range point index");
            }
        }
        if (tetrahedron[0] == tetrahedron[1] || tetrahedron[0] == tetrahedron[2] || tetrahedron[0] == tetrahedron[3] ||
            tetrahedron[1] == tetrahedron[2] || tetrahedron[1] == tetrahedron[3] || tetrahedron[2] == tetrahedron[3] ||
            !is_nondegenerate(tetrahedron, mesh.points)) {
            throw Error(ErrorCategory::invalid_mesh,
                        "tetrahedral diagnostic geometry contains a degenerate tetrahedron");
        }
    }
}

[[nodiscard]] std::vector<std::uint64_t> validate_cat(const CatConstructionResult& cat)
{
    if (!cat.succeeded()) {
        throw Error(ErrorCategory::invalid_mesh, "CAT diagnostic geometry requires a successful construction result");
    }

    std::uint64_t point_count = 0;
    for (const CatPolygon& polygon : cat.polygons) {
        if (polygon.vertex_count != 3 && polygon.vertex_count != 4) {
            throw Error(ErrorCategory::invalid_mesh,
                        "CAT diagnostic geometry contains a polygon with an invalid vertex count");
        }
        if (polygon.owner >= cat.participant_ranges.size()) {
            throw Error(ErrorCategory::invalid_mesh, "CAT diagnostic geometry contains an invalid polygon owner");
        }
        if (point_count > std::numeric_limits<std::uint64_t>::max() - polygon.vertex_count) {
            throw Error(ErrorCategory::resource_limit, "CAT diagnostic point count overflows the project-owned range");
        }
        point_count += polygon.vertex_count;
        for (std::uint8_t corner = 0; corner < polygon.vertex_count; ++corner) {
            if (!is_finite(polygon.vertices[corner])) {
                throw Error(ErrorCategory::invalid_mesh,
                            "CAT diagnostic geometry contains a non-finite polygon vertex");
            }
        }
    }
    validate_vtk_counts(point_count, static_cast<std::uint64_t>(cat.polygons.size()));

    std::uint64_t expected_polygon_begin = 0;
    std::uint64_t expected_constraint_begin = 0;
    for (std::size_t owner = 0; owner < cat.participant_ranges.size(); ++owner) {
        const CatParticipantRange& range = cat.participant_ranges[owner];
        if (range.polygon_begin != expected_polygon_begin || range.constraint_begin != expected_constraint_begin ||
            range.polygon_count > cat.polygons.size() - expected_polygon_begin ||
            range.constraint_count > cat.constraints.size() - expected_constraint_begin) {
            throw Error(ErrorCategory::invalid_mesh,
                        "CAT diagnostic participant ranges are not contiguous and complete");
        }
        expected_polygon_begin += range.polygon_count;
        expected_constraint_begin += range.constraint_count;

        for (std::uint64_t index = range.polygon_begin; index < expected_polygon_begin; ++index) {
            if (cat.polygons[static_cast<std::size_t>(index)].owner != owner) {
                throw Error(ErrorCategory::invalid_mesh,
                            "CAT diagnostic polygon ownership disagrees with participant ranges");
            }
        }
        for (std::uint64_t index = range.constraint_begin; index < expected_constraint_begin; ++index) {
            const CatPlaneConstraint& constraint = cat.constraints[static_cast<std::size_t>(index)];
            if (constraint.owner != owner || constraint.polygon >= cat.polygons.size() ||
                !is_finite(constraint.plane_point) || !is_finite(constraint.inward_unit_normal)) {
                throw Error(ErrorCategory::invalid_mesh,
                            "CAT diagnostic geometry contains an invalid plane constraint");
            }
            const CatPolygon& polygon = cat.polygons[static_cast<std::size_t>(constraint.polygon)];
            if (polygon.owner != constraint.owner || polygon.tetrahedron != constraint.tetrahedron) {
                throw Error(ErrorCategory::invalid_mesh,
                            "CAT diagnostic plane constraint disagrees with its referenced polygon");
            }
        }
    }
    if (expected_polygon_begin != cat.polygons.size() || expected_constraint_begin != cat.constraints.size()) {
        throw Error(ErrorCategory::invalid_mesh, "CAT diagnostic participant ranges do not cover all output geometry");
    }

    std::vector<std::uint64_t> constraint_counts(cat.polygons.size(), 0);
    for (const CatPlaneConstraint& constraint : cat.constraints) {
        ++constraint_counts[static_cast<std::size_t>(constraint.polygon)];
    }
    return constraint_counts;
}

template <typename Writer, typename DataSet>
void write_vtk_xml(const std::filesystem::path& output_path, DataSet* data_set, const char* format_name)
{
    const std::filesystem::path stable_output_path = resolve_output_path(output_path);
#ifdef _WIN32
    ScopedWindowsHandle output_reservation = reserve_output_path(stable_output_path);
#else
    std::error_code existing_error;
    const std::filesystem::file_status existing_status =
        std::filesystem::symlink_status(stable_output_path, existing_error);
    if (existing_error || existing_status.type() != std::filesystem::file_type::not_found) {
        throw Error(ErrorCategory::output_io, "diagnostic output path already exists or cannot be inspected");
    }
#endif

    try {
        vtkNew<Writer> writer;
        bool vtk_error_observed = false;
        vtkNew<vtkCallbackCommand> error_observer;
        error_observer->SetClientData(&vtk_error_observed);
        error_observer->SetCallback(mark_vtk_error);
        const unsigned long observer_tag = writer->AddObserver(vtkCommand::ErrorEvent, error_observer);
        const std::string vtk_path = path_for_vtk(stable_output_path);
        writer->SetFileName(vtk_path.c_str());
        writer->SetDataModeToBinary();
        writer->SetInputData(data_set);
        const int write_succeeded = writer->Write();
        writer->RemoveObserver(observer_tag);
        if (write_succeeded == 0 || vtk_error_observed || writer->GetErrorCode() != vtkErrorCode::NoError) {
            throw Error(ErrorCategory::output_io, std::string("VTK failed to write ") + format_name + ": " +
                                                      vtkErrorCode::GetStringFromErrorCode(writer->GetErrorCode()));
        }

        std::error_code output_error;
        const std::uintmax_t output_size = std::filesystem::file_size(stable_output_path, output_error);
        if (output_error || output_size == 0) {
            throw Error(ErrorCategory::output_io, std::string("VTK produced an empty or inaccessible ") + format_name);
        }
#ifdef _WIN32
        output_reservation.close();
#endif
    }
    catch (...) {
#ifdef _WIN32
        output_reservation.close();
#endif
        std::error_code remove_error;
        static_cast<void>(std::filesystem::remove(stable_output_path, remove_error));
        throw;
    }
}

}  // namespace

void write_tetrahedralization_vtu(const std::filesystem::path& output_path, const TetrahedralMesh& mesh)
{
    validate_tetrahedral_mesh(mesh);

    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.points.size()));
    for (std::size_t index = 0; index < mesh.points.size(); ++index) {
        const Point3& point = mesh.points[index];
        points->SetPoint(static_cast<vtkIdType>(index), point.x, point.y, point.z);
    }

    vtkNew<vtkUnsignedIntArray> participant_ids;
    participant_ids->SetName("participant_id");
    participant_ids->SetNumberOfValues(static_cast<vtkIdType>(mesh.point_owners.size()));
    for (std::size_t index = 0; index < mesh.point_owners.size(); ++index) {
        participant_ids->SetValue(static_cast<vtkIdType>(index), mesh.point_owners[index]);
    }

    vtkNew<vtkUnsignedLongLongArray> tetrahedron_ids;
    tetrahedron_ids->SetName("tetrahedron_id");
    tetrahedron_ids->SetNumberOfValues(static_cast<vtkIdType>(mesh.tetrahedra.size()));

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(points);
    grid->Allocate(static_cast<vtkIdType>(mesh.tetrahedra.size()));
    for (std::size_t index = 0; index < mesh.tetrahedra.size(); ++index) {
        const Tetrahedron& tetrahedron = mesh.tetrahedra[index];
        const std::array<vtkIdType, 4> point_ids {
            static_cast<vtkIdType>(tetrahedron[0]),
            static_cast<vtkIdType>(tetrahedron[1]),
            static_cast<vtkIdType>(tetrahedron[2]),
            static_cast<vtkIdType>(tetrahedron[3]),
        };
        grid->InsertNextCell(VTK_TETRA, static_cast<vtkIdType>(point_ids.size()), point_ids.data());
        tetrahedron_ids->SetValue(static_cast<vtkIdType>(index), static_cast<unsigned long long>(index));
    }
    grid->GetPointData()->AddArray(participant_ids);
    grid->GetCellData()->AddArray(tetrahedron_ids);

    write_vtk_xml<vtkXMLUnstructuredGridWriter>(output_path, grid.GetPointer(), "VTU diagnostic geometry");
}

void write_cat_vtp(const std::filesystem::path& output_path, const CatConstructionResult& cat)
{
    const std::vector<std::uint64_t> constraint_counts = validate_cat(cat);

    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    vtkNew<vtkCellArray> polygons;
    polygons->AllocateEstimate(static_cast<vtkIdType>(cat.polygons.size()), 4);

    vtkNew<vtkUnsignedIntArray> participant_ids;
    participant_ids->SetName("participant_id");
    vtkNew<vtkUnsignedLongLongArray> tetrahedron_ids;
    tetrahedron_ids->SetName("tetrahedron_id");
    vtkNew<vtkUnsignedLongLongArray> polygon_ids;
    polygon_ids->SetName("polygon_id");
    vtkNew<vtkUnsignedLongLongArray> polygon_constraint_counts;
    polygon_constraint_counts->SetName("constraint_count");

    for (std::size_t polygon_index = 0; polygon_index < cat.polygons.size(); ++polygon_index) {
        const CatPolygon& polygon = cat.polygons[polygon_index];
        std::array<vtkIdType, 4> point_ids {};
        for (std::uint8_t corner = 0; corner < polygon.vertex_count; ++corner) {
            const Point3& point = polygon.vertices[corner];
            point_ids[corner] = points->InsertNextPoint(point.x, point.y, point.z);
        }
        polygons->InsertNextCell(static_cast<vtkIdType>(polygon.vertex_count), point_ids.data());
        participant_ids->InsertNextValue(polygon.owner);
        tetrahedron_ids->InsertNextValue(static_cast<unsigned long long>(polygon.tetrahedron));
        polygon_ids->InsertNextValue(static_cast<unsigned long long>(polygon_index));
        polygon_constraint_counts->InsertNextValue(static_cast<unsigned long long>(constraint_counts[polygon_index]));
    }

    vtkNew<vtkPolyData> poly_data;
    poly_data->SetPoints(points);
    poly_data->SetPolys(polygons);
    poly_data->GetCellData()->AddArray(participant_ids);
    poly_data->GetCellData()->AddArray(tetrahedron_ids);
    poly_data->GetCellData()->AddArray(polygon_ids);
    poly_data->GetCellData()->AddArray(polygon_constraint_counts);

    write_vtk_xml<vtkXMLPolyDataWriter>(output_path, poly_data.GetPointer(), "VTP diagnostic geometry");
}

}  // namespace irop
