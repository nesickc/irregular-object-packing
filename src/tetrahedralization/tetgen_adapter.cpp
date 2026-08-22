#include <tetgen.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <utility>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/tetrahedralization/tetrahedralization.hpp"

namespace irop {
namespace {

// COMPATIBILITY(IROP-COMPAT-0005): The Python reference passes the
// non-empty switch string "O0/0Q", which makes its TetGen wrapper bypass
// cdt=True, steinerleft=0, and the other keyword arguments. Preserve the
// resulting point-union tetrahedralization until post-parity review.
// See docs/COMPATIBILITY.md.
constexpr char tetgen_switches[] = "O0/0Q";

[[nodiscard]] TetrahedralizationResult failure(const TetrahedralizationStatus status, TetrahedralizationWork work,
                                               std::string diagnostic)
{
    return {
        .status = status,
        .mesh = {},
        .work = work,
        .diagnostic = std::move(diagnostic),
    };
}

[[nodiscard]] bool limits_are_positive(const TetrahedralizationLimits& limits) noexcept
{
    return limits.max_participants > 0 && limits.max_input_points > 0 && limits.max_input_triangles > 0 &&
           limits.max_output_points > 0 && limits.max_output_tetrahedra > 0;
}

[[nodiscard]] bool checked_accumulate(const std::uint64_t value, const std::uint64_t maximum,
                                      std::uint64_t& total) noexcept
{
    if (value > maximum - total) {
        return false;
    }
    total += value;
    return true;
}

[[nodiscard]] bool same_point(const Point3& expected, const REAL* actual) noexcept
{
    return expected.x == actual[0] && expected.y == actual[1] && expected.z == actual[2];
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

[[nodiscard]] bool tetrahedron_is_nondegenerate(const Tetrahedron& tetrahedron,
                                                const std::vector<Point3>& points) noexcept
{
    const Point3& first = points[static_cast<std::size_t>(tetrahedron[0])];
    const Point3& second = points[static_cast<std::size_t>(tetrahedron[1])];
    const Point3& third = points[static_cast<std::size_t>(tetrahedron[2])];
    const Point3& fourth = points[static_cast<std::size_t>(tetrahedron[3])];
    const double signed_six_times_volume =
        dot(subtract(second, first), cross(subtract(third, first), subtract(fourth, first)));
    return std::isfinite(signed_six_times_volume) && signed_six_times_volume != 0.0;
}

[[nodiscard]] TetrahedralizationStatus status_for_tetgen_code(const int code) noexcept
{
    switch (code) {
    case 1:
        return TetrahedralizationStatus::resource_exhausted;
    case 3:
    case 4:
    case 5:
    case 10:
    case 200:
        return TetrahedralizationStatus::invalid_input;
    default:
        return TetrahedralizationStatus::dependency_failure;
    }
}

[[nodiscard]] std::string diagnostic_for_tetgen_code(const int code)
{
    switch (code) {
    case 1:
        return "TetGen exhausted memory";
    case 2:
        return "TetGen reported an internal error";
    case 3:
        return "TetGen rejected intersecting input facets";
    case 4:
        return "TetGen rejected an input feature below its numerical tolerance";
    case 5:
        return "TetGen rejected input facets that are too close";
    case 10:
        return "TetGen rejected the input piecewise-linear complex";
    case 200:
        return "TetGen could not complete the requested reference tetrahedralization";
    default:
        return "TetGen failed with code " + std::to_string(code);
    }
}

[[nodiscard]] TetrahedralizationResult build_tetgen_input(std::span<const TriangleMesh> participants,
                                                          const TetrahedralizationLimits& limits,
                                                          TetrahedralizationWork& work,
                                                          std::vector<ParticipantId>& input_point_owners,
                                                          tetgenio& input)
{
    if (participants.empty()) {
        return failure(TetrahedralizationStatus::invalid_input, work,
                       "tetrahedralization requires at least one participant surface");
    }
    if (!limits_are_positive(limits)) {
        return failure(TetrahedralizationStatus::invalid_input, work, "tetrahedralization limits must all be positive");
    }

    // DEVIATION(IROP-DEV-0014): Bound project-owned tetrahedralization input
    // and output counts. TetGen's peak in-process work remains backend-owned
    // and cannot be strictly capped through its library API.
    // See docs/COMPATIBILITY.md.
    work.input_participants = static_cast<std::uint64_t>(participants.size());
    if (work.input_participants > limits.max_participants) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "participant count exceeds the configured tetrahedralization limit");
    }
    if (work.input_participants > static_cast<std::uint64_t>(std::numeric_limits<ParticipantId>::max())) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "participant count exceeds the project-owned participant index range");
    }

    const MeshLimits validation_limits {
        .max_input_bytes = MeshLimits::default_max_input_bytes,
        .max_vertices = limits.max_input_points,
        .max_triangles = limits.max_input_triangles,
    };
    try {
        // DEVIATION(IROP-DEV-0013): Reject unsafe counts, indices, non-finite
        // coordinates, and degenerate faces before they reach TetGen instead
        // of trusting them as the Python tetrahedralization/CAT path does.
        // See docs/COMPATIBILITY.md.
        for (const TriangleMesh& participant : participants) {
            const MeshStatistics statistics = validate_and_measure_mesh(participant, validation_limits);
            if (!checked_accumulate(statistics.vertex_count, limits.max_input_points, work.input_points)) {
                return failure(TetrahedralizationStatus::resource_exhausted, work,
                               "input point count exceeds the configured tetrahedralization limit");
            }
            if (!checked_accumulate(statistics.triangle_count, limits.max_input_triangles, work.input_triangles)) {
                return failure(TetrahedralizationStatus::resource_exhausted, work,
                               "input triangle count exceeds the configured tetrahedralization limit");
            }
            static_cast<void>(ClosedMeshQuery(participant));
        }
    }
    catch (const Error& error) {
        const TetrahedralizationStatus status = error.category() == ErrorCategory::resource_limit
                                                    ? TetrahedralizationStatus::resource_exhausted
                                                    : TetrahedralizationStatus::invalid_input;
        return failure(status, work, error.what());
    }
    if (work.input_points == 0 || work.input_triangles == 0) {
        return failure(TetrahedralizationStatus::invalid_input, work,
                       "tetrahedralization requires non-empty participant surfaces");
    }

    constexpr std::uint64_t tetgen_count_max = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    if (work.input_points > tetgen_count_max || work.input_triangles > tetgen_count_max) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "input counts exceed TetGen's integer index range");
    }
    if (work.input_points > limits.max_output_points) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "the required reference point set exceeds the configured output point limit");
    }

    const std::size_t input_point_count = static_cast<std::size_t>(work.input_points);
    const std::size_t input_triangle_count = static_cast<std::size_t>(work.input_triangles);
    input.firstnumber = 0;
    input.numberofpoints = static_cast<int>(work.input_points);
    input.pointlist = new REAL[input_point_count * 3U];
    input.numberoffacets = static_cast<int>(work.input_triangles);
    // Value initialization is equivalent to tetgenio::init(facet*) for this
    // pointer-and-count aggregate and makes every slot cleanup-safe at once.
    input.facetlist = new tetgenio::facet[input_triangle_count] {};

    input_point_owners.reserve(input_point_count);
    std::size_t point_offset = 0;
    std::size_t facet_offset = 0;
    for (std::size_t participant_index = 0; participant_index < participants.size(); ++participant_index) {
        const TriangleMesh& participant = participants[participant_index];
        const ParticipantId owner = static_cast<ParticipantId>(participant_index);
        if (point_offset > input_point_count || participant.vertices.size() > input_point_count - point_offset ||
            facet_offset > input_triangle_count || participant.triangles.size() > input_triangle_count - facet_offset) {
            return failure(TetrahedralizationStatus::dependency_failure, work,
                           "validated participant counts changed while building the TetGen input");
        }

        for (std::size_t point_index = 0; point_index < participant.vertices.size(); ++point_index) {
            const Point3& point = participant.vertices[point_index];
            const std::size_t output_index = point_offset + point_index;
            input.pointlist[output_index * 3U] = point.x;
            input.pointlist[output_index * 3U + 1U] = point.y;
            input.pointlist[output_index * 3U + 2U] = point.z;
            input_point_owners.push_back(owner);
        }

        for (std::size_t triangle_index = 0; triangle_index < participant.triangles.size(); ++triangle_index) {
            const std::size_t facet_index = facet_offset + triangle_index;
            if (facet_index >= input_triangle_count) {
                return failure(TetrahedralizationStatus::dependency_failure, work,
                               "validated facet count exceeded the TetGen input array");
            }
            tetgenio::facet& facet = input.facetlist[facet_index];
            facet.polygonlist = new tetgenio::polygon[1];
            facet.numberofpolygons = 1;
            tetgenio::init(&facet.polygonlist[0]);
            facet.polygonlist[0].vertexlist = new int[3];
            facet.polygonlist[0].numberofvertices = 3;

            const Triangle& triangle = participant.triangles[triangle_index];
            for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                const std::uint64_t global_index = static_cast<std::uint64_t>(point_offset) + triangle[corner];
                facet.polygonlist[0].vertexlist[corner] = static_cast<int>(global_index);
            }
        }

        point_offset += participant.vertices.size();
        facet_offset += participant.triangles.size();
    }
    if (point_offset != input_point_count || facet_offset != input_triangle_count ||
        input_point_owners.size() != input_point_count) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "validated participant counts did not match the TetGen input arrays");
    }

    return {
        .status = TetrahedralizationStatus::success,
        .mesh = {},
        .work = work,
        .diagnostic = {},
    };
}

[[nodiscard]] TetrahedralizationResult translate_output(const tetgenio& output,
                                                        const std::span<const TriangleMesh> participants,
                                                        std::vector<ParticipantId> input_point_owners,
                                                        const std::uint64_t participant_count,
                                                        const TetrahedralizationLimits& limits,
                                                        TetrahedralizationWork work)
{
    if (output.numberofpoints < 0 || output.numberoftetrahedra < 0) {
        return failure(TetrahedralizationStatus::dependency_failure, work, "TetGen returned a negative output count");
    }
    work.output_points = static_cast<std::uint64_t>(output.numberofpoints);
    work.output_tetrahedra = static_cast<std::uint64_t>(output.numberoftetrahedra);

    if (work.output_points > limits.max_output_points) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "TetGen output point count exceeds the configured limit");
    }
    if (work.output_tetrahedra > limits.max_output_tetrahedra) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "TetGen output tetrahedron count exceeds the configured limit");
    }
    if (output.firstnumber != 0) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "TetGen did not honor zero-based output indexing");
    }
    if (output.numberofcorners != 4) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "TetGen did not return first-order tetrahedra");
    }
    if (work.output_tetrahedra == 0) {
        return failure(TetrahedralizationStatus::invalid_input, work,
                       "TetGen produced no tetrahedra from the participant surfaces");
    }
    if (work.output_points != work.input_points) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "TetGen did not preserve the reference point set");
    }
    if (work.output_points > 0 && output.pointlist == nullptr) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "TetGen returned no point array for a non-empty output");
    }
    if (output.tetrahedronlist == nullptr) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "TetGen returned no tetrahedron array for a non-empty output");
    }

    TetrahedralMesh mesh;
    mesh.participant_count = participant_count;
    mesh.points.reserve(static_cast<std::size_t>(work.output_points));
    std::size_t output_point_index = 0;
    for (const TriangleMesh& participant : participants) {
        for (const Point3& expected_point : participant.vertices) {
            const REAL* point = &output.pointlist[output_point_index * 3U];
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
                return failure(TetrahedralizationStatus::dependency_failure, work,
                               "TetGen returned a non-finite point coordinate");
            }
            if (!same_point(expected_point, point)) {
                return failure(TetrahedralizationStatus::dependency_failure, work,
                               "TetGen changed point coordinates or ordering, so ownership cannot be assigned safely");
            }
            mesh.points.push_back({ point[0], point[1], point[2] });
            ++output_point_index;
        }
    }
    mesh.point_owners = std::move(input_point_owners);

    mesh.tetrahedra.reserve(static_cast<std::size_t>(work.output_tetrahedra));
    for (std::uint64_t tetrahedron_index = 0; tetrahedron_index < work.output_tetrahedra; ++tetrahedron_index) {
        Tetrahedron tetrahedron {};
        for (std::size_t corner = 0; corner < tetrahedron.size(); ++corner) {
            const int point_index = output.tetrahedronlist[static_cast<std::size_t>(tetrahedron_index) * 4U + corner];
            if (point_index < 0 || static_cast<std::uint64_t>(point_index) >= work.output_points) {
                return failure(TetrahedralizationStatus::dependency_failure, work,
                               "TetGen returned an out-of-range tetrahedron point index");
            }
            tetrahedron[corner] = static_cast<MeshIndex>(point_index);
        }
        if (tetrahedron[0] == tetrahedron[1] || tetrahedron[0] == tetrahedron[2] || tetrahedron[0] == tetrahedron[3] ||
            tetrahedron[1] == tetrahedron[2] || tetrahedron[1] == tetrahedron[3] || tetrahedron[2] == tetrahedron[3] ||
            !tetrahedron_is_nondegenerate(tetrahedron, mesh.points)) {
            return failure(TetrahedralizationStatus::dependency_failure, work,
                           "TetGen returned a degenerate tetrahedron");
        }
        mesh.tetrahedra.push_back(tetrahedron);
    }

    return {
        .status = TetrahedralizationStatus::success,
        .mesh = std::move(mesh),
        .work = work,
        .diagnostic =
            "TetGen completed the Python-compatible O0/0Q tetrahedralization without changing the input "
            "point set",
    };
}

}  // namespace

const char* to_string(const TetrahedralizationStatus status) noexcept
{
    switch (status) {
    case TetrahedralizationStatus::success:
        return "success";
    case TetrahedralizationStatus::invalid_input:
        return "invalid_input";
    case TetrahedralizationStatus::resource_exhausted:
        return "resource_exhausted";
    case TetrahedralizationStatus::dependency_failure:
        return "dependency_failure";
    }
    return "unknown";
}

TetrahedralizationResult tetrahedralize_surfaces(const std::span<const TriangleMesh> participants,
                                                 const TetrahedralizationLimits& limits)
{
    TetrahedralizationWork work;
    try {
        tetgenio input;
        std::vector<ParticipantId> input_point_owners;
        TetrahedralizationResult input_result =
            build_tetgen_input(participants, limits, work, input_point_owners, input);
        if (!input_result.succeeded()) {
            return input_result;
        }

        tetgenio output;
        char switches[sizeof(tetgen_switches)] {};
        std::copy(std::begin(tetgen_switches), std::end(tetgen_switches), std::begin(switches));
        try {
            // TetGen's exact-predicate initialization and static filters use
            // mutable process-global state derived from the current input.
            // Keep the complete backend call single-owner across public calls.
            static std::mutex tetgen_call_mutex;
            const std::scoped_lock call_lock(tetgen_call_mutex);
            tetrahedralize(switches, &input, &output);
        }
        catch (const int code) {
            return failure(status_for_tetgen_code(code), work, diagnostic_for_tetgen_code(code));
        }

        return translate_output(output, participants, std::move(input_point_owners), work.input_participants, limits,
                                work);
    }
    catch (const std::bad_alloc&) {
        return failure(TetrahedralizationStatus::resource_exhausted, work,
                       "tetrahedralization exhausted addressable memory");
    }
    catch (const std::exception&) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "tetrahedralization failed with an unexpected dependency exception");
    }
    catch (...) {
        return failure(TetrahedralizationStatus::dependency_failure, work,
                       "tetrahedralization failed with an unknown dependency exception");
    }
}

}  // namespace irop
