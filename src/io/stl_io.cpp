#include "irop/io/stl_io.hpp"

#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkErrorCode.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSTLReader.h>
#include <vtkSTLWriter.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
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

#include "io/stl_io_internal.hpp"
#include "irop/error.hpp"
#include "irop/model/mesh_validation.hpp"

namespace irop {
namespace {

constexpr std::uint64_t binary_header_bytes = 84;
constexpr std::uint64_t binary_triangle_bytes = 50;
constexpr std::size_t inspection_prefix_bytes = 512;
constexpr std::size_t maximum_ascii_line_bytes = 16 * 1024;
constexpr std::size_t maximum_vtk_warnings = 16;
constexpr std::size_t maximum_vtk_warning_bytes = 512;
constexpr std::uint64_t maximum_vtk_binary_triangles =
    static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / 3ULL;

struct PreflightResult {
    StlEncoding encoding;
    std::uint64_t input_bytes;
    std::uint64_t triangle_records;
};

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

[[nodiscard]] ScopedWindowsHandle lock_input_path(const std::filesystem::path& path)
{
    ScopedWindowsHandle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!handle.is_valid()) {
        throw Error(ErrorCategory::input_io,
                    "failed to lock the resolved input STL for a consistent read (Windows error " +
                        std::to_string(GetLastError()) + ")");
    }
    return handle;
}

[[nodiscard]] ScopedWindowsHandle reserve_output_path(const std::filesystem::path& path)
{
    // Desired access is zero so VTK can open the same file for writing. Omitting FILE_SHARE_DELETE keeps the path from
    // being replaced with a symlink or another file until VTK has finished.
    ScopedWindowsHandle handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.is_valid()) {
        throw Error(ErrorCategory::output_io,
                    "failed to reserve a new STL output path (Windows error " + std::to_string(GetLastError()) + ")");
    }
    return handle;
}

[[nodiscard]] ScopedWindowsHandle lock_written_output(const std::filesystem::path& path)
{
    ScopedWindowsHandle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.is_valid()) {
        throw Error(ErrorCategory::output_io,
                    "failed to lock the written STL through success publication (Windows error " +
                        std::to_string(GetLastError()) + ")");
    }
    return handle;
}
#endif

[[nodiscard]] std::filesystem::path resolve_input_path(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path resolved = std::filesystem::canonical(path, error);
    if (error) {
        throw Error(ErrorCategory::input_io, "input STL does not exist or cannot be resolved");
    }
    return resolved;
}

[[nodiscard]] std::filesystem::path resolve_output_path(const std::filesystem::path& path)
{
    if (path.filename().empty()) {
        throw Error(ErrorCategory::output_io, "STL output path must include a filename");
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
        throw Error(ErrorCategory::output_io, "STL output directory does not exist or cannot be resolved");
    }
    return parent / path.filename();
}

[[nodiscard]] std::string path_for_vtk(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

[[nodiscard]] bool starts_with_ascii_case_insensitive(const std::string_view text, const std::string_view prefix)
{
    if (text.size() < prefix.size()) {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto actual = static_cast<unsigned char>(text[index]);
        const auto expected = static_cast<unsigned char>(prefix[index]);
        if (std::tolower(actual) != std::tolower(expected)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view trim_ascii_left(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    return text;
}

[[nodiscard]] bool is_ascii_stl_record(const std::string_view line, const std::string_view keyword)
{
    const std::string_view trimmed = trim_ascii_left(line);
    if (!starts_with_ascii_case_insensitive(trimmed, keyword)) {
        return false;
    }
    return trimmed.size() == keyword.size() || std::isspace(static_cast<unsigned char>(trimmed[keyword.size()])) != 0;
}

[[nodiscard]] std::uint32_t read_little_endian_uint32(const std::array<unsigned char, binary_header_bytes>& header)
{
    return static_cast<std::uint32_t>(header[80]) | (static_cast<std::uint32_t>(header[81]) << 8U) |
           (static_cast<std::uint32_t>(header[82]) << 16U) | (static_cast<std::uint32_t>(header[83]) << 24U);
}

template <std::size_t Size>
[[nodiscard]] std::uint32_t read_little_endian_uint32(const std::array<unsigned char, Size>& bytes,
                                                      const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool contains_non_text_byte(const std::vector<unsigned char>& bytes)
{
    std::size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU) {
        offset = 3;
    }
    return std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end(), [](const unsigned char byte) {
        return byte != '\t' && byte != '\n' && byte != '\r' && (byte < 32U || byte > 126U);
    });
}

[[nodiscard]] bool starts_with_solid(const std::vector<unsigned char>& bytes)
{
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string_view trimmed = trim_ascii_left(text);
    if (trimmed.size() >= 3 && static_cast<unsigned char>(trimmed[0]) == 0xEFU &&
        static_cast<unsigned char>(trimmed[1]) == 0xBBU && static_cast<unsigned char>(trimmed[2]) == 0xBFU) {
        trimmed.remove_prefix(3);
        trimmed = trim_ascii_left(trimmed);
    }
    return starts_with_ascii_case_insensitive(trimmed, "solid");
}

[[nodiscard]] bool has_utf8_byte_order_mark(const std::vector<unsigned char>& bytes) noexcept
{
    return bytes.size() >= 3 && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU;
}

[[noreturn]] void throw_preflight_limit(const char* kind, const std::uint64_t actual, const std::uint64_t maximum)
{
    throw Error(ErrorCategory::resource_limit, std::string("STL ") + kind + " " + std::to_string(actual) +
                                                   " exceeds configured limit " + std::to_string(maximum));
}

void enforce_declared_triangle_limits(const std::uint64_t triangles, const MeshLimits& limits)
{
    if (triangles > limits.max_triangles) {
        throw_preflight_limit("triangle count", triangles, limits.max_triangles);
    }
}

void preflight_binary_records(const std::filesystem::path& input_path, const std::uint64_t triangle_count)
{
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw Error(ErrorCategory::input_io, "failed to open input STL for binary preflight");
    }
    input.seekg(static_cast<std::streamoff>(binary_header_bytes), std::ios::beg);
    if (!input) {
        throw Error(ErrorCategory::input_io, "failed to seek within input STL during binary preflight");
    }

    std::array<unsigned char, binary_triangle_bytes> record {};
    for (std::uint64_t triangle = 0; triangle < triangle_count; ++triangle) {
        input.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
        if (input.gcount() != static_cast<std::streamsize>(record.size())) {
            throw Error(ErrorCategory::invalid_mesh, "binary STL ends inside a triangle record");
        }

        for (std::size_t component = 0; component < 12; ++component) {
            const std::uint32_t bits = read_little_endian_uint32(record, component * sizeof(float));
            if (!std::isfinite(std::bit_cast<float>(bits))) {
                throw Error(ErrorCategory::invalid_mesh, "binary STL contains a non-finite floating-point value");
            }
        }
    }
}

void preflight_ascii_records(const std::filesystem::path& input_path, const MeshLimits& limits,
                             std::uint64_t& triangle_count, std::uint64_t& vertex_record_count)
{
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw Error(ErrorCategory::input_io, "failed to open input STL for ASCII preflight");
    }

    std::string line;
    line.reserve(256);
    auto inspect_line = [&]() {
        if (is_ascii_stl_record(line, "facet")) {
            ++triangle_count;
            if (triangle_count > limits.max_triangles) {
                throw_preflight_limit("triangle count", triangle_count, limits.max_triangles);
            }
        }
        if (is_ascii_stl_record(line, "vertex")) {
            ++vertex_record_count;
            const std::uint64_t maximum_vertex_records =
                limits.max_triangles > std::numeric_limits<std::uint64_t>::max() / 3ULL
                    ? std::numeric_limits<std::uint64_t>::max()
                    : limits.max_triangles * 3ULL;
            if (vertex_record_count > maximum_vertex_records) {
                throw_preflight_limit("vertex record count", vertex_record_count, maximum_vertex_records);
            }
        }
        line.clear();
    };

    char character = '\0';
    while (input.get(character)) {
        if (character == '\n') {
            inspect_line();
            continue;
        }
        if (line.size() >= maximum_ascii_line_bytes) {
            throw Error(ErrorCategory::invalid_mesh, "ASCII STL contains an overlong record");
        }
        line.push_back(character);
    }
    if (!input.eof()) {
        throw Error(ErrorCategory::input_io, "failed while reading input STL during ASCII preflight");
    }
    if (!line.empty()) {
        inspect_line();
    }

    if (triangle_count == 0) {
        throw Error(ErrorCategory::invalid_mesh, "ASCII STL contains no facet records");
    }
    enforce_declared_triangle_limits(triangle_count, limits);
    if (vertex_record_count != triangle_count * 3ULL) {
        throw Error(ErrorCategory::invalid_mesh, "ASCII STL facet and vertex record counts are inconsistent");
    }
}

[[nodiscard]] PreflightResult preflight_stl(const std::filesystem::path& input_path, const MeshLimits& limits)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(input_path, error);
    if (error || !std::filesystem::exists(status)) {
        throw Error(ErrorCategory::input_io, "input STL does not exist or cannot be inspected");
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw Error(ErrorCategory::input_io, "input STL is not a regular file");
    }

    const std::uintmax_t native_size = std::filesystem::file_size(input_path, error);
    if (error || native_size > std::numeric_limits<std::uint64_t>::max()) {
        throw Error(ErrorCategory::input_io, "input STL size cannot be represented safely");
    }
    const auto input_bytes = static_cast<std::uint64_t>(native_size);
    if (input_bytes > limits.max_input_bytes) {
        throw_preflight_limit("byte size", input_bytes, limits.max_input_bytes);
    }
    if (input_bytes == 0) {
        throw Error(ErrorCategory::invalid_mesh, "input STL is empty");
    }

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw Error(ErrorCategory::input_io, "failed to open input STL for preflight");
    }
    const auto prefix_size = static_cast<std::size_t>(std::min<std::uint64_t>(input_bytes, inspection_prefix_bytes));
    std::vector<unsigned char> prefix(prefix_size);
    input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    if (input.gcount() != static_cast<std::streamsize>(prefix.size())) {
        throw Error(ErrorCategory::input_io, "failed to read input STL preflight prefix");
    }

    std::array<unsigned char, binary_header_bytes> binary_header {};
    std::uint64_t declared_triangles = 0;
    std::uint64_t expected_binary_bytes = 0;
    bool exact_binary_length = false;
    if (input_bytes >= binary_header_bytes) {
        std::copy_n(prefix.begin(), binary_header_bytes, binary_header.begin());
        declared_triangles = read_little_endian_uint32(binary_header);
        expected_binary_bytes = binary_header_bytes + declared_triangles * binary_triangle_bytes;
        exact_binary_length = expected_binary_bytes == input_bytes;
    }

    const bool appears_binary = exact_binary_length || !starts_with_solid(prefix) || contains_non_text_byte(prefix);
    if (appears_binary) {
        if (input_bytes < binary_header_bytes) {
            throw Error(ErrorCategory::invalid_mesh, "binary STL is truncated before its triangle count");
        }
        enforce_declared_triangle_limits(declared_triangles, limits);
        if (declared_triangles > maximum_vtk_binary_triangles) {
            throw Error(ErrorCategory::resource_limit, "binary STL triangle count exceeds VTK's safe allocation range");
        }
        if (expected_binary_bytes != input_bytes) {
            throw Error(ErrorCategory::invalid_mesh,
                        "binary STL byte length does not match its declared triangle count");
        }
        if (declared_triangles == 0) {
            throw Error(ErrorCategory::invalid_mesh, "binary STL declares no triangles");
        }
        preflight_binary_records(input_path, declared_triangles);
        return { StlEncoding::binary, input_bytes, declared_triangles };
    }

    // VTK 9.3 does not reliably accept a UTF-8 BOM before the opening `solid` record. Reject it at the project-owned
    // boundary instead of passing an input to VTK that its parser can silently misinterpret.
    if (has_utf8_byte_order_mark(prefix)) {
        throw Error(ErrorCategory::invalid_mesh, "ASCII STL with a UTF-8 byte order mark is not supported");
    }

    std::uint64_t ascii_triangles = 0;
    std::uint64_t ascii_vertex_records = 0;
    preflight_ascii_records(input_path, limits, ascii_triangles, ascii_vertex_records);
    return { StlEncoding::ascii, input_bytes, ascii_triangles };
}

void mark_vtk_error(vtkObject*, unsigned long, void* client_data, void*) { *static_cast<bool*>(client_data) = true; }

void record_vtk_warning(vtkObject*, unsigned long, void* client_data, void* call_data)
{
    auto& warnings = *static_cast<std::vector<std::string>*>(client_data);
    if (warnings.size() >= maximum_vtk_warnings) {
        return;
    }

    const auto* message = static_cast<const char*>(call_data);
    if (message == nullptr) {
        warnings.emplace_back("VTK reported an unspecified warning");
        return;
    }
    std::size_t length = 0;
    while (length < maximum_vtk_warning_bytes && message[length] != '\0') {
        ++length;
    }
    std::string warning;
    warning.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(message[index]);
        warning.push_back(byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 32U && byte <= 126U)
                              ? static_cast<char>(byte)
                              : '?');
    }
    warnings.push_back(std::move(warning));
}

[[nodiscard]] std::uint64_t checked_vtk_count(const vtkIdType count, const char* kind)
{
    if (count < 0) {
        throw Error(ErrorCategory::dependency_failure, std::string("VTK returned a negative ") + kind + " count");
    }
    return static_cast<std::uint64_t>(count);
}

[[nodiscard]] TriangleMesh poly_data_to_triangle_mesh(vtkPolyData& poly_data, const MeshLimits& limits)
{
    const std::uint64_t vertex_count = checked_vtk_count(poly_data.GetNumberOfPoints(), "point");
    const std::uint64_t cell_count = checked_vtk_count(poly_data.GetNumberOfCells(), "cell");
    const std::uint64_t polygon_count = checked_vtk_count(poly_data.GetNumberOfPolys(), "polygon");

    if (vertex_count > limits.max_vertices) {
        throw_preflight_limit("post-load vertex count", vertex_count, limits.max_vertices);
    }
    if (cell_count > limits.max_triangles) {
        throw_preflight_limit("post-load cell count", cell_count, limits.max_triangles);
    }
    if (cell_count != polygon_count || poly_data.GetNumberOfVerts() != 0 || poly_data.GetNumberOfLines() != 0 ||
        poly_data.GetNumberOfStrips() != 0) {
        throw Error(ErrorCategory::invalid_mesh, "STL did not load as a triangle-only polygon mesh");
    }

    TriangleMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(vertex_count));
    mesh.triangles.reserve(static_cast<std::size_t>(cell_count));

    std::array<double, 3> coordinates {};
    for (vtkIdType point_id = 0; point_id < poly_data.GetNumberOfPoints(); ++point_id) {
        poly_data.GetPoint(point_id, coordinates.data());
        mesh.vertices.push_back({ coordinates[0], coordinates[1], coordinates[2] });
    }

    for (vtkIdType cell_id = 0; cell_id < poly_data.GetNumberOfCells(); ++cell_id) {
        vtkIdType point_count = 0;
        const vtkIdType* point_ids = nullptr;
        poly_data.GetCellPoints(cell_id, point_count, point_ids);
        if (point_count != 3 || point_ids == nullptr) {
            throw Error(ErrorCategory::invalid_mesh, "STL contains a non-triangular cell");
        }
        if (point_ids[0] < 0 || point_ids[1] < 0 || point_ids[2] < 0) {
            throw Error(ErrorCategory::dependency_failure, "VTK returned a negative mesh point index");
        }
        if (static_cast<std::uint64_t>(point_ids[0]) >= vertex_count ||
            static_cast<std::uint64_t>(point_ids[1]) >= vertex_count ||
            static_cast<std::uint64_t>(point_ids[2]) >= vertex_count) {
            throw Error(ErrorCategory::dependency_failure, "VTK returned an out-of-range mesh point index");
        }
        mesh.triangles.push_back({
            static_cast<MeshIndex>(point_ids[0]),
            static_cast<MeshIndex>(point_ids[1]),
            static_cast<MeshIndex>(point_ids[2]),
        });
    }

    return mesh;
}

[[nodiscard]] vtkSmartPointer<vtkPolyData> triangle_mesh_to_poly_data(const TriangleMesh& mesh)
{
    if (mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<vtkIdType>::max()) ||
        mesh.triangles.size() > static_cast<std::size_t>(std::numeric_limits<vtkIdType>::max())) {
        throw Error(ErrorCategory::resource_limit, "mesh is too large for the configured VTK index type");
    }

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

[[nodiscard]] Point3 quantize_binary_stl_point(const Point3& point) noexcept
{
    return {
        static_cast<double>(static_cast<float>(point.x)),
        static_cast<double>(static_cast<float>(point.y)),
        static_cast<double>(static_cast<float>(point.z)),
    };
}

[[nodiscard]] Point3 subtract_points(const Point3& left, const Point3& right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Point3 cross_product(const Point3& left, const Point3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double squared_norm(const Point3& point) noexcept
{
    return point.x * point.x + point.y * point.y + point.z * point.z;
}

void validate_binary_stl_quantization(const TriangleMesh& mesh)
{
    for (const Triangle& triangle : mesh.triangles) {
        const Point3 first = quantize_binary_stl_point(mesh.vertices[static_cast<std::size_t>(triangle[0])]);
        const Point3 second = quantize_binary_stl_point(mesh.vertices[static_cast<std::size_t>(triangle[1])]);
        const Point3 third = quantize_binary_stl_point(mesh.vertices[static_cast<std::size_t>(triangle[2])]);
        const double area_factor_squared =
            squared_norm(cross_product(subtract_points(second, first), subtract_points(third, first)));
        if (!std::isfinite(area_factor_squared) || area_factor_squared <= 0.0) {
            throw Error(ErrorCategory::invalid_mesh,
                        "mesh contains a triangle that degenerates when encoded as binary STL floats");
        }
    }
}

}  // namespace

const char* to_string(const StlEncoding encoding) noexcept
{
    switch (encoding) {
    case StlEncoding::ascii:
        return "ascii";
    case StlEncoding::binary:
        return "binary";
    }
    return "unknown";
}

LoadedStl read_stl(const std::filesystem::path& input_path, const MeshLimits& limits)
{
    const std::filesystem::path stable_input_path = resolve_input_path(input_path);
#ifdef _WIN32
    ScopedWindowsHandle input_lock = lock_input_path(stable_input_path);
    static_cast<void>(input_lock);
#endif
    const PreflightResult preflight = preflight_stl(stable_input_path, limits);

    vtkNew<vtkSTLReader> reader;
    bool vtk_error_observed = false;
    vtkNew<vtkCallbackCommand> error_observer;
    error_observer->SetClientData(&vtk_error_observed);
    error_observer->SetCallback(mark_vtk_error);
    const unsigned long error_observer_tag = reader->AddObserver(vtkCommand::ErrorEvent, error_observer);
    std::vector<std::string> warnings;
    vtkNew<vtkCallbackCommand> warning_observer;
    warning_observer->SetClientData(&warnings);
    warning_observer->SetCallback(record_vtk_warning);
    const unsigned long warning_observer_tag = reader->AddObserver(vtkCommand::WarningEvent, warning_observer);
    const std::string vtk_path = path_for_vtk(stable_input_path);
    reader->SetFileName(vtk_path.c_str());
    reader->MergingOn();
    reader->Update();
    reader->RemoveObserver(warning_observer_tag);
    reader->RemoveObserver(error_observer_tag);
    if (vtk_error_observed || reader->GetErrorCode() != vtkErrorCode::NoError) {
        throw Error(ErrorCategory::invalid_mesh, std::string("VTK failed to read STL: ") +
                                                     vtkErrorCode::GetStringFromErrorCode(reader->GetErrorCode()));
    }
    vtkPolyData* output = reader->GetOutput();
    if (output == nullptr) {
        throw Error(ErrorCategory::dependency_failure, "VTK returned no mesh after reading STL");
    }

    TriangleMesh mesh = poly_data_to_triangle_mesh(*output, limits);
    if (mesh.triangles.size() != preflight.triangle_records) {
        throw Error(ErrorCategory::invalid_mesh, "VTK triangle count differs from the preflight STL record count");
    }
    static_cast<void>(validate_and_measure_mesh(mesh, limits));

    std::error_code file_error;
    const std::uintmax_t final_size = std::filesystem::file_size(stable_input_path, file_error);
    if (file_error || final_size != preflight.input_bytes) {
        throw Error(ErrorCategory::input_io, "input STL changed while it was being read");
    }

    return {
        .mesh = std::move(mesh),
        .encoding = preflight.encoding,
        .input_bytes = preflight.input_bytes,
        .warnings = std::move(warnings),
    };
}

void detail::write_stl_transactional(const std::filesystem::path& output_path, const TriangleMesh& mesh,
                                     const std::function<void()>& completion)
{
    MeshLimits validation_limits {
        .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_vertices = static_cast<std::uint64_t>(mesh.vertices.size()),
        .max_triangles = static_cast<std::uint64_t>(mesh.triangles.size()),
    };
    static_cast<void>(validate_and_measure_mesh(mesh, validation_limits));
    if (mesh.triangles.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw Error(ErrorCategory::resource_limit, "mesh has too many triangles for binary STL");
    }
    for (const Point3& point : mesh.vertices) {
        constexpr double maximum_stl_coordinate = static_cast<double>(std::numeric_limits<float>::max());
        if (std::abs(point.x) > maximum_stl_coordinate || std::abs(point.y) > maximum_stl_coordinate ||
            std::abs(point.z) > maximum_stl_coordinate) {
            throw Error(ErrorCategory::invalid_mesh, "mesh contains a coordinate outside binary STL float range");
        }
    }
    validate_binary_stl_quantization(mesh);

    const std::filesystem::path stable_output_path = resolve_output_path(output_path);
#ifdef _WIN32
    ScopedWindowsHandle output_reservation = reserve_output_path(stable_output_path);
#else
    std::error_code existing_error;
    const std::filesystem::file_status existing_status =
        std::filesystem::symlink_status(stable_output_path, existing_error);
    if (existing_error || existing_status.type() != std::filesystem::file_type::not_found) {
        throw Error(ErrorCategory::output_io, "STL output path already exists or cannot be inspected");
    }
#endif

    try {
        vtkSmartPointer<vtkPolyData> poly_data = triangle_mesh_to_poly_data(mesh);
        vtkNew<vtkSTLWriter> writer;
        bool vtk_error_observed = false;
        vtkNew<vtkCallbackCommand> error_observer;
        error_observer->SetClientData(&vtk_error_observed);
        error_observer->SetCallback(mark_vtk_error);
        const unsigned long error_observer_tag = writer->AddObserver(vtkCommand::ErrorEvent, error_observer);
        const std::string vtk_path = path_for_vtk(stable_output_path);
        writer->SetFileName(vtk_path.c_str());
        writer->SetFileTypeToBinary();
        writer->SetInputData(poly_data);
        const int write_succeeded = writer->Write();
        writer->RemoveObserver(error_observer_tag);
        if (write_succeeded == 0 || vtk_error_observed || writer->GetErrorCode() != vtkErrorCode::NoError) {
            throw Error(ErrorCategory::output_io, std::string("VTK failed to write STL: ") +
                                                      vtkErrorCode::GetStringFromErrorCode(writer->GetErrorCode()));
        }

        const std::uint64_t expected_size =
            binary_header_bytes + static_cast<std::uint64_t>(mesh.triangles.size()) * binary_triangle_bytes;
        std::error_code output_error;
        const std::uintmax_t output_size = std::filesystem::file_size(stable_output_path, output_error);
        if (output_error || output_size != expected_size) {
            throw Error(ErrorCategory::output_io, "VTK produced an incomplete or inconsistent binary STL output");
        }
#ifdef _WIN32
        ScopedWindowsHandle integrity_lock = lock_written_output(stable_output_path);
        output_reservation.close();
        static_cast<void>(integrity_lock);
#endif
        completion();
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

void write_stl(const std::filesystem::path& output_path, const TriangleMesh& mesh)
{
    detail::write_stl_transactional(output_path, mesh, []() {});
}

}  // namespace irop
