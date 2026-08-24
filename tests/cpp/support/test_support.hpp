#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "irop/error.hpp"
#include "irop/model/triangle_mesh.hpp"

namespace irop::test {

class TempDirectory final {
public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence { 0 };
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code error;
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        if (error) {
            throw std::runtime_error("failed to locate the temporary test root");
        }
        root_ = std::filesystem::weakly_canonical(temporary_root, error);
        if (error || !root_.is_absolute()) {
            throw std::runtime_error("failed to resolve the temporary test root");
        }
        path_ = root_ / ("irop-cpp-test-" + std::to_string(timestamp) + "-" + std::to_string(sequence.fetch_add(1)));

        if (!std::filesystem::create_directories(path_, error) || error) {
            throw std::runtime_error("failed to create a temporary test directory");
        }
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    TempDirectory& operator=(TempDirectory&&) = delete;

    ~TempDirectory() noexcept
    {
        try {
            if (path_.parent_path() != root_ || !path_.filename().string().starts_with("irop-cpp-test-")) {
                return;
            }
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
        catch (...) {
            // Test cleanup must not terminate the process during stack unwinding.
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path root_;
    std::filesystem::path path_;
};

[[nodiscard]] inline TriangleMesh tetrahedron_mesh()
{
    return {
        .vertices = {
            { 0.0, 0.0, 0.0 },
            { 1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 1.0 },
        },
        .triangles = {
            { 0, 2, 1 },
            { 0, 1, 3 },
            { 0, 3, 2 },
            { 1, 2, 3 },
        },
    };
}

[[nodiscard]] inline TriangleMesh box_mesh(const double half_x, const double half_y, const double half_z)
{
    return {
        .vertices = {
            { -half_x, -half_y, -half_z },
            { half_x, -half_y, -half_z },
            { half_x, half_y, -half_z },
            { -half_x, half_y, -half_z },
            { -half_x, -half_y, half_z },
            { half_x, -half_y, half_z },
            { half_x, half_y, half_z },
            { -half_x, half_y, half_z },
        },
        .triangles = {
            { 0, 2, 1 },
            { 0, 3, 2 },
            { 4, 5, 6 },
            { 4, 6, 7 },
            { 0, 1, 5 },
            { 0, 5, 4 },
            { 3, 7, 6 },
            { 3, 6, 2 },
            { 0, 4, 7 },
            { 0, 7, 3 },
            { 1, 2, 6 },
            { 1, 6, 5 },
        },
    };
}

[[nodiscard]] inline TriangleMesh cube_mesh(const double half_extent = 1.0)
{
    return box_mesh(half_extent, half_extent, half_extent);
}

[[nodiscard]] inline TriangleMesh cylinder_mesh(const double radius, const double half_height,
                                                const std::size_t segment_count = 12)
{
    if (!std::isfinite(radius) || !std::isfinite(half_height) || radius <= 0.0 || half_height <= 0.0 ||
        segment_count < 3 || segment_count > std::numeric_limits<MeshIndex>::max() / 2) {
        throw std::invalid_argument("test cylinder dimensions or segment count are invalid");
    }

    constexpr double two_pi = 6.28318530717958647692;
    TriangleMesh mesh;
    mesh.vertices.reserve(segment_count * 2);
    for (std::size_t index = 0; index < segment_count; ++index) {
        const double angle = two_pi * static_cast<double>(index) / static_cast<double>(segment_count);
        mesh.vertices.push_back({ radius * std::cos(angle), radius * std::sin(angle), -half_height });
    }
    for (std::size_t index = 0; index < segment_count; ++index) {
        const double angle = two_pi * static_cast<double>(index) / static_cast<double>(segment_count);
        mesh.vertices.push_back({ radius * std::cos(angle), radius * std::sin(angle), half_height });
    }

    const MeshIndex top_offset = static_cast<MeshIndex>(segment_count);
    for (std::size_t index = 0; index < segment_count; ++index) {
        const MeshIndex bottom = static_cast<MeshIndex>(index);
        const MeshIndex next = static_cast<MeshIndex>((index + 1) % segment_count);
        const MeshIndex top = top_offset + bottom;
        const MeshIndex top_next = top_offset + next;
        mesh.triangles.push_back({ bottom, next, top_next });
        mesh.triangles.push_back({ bottom, top_next, top });
    }
    for (std::size_t index = 1; index + 1 < segment_count; ++index) {
        const MeshIndex current = static_cast<MeshIndex>(index);
        const MeshIndex next = static_cast<MeshIndex>(index + 1);
        mesh.triangles.push_back({ 0, next, current });
        mesh.triangles.push_back({ top_offset, top_offset + current, top_offset + next });
    }
    return mesh;
}

[[nodiscard]] inline std::string ascii_tetrahedron()
{
    return R"stl(solid tetrahedron
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 0 1 0
      vertex 1 0 0
    endloop
  endfacet
  facet normal 0 -1 0
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 0 0 1
    endloop
  endfacet
  facet normal -1 0 0
    outer loop
      vertex 0 0 0
      vertex 0 0 1
      vertex 0 1 0
    endloop
  endfacet
  facet normal 0.577350269 0.577350269 0.577350269
    outer loop
      vertex 1 0 0
      vertex 0 1 0
      vertex 0 0 1
    endloop
  endfacet
endsolid tetrahedron
)stl";
}

inline void write_text_file(const std::filesystem::path& path, const std::string_view contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to open a test fixture for writing");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error("failed to write a test fixture");
    }
}

struct BinaryFacet {
    std::array<float, 3> normal {};
    std::array<std::array<float, 3>, 3> vertices {};
};

[[nodiscard]] inline std::vector<BinaryFacet> binary_tetrahedron_facets()
{
    return {
        { { 0.0F, 0.0F, -1.0F },                        { { { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } } } },
        { { 0.0F, -1.0F, 0.0F },                        { { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } } } },
        { { -1.0F, 0.0F, 0.0F },                        { { { 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F } } } },
        { { 0.577350269F, 0.577350269F, 0.577350269F },
         { { { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } } }                                                },
    };
}

inline void write_u16_le(std::ostream& output, const std::uint16_t value)
{
    const std::array<char, 2> bytes {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline void write_u32_le(std::ostream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline void write_float_le(std::ostream& output, const float value)
{
    write_u32_le(output, std::bit_cast<std::uint32_t>(value));
}

inline void write_binary_stl(const std::filesystem::path& path, const std::vector<BinaryFacet>& facets,
                             const std::optional<std::uint32_t> declared_triangle_count = std::nullopt,
                             const std::string_view header_text = "irop test fixture",
                             const std::string_view trailing_bytes = {})
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to open a binary STL test fixture for writing");
    }

    std::array<char, 80> header {};
    const std::size_t header_size = std::min(header.size(), header_text.size());
    std::copy_n(header_text.begin(), header_size, header.begin());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));

    const std::uint32_t triangle_count = declared_triangle_count.value_or(static_cast<std::uint32_t>(facets.size()));
    write_u32_le(output, triangle_count);

    for (const BinaryFacet& facet : facets) {
        for (const float component : facet.normal) {
            write_float_le(output, component);
        }
        for (const auto& vertex : facet.vertices) {
            for (const float component : vertex) {
                write_float_le(output, component);
            }
        }
        write_u16_le(output, 0);
    }

    output.write(trailing_bytes.data(), static_cast<std::streamsize>(trailing_bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to write a binary STL test fixture");
    }
}

template <typename Callable>
void require_error_category(Callable&& callable, const ErrorCategory expected)
{
    try {
        std::forward<Callable>(callable)();
    }
    catch (const Error& error) {
        REQUIRE(error.category() == expected);
        return;
    }
    catch (...) {
        FAIL("expected irop::Error, but a different exception type was thrown");
    }
    FAIL("expected irop::Error");
}

}  // namespace irop::test
