#include "irop/geometry/surface_resampling.hpp"

#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCleanPolyData.h>
#include <vtkCommand.h>
#include <vtkErrorCode.h>
#include <vtkLoopSubdivisionFilter.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkQuadricDecimation.h>
#include <vtkSmartPointer.h>
#include <vtkSmoothPolyDataFilter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/model/mesh_validation.hpp"

namespace irop {
namespace {

constexpr std::uint64_t minimum_closed_triangle_count = 4;
constexpr int smoothing_iteration_count = 10;

[[nodiscard]] MeshLimits structural_limits_for(const TriangleMesh& mesh) noexcept
{
    return {
        .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_vertices = static_cast<std::uint64_t>(mesh.vertices.size()),
        .max_triangles = static_cast<std::uint64_t>(mesh.triangles.size()),
    };
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

[[nodiscard]] double norm(const Point3& point) noexcept { return std::hypot(point.x, point.y, point.z); }

[[noreturn]] void throw_output_limit(const char* kind, const std::uint64_t actual, const std::uint64_t maximum)
{
    throw Error(ErrorCategory::resource_limit, std::string("resampled mesh ") + kind + " count " +
                                                   std::to_string(actual) + " exceeds configured limit " +
                                                   std::to_string(maximum));
}

[[nodiscard]] std::uint64_t first_count_above(const std::uint64_t maximum) noexcept
{
    return maximum == std::numeric_limits<std::uint64_t>::max() ? maximum : maximum + 1U;
}

void validate_resampling_limits(const SurfaceResamplingLimits& limits)
{
    if (limits.mesh_limits.max_vertices == 0 || limits.mesh_limits.max_triangles == 0 ||
        limits.max_subdivision_steps == 0) {
        throw Error(ErrorCategory::invalid_configuration, "surface-resampling limits must be positive");
    }
}

[[nodiscard]] std::uint64_t checked_vtk_count(const vtkIdType count, const char* kind)
{
    if (count < 0) {
        throw Error(ErrorCategory::dependency_failure, std::string("VTK returned a negative ") + kind + " count");
    }
    return static_cast<std::uint64_t>(count);
}

void validate_vtk_input_range(const TriangleMesh& mesh)
{
    const auto vtk_max = static_cast<std::uint64_t>(std::numeric_limits<vtkIdType>::max());
    if (static_cast<std::uint64_t>(mesh.vertices.size()) > vtk_max ||
        static_cast<std::uint64_t>(mesh.triangles.size()) > vtk_max) {
        throw Error(ErrorCategory::resource_limit, "surface mesh is too large for the configured VTK index type");
    }
}

[[nodiscard]] vtkSmartPointer<vtkPolyData> triangle_mesh_to_poly_data(const TriangleMesh& mesh)
{
    validate_vtk_input_range(mesh);

    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.vertices.size()));
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        const Point3& point = mesh.vertices[index];
        points->SetPoint(static_cast<vtkIdType>(index), point.x, point.y, point.z);
    }

    vtkNew<vtkCellArray> triangles;
    triangles->AllocateEstimate(static_cast<vtkIdType>(mesh.triangles.size()), 3);
    for (const Triangle& triangle : mesh.triangles) {
        const std::array<vtkIdType, 3> point_ids {
            static_cast<vtkIdType>(triangle[0]),
            static_cast<vtkIdType>(triangle[1]),
            static_cast<vtkIdType>(triangle[2]),
        };
        triangles->InsertNextCell(static_cast<vtkIdType>(point_ids.size()), point_ids.data());
    }

    vtkSmartPointer<vtkPolyData> poly_data = vtkSmartPointer<vtkPolyData>::New();
    poly_data->SetPoints(points);
    poly_data->SetPolys(triangles);
    return poly_data;
}

[[nodiscard]] TriangleMesh poly_data_to_triangle_mesh(vtkPolyData& poly_data, const MeshLimits& limits)
{
    const std::uint64_t vertex_count = checked_vtk_count(poly_data.GetNumberOfPoints(), "point");
    const std::uint64_t cell_count = checked_vtk_count(poly_data.GetNumberOfCells(), "cell");
    const std::uint64_t polygon_count = checked_vtk_count(poly_data.GetNumberOfPolys(), "polygon");
    if (vertex_count > limits.max_vertices) {
        throw_output_limit("vertex", vertex_count, limits.max_vertices);
    }
    if (cell_count > limits.max_triangles) {
        throw_output_limit("triangle", cell_count, limits.max_triangles);
    }
    if (vertex_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        cell_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw Error(ErrorCategory::resource_limit, "resampled mesh count exceeds addressable memory");
    }
    if (cell_count != polygon_count || poly_data.GetNumberOfVerts() != 0 || poly_data.GetNumberOfLines() != 0 ||
        poly_data.GetNumberOfStrips() != 0) {
        throw Error(ErrorCategory::dependency_failure, "VTK resampling did not produce a triangle-only surface");
    }

    TriangleMesh result;
    result.vertices.reserve(static_cast<std::size_t>(vertex_count));
    result.triangles.reserve(static_cast<std::size_t>(cell_count));
    std::array<double, 3> coordinates {};
    for (vtkIdType point_index = 0; point_index < poly_data.GetNumberOfPoints(); ++point_index) {
        poly_data.GetPoint(point_index, coordinates.data());
        result.vertices.push_back({ coordinates[0], coordinates[1], coordinates[2] });
    }
    for (vtkIdType cell_index = 0; cell_index < poly_data.GetNumberOfCells(); ++cell_index) {
        vtkIdType point_count = 0;
        const vtkIdType* point_ids = nullptr;
        poly_data.GetCellPoints(cell_index, point_count, point_ids);
        if (point_count != 3 || point_ids == nullptr) {
            throw Error(ErrorCategory::dependency_failure, "VTK resampling produced a non-triangular cell");
        }
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (point_ids[corner] < 0 || static_cast<std::uint64_t>(point_ids[corner]) >= vertex_count) {
                throw Error(ErrorCategory::dependency_failure, "VTK resampling produced an out-of-range point index");
            }
        }
        result.triangles.push_back({
            static_cast<MeshIndex>(point_ids[0]),
            static_cast<MeshIndex>(point_ids[1]),
            static_cast<MeshIndex>(point_ids[2]),
        });
    }
    return result;
}

void mark_vtk_error(vtkObject*, unsigned long, void* client_data, void*) { *static_cast<bool*>(client_data) = true; }

template <typename Filter>
void update_filter(Filter* filter, const char* description)
{
    bool error_observed = false;
    vtkNew<vtkCallbackCommand> observer;
    observer->SetClientData(&error_observed);
    observer->SetCallback(mark_vtk_error);
    const unsigned long observer_tag = filter->AddObserver(vtkCommand::ErrorEvent, observer);
    filter->Update();
    filter->RemoveObserver(observer_tag);
    if (error_observed || filter->GetErrorCode() != vtkErrorCode::NoError || filter->GetOutput() == nullptr) {
        throw Error(ErrorCategory::dependency_failure, std::string("VTK failed during ") + description);
    }
}

[[nodiscard]] TriangleMesh smooth_clean_and_convert(vtkPolyData* input, const MeshLimits& limits)
{
    if (input == nullptr) {
        throw Error(ErrorCategory::dependency_failure, "VTK resampling produced no surface output");
    }

    vtkNew<vtkSmoothPolyDataFilter> smoother;
    smoother->SetInputData(input);
    smoother->SetNumberOfIterations(smoothing_iteration_count);
    smoother->FeatureEdgeSmoothingOff();
    smoother->BoundarySmoothingOn();
    update_filter(smoother.GetPointer(), "surface smoothing");

    vtkNew<vtkCleanPolyData> cleaner;
    cleaner->SetInputConnection(smoother->GetOutputPort());
    cleaner->PointMergingOn();
    cleaner->ConvertLinesToPointsOff();
    cleaner->ConvertPolysToLinesOff();
    cleaner->ConvertStripsToPolysOff();
    update_filter(cleaner.GetPointer(), "surface cleaning");
    return poly_data_to_triangle_mesh(*cleaner->GetOutput(), limits);
}

struct SubdivisionPlan {
    std::uint64_t steps = 0;
    std::uint64_t final_vertices = 0;
    std::uint64_t final_triangles = 0;
};

[[nodiscard]] SubdivisionPlan plan_subdivision(const MeshStatistics& statistics,
                                               const std::uint64_t target_triangle_count,
                                               const SurfaceResamplingLimits& limits)
{
    SubdivisionPlan plan {
        .steps = 0,
        .final_vertices = statistics.vertex_count,
        .final_triangles = statistics.triangle_count,
    };
    while (plan.final_triangles < target_triangle_count) {
        if (plan.steps >= limits.max_subdivision_steps) {
            throw Error(ErrorCategory::resource_limit, "surface subdivision exceeds the configured step limit");
        }
        if ((plan.final_triangles % 2U) != 0U ||
            plan.final_triangles > std::numeric_limits<std::uint64_t>::max() / 3U) {
            throw Error(ErrorCategory::invalid_mesh,
                        "closed triangular surface has an invalid edge-count relationship");
        }
        const std::uint64_t new_edge_vertices = (plan.final_triangles * 3U) / 2U;
        if (new_edge_vertices > limits.mesh_limits.max_vertices - plan.final_vertices) {
            throw_output_limit("vertex", first_count_above(limits.mesh_limits.max_vertices),
                               limits.mesh_limits.max_vertices);
        }
        if (plan.final_triangles > limits.mesh_limits.max_triangles / 4U) {
            throw_output_limit("triangle", first_count_above(limits.mesh_limits.max_triangles),
                               limits.mesh_limits.max_triangles);
        }
        plan.final_vertices += new_edge_vertices;
        plan.final_triangles *= 4U;
        ++plan.steps;
    }

    const auto vtk_max = static_cast<std::uint64_t>(std::numeric_limits<vtkIdType>::max());
    if (plan.final_vertices > vtk_max || plan.final_triangles > vtk_max) {
        throw Error(ErrorCategory::resource_limit, "subdivided surface exceeds the configured VTK index range");
    }
    return plan;
}

[[nodiscard]] TriangleMesh subdivide_without_moving_surface(const TriangleMesh& mesh, const SubdivisionPlan& plan)
{
    // DEVIATION(IROP-DEV-0034): Retain the input boundary and its double precision.
    // VTK linear subdivision stores output points as float, while Loop subdivision
    // and smoothing also move the boundary. See docs/COMPATIBILITY.md.
    TriangleMesh result = mesh;
    if (plan.final_vertices > result.vertices.max_size() || plan.final_triangles > result.triangles.max_size()) {
        throw Error(ErrorCategory::resource_limit, "surface subdivision exceeds addressable memory");
    }
    result.vertices.reserve(static_cast<std::size_t>(plan.final_vertices));
    for (std::uint64_t step = 0; step < plan.steps; ++step) {
        std::map<std::pair<MeshIndex, MeshIndex>, MeshIndex> edge_midpoints;
        std::vector<Triangle> triangles;
        triangles.reserve(result.triangles.size() * 4U);
        const auto midpoint_index = [&](const MeshIndex first_index, const MeshIndex second_index) {
            const std::pair<MeshIndex, MeshIndex> edge { std::min(first_index, second_index),
                                                         std::max(first_index, second_index) };
            const auto existing = edge_midpoints.find(edge);
            if (existing != edge_midpoints.end()) {
                return existing->second;
            }
            if (result.vertices.size() >= plan.final_vertices) {
                throw Error(ErrorCategory::invalid_mesh, "surface subdivision exceeded the preflight vertex count");
            }
            const Point3& first = result.vertices[static_cast<std::size_t>(first_index)];
            const Point3& second = result.vertices[static_cast<std::size_t>(second_index)];
            const Point3 midpoint {
                std::midpoint(first.x, second.x),
                std::midpoint(first.y, second.y),
                std::midpoint(first.z, second.z),
            };
            const MeshIndex index = static_cast<MeshIndex>(result.vertices.size());
            result.vertices.push_back(midpoint);
            edge_midpoints.emplace(edge, index);
            return index;
        };
        for (const Triangle& triangle : result.triangles) {
            const MeshIndex last_first = midpoint_index(triangle[2], triangle[0]);
            const MeshIndex first_second = midpoint_index(triangle[0], triangle[1]);
            const MeshIndex second_last = midpoint_index(triangle[1], triangle[2]);
            triangles.push_back({ triangle[0], first_second, last_first });
            triangles.push_back({ first_second, triangle[1], second_last });
            triangles.push_back({ second_last, triangle[2], last_first });
            triangles.push_back({ first_second, second_last, last_first });
        }
        result.triangles = std::move(triangles);
    }
    return result;
}

[[nodiscard]] TriangleMesh resample_with_vtk(const TriangleMesh& mesh, const std::uint64_t target_triangle_count,
                                             const SurfaceResamplingLimits& limits, std::uint64_t& subdivision_steps)
{
    vtkSmartPointer<vtkPolyData> input = triangle_mesh_to_poly_data(mesh);
    const std::uint64_t original_triangle_count = static_cast<std::uint64_t>(mesh.triangles.size());
    if (target_triangle_count < original_triangle_count) {
        const double target_reduction =
            1.0 - static_cast<double>(target_triangle_count) / static_cast<double>(original_triangle_count);
        if (!std::isfinite(target_reduction) || target_reduction < 0.0 || target_reduction > 1.0) {
            throw Error(ErrorCategory::invalid_configuration, "surface decimation target is not representable");
        }

        vtkNew<vtkQuadricDecimation> decimator;
        decimator->SetInputData(input);
        decimator->SetTargetReduction(target_reduction);
        decimator->VolumePreservationOn();
        update_filter(decimator.GetPointer(), "surface decimation");
        return smooth_clean_and_convert(decimator->GetOutput(), limits.mesh_limits);
    }

    const MeshStatistics statistics = validate_and_measure_mesh(mesh, limits.mesh_limits);
    const SubdivisionPlan plan = plan_subdivision(statistics, target_triangle_count, limits);
    subdivision_steps = plan.steps;
    if (plan.steps > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw Error(ErrorCategory::resource_limit, "surface subdivision step count exceeds the VTK range");
    }

    vtkNew<vtkLoopSubdivisionFilter> subdivider;
    subdivider->SetInputData(input);
    subdivider->SetNumberOfSubdivisions(static_cast<int>(plan.steps));
    update_filter(subdivider.GetPointer(), "surface subdivision");
    return smooth_clean_and_convert(subdivider->GetOutput(), limits.mesh_limits);
}

[[nodiscard]] SurfaceResamplingResult resample_closed_surface_impl(const TriangleMesh& mesh,
                                                                   const std::uint64_t target_triangle_count,
                                                                   const SurfaceResamplingLimits& limits,
                                                                   const SurfaceResamplingMode mode)
{
    validate_resampling_limits(limits);
    if (mode != SurfaceResamplingMode::reference && mode != SurfaceResamplingMode::preserve_surface) {
        throw Error(ErrorCategory::invalid_configuration, "surface-resampling mode is not recognized");
    }
    if (target_triangle_count < minimum_closed_triangle_count) {
        throw Error(ErrorCategory::invalid_configuration,
                    "surface-resampling target must contain at least four triangles");
    }
    if (target_triangle_count > limits.mesh_limits.max_triangles) {
        throw_output_limit("triangle", target_triangle_count, limits.mesh_limits.max_triangles);
    }

    const MeshStatistics input_statistics = validate_and_measure_mesh(mesh, limits.mesh_limits);
    static_cast<void>(ClosedMeshQuery(mesh));
    if (target_triangle_count == input_statistics.triangle_count ||
        (mode == SurfaceResamplingMode::preserve_surface && target_triangle_count < input_statistics.triangle_count)) {
        return {
            .mesh = mesh,
            .requested_triangle_count = target_triangle_count,
            .actual_triangle_count = input_statistics.triangle_count,
            .subdivision_steps = 0,
            .changed = false,
        };
    }

    std::uint64_t subdivision_steps = 0;
    TriangleMesh resampled;
    if (mode == SurfaceResamplingMode::preserve_surface) {
        const SubdivisionPlan plan = plan_subdivision(input_statistics, target_triangle_count, limits);
        subdivision_steps = plan.steps;
        resampled = subdivide_without_moving_surface(mesh, plan);
    }
    else {
        // DEVIATION(IROP-DEV-0020): Use the pinned VTK resampling path instead of
        // the Python implementation's Trimesh-first decimation fallback chain.
        // See docs/COMPATIBILITY.md.
        resampled = resample_with_vtk(mesh, target_triangle_count, limits, subdivision_steps);
    }
    const MeshStatistics output_statistics = validate_and_measure_mesh(resampled, limits.mesh_limits);
    try {
        static_cast<void>(ClosedMeshQuery(resampled));
    }
    catch (const Error& error) {
        if (error.category() == ErrorCategory::resource_limit) {
            throw;
        }
        throw Error(ErrorCategory::dependency_failure, "surface resampling produced an invalid closed surface");
    }

    if (target_triangle_count > input_statistics.triangle_count &&
        output_statistics.triangle_count < target_triangle_count) {
        throw Error(ErrorCategory::dependency_failure, "surface subdivision did not reach the requested surface count");
    }
    if (target_triangle_count < input_statistics.triangle_count &&
        output_statistics.triangle_count >= input_statistics.triangle_count) {
        throw Error(ErrorCategory::dependency_failure, "VTK decimation did not reduce the surface triangle count");
    }
    return {
        .mesh = std::move(resampled),
        .requested_triangle_count = target_triangle_count,
        .actual_triangle_count = output_statistics.triangle_count,
        .subdivision_steps = subdivision_steps,
        .changed = true,
    };
}

}  // namespace

double average_triangle_area(const TriangleMesh& mesh)
{
    static_cast<void>(validate_and_measure_mesh(mesh, structural_limits_for(mesh)));
    long double total_area = 0.0L;
    for (const Triangle& triangle : mesh.triangles) {
        const Point3& first = mesh.vertices[static_cast<std::size_t>(triangle[0])];
        const Point3& second = mesh.vertices[static_cast<std::size_t>(triangle[1])];
        const Point3& third = mesh.vertices[static_cast<std::size_t>(triangle[2])];
        const double area = 0.5 * norm(cross(subtract(second, first), subtract(third, first)));
        if (!std::isfinite(area) || area <= 0.0) {
            throw Error(ErrorCategory::invalid_mesh, "mesh triangle area is not finite and positive");
        }
        total_area += static_cast<long double>(area);
    }
    const long double average = total_area / static_cast<long double>(mesh.triangles.size());
    if (!std::isfinite(average) || average <= 0.0L ||
        average > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw Error(ErrorCategory::invalid_mesh, "mesh average triangle area is not representable");
    }
    return static_cast<double>(average);
}

std::uint64_t target_container_triangle_count(const TriangleMesh& sampled_object,
                                              const TriangleMesh& original_container,
                                              const std::uint64_t refinement_factor,
                                              const std::uint64_t minimum_triangle_count)
{
    if (refinement_factor == 0 || minimum_triangle_count < minimum_closed_triangle_count) {
        throw Error(ErrorCategory::invalid_configuration,
                    "container surface target factor must be positive and its minimum at least four");
    }

    const double object_area = average_triangle_area(sampled_object);
    const double container_area = average_triangle_area(original_container);
    const double unmultiplied_target =
        static_cast<double>(original_container.triangles.size()) * (container_area / object_area);
    const double uint64_upper_exclusive = std::ldexp(1.0, std::numeric_limits<std::uint64_t>::digits);
    if (!std::isfinite(unmultiplied_target) || unmultiplied_target < 0.0 ||
        unmultiplied_target >= uint64_upper_exclusive) {
        throw Error(ErrorCategory::resource_limit, "container surface target count is not representable");
    }

    // Match Python's int(...) truncation before applying the integer factor.
    const std::uint64_t truncated = static_cast<std::uint64_t>(unmultiplied_target);
    if (truncated != 0 && refinement_factor > std::numeric_limits<std::uint64_t>::max() / truncated) {
        throw Error(ErrorCategory::resource_limit, "container surface target count overflows");
    }
    return std::max(minimum_triangle_count, truncated * refinement_factor);
}

SurfaceResamplingResult resample_closed_surface(const TriangleMesh& mesh, const std::uint64_t target_triangle_count,
                                                const SurfaceResamplingLimits& limits, const SurfaceResamplingMode mode)
{
    try {
        return resample_closed_surface_impl(mesh, target_triangle_count, limits, mode);
    }
    catch (const Error&) {
        throw;
    }
    catch (const std::bad_alloc&) {
        throw Error(ErrorCategory::resource_limit, "surface resampling could not allocate bounded work");
    }
    catch (const std::length_error&) {
        throw Error(ErrorCategory::resource_limit, "surface resampling exceeds addressable memory");
    }
    catch (const std::exception&) {
        throw Error(ErrorCategory::dependency_failure, "surface resampling failed unexpectedly");
    }
    catch (...) {
        throw Error(ErrorCategory::dependency_failure, "surface resampling failed with an unknown error");
    }
}

}  // namespace irop
