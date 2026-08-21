#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
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
