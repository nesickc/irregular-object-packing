#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "irop/error.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/model/triangle_mesh.hpp"
#include "support/test_support.hpp"

#ifndef IROP_TEST_FIXTURE_DIR
#error "IROP_TEST_FIXTURE_DIR must name the checked-in test fixture directory"
#endif

namespace {

using Catch::Approx;

[[nodiscard]] std::filesystem::path fixture_path(const std::string_view name)
{
    return std::filesystem::path(IROP_TEST_FIXTURE_DIR) / name;
}

void require_tetrahedron_statistics(const irop::TriangleMesh& mesh)
{
    const irop::MeshStatistics statistics = irop::validate_and_measure_mesh(mesh, {});
    REQUIRE(statistics.vertex_count == 4);
    REQUIRE(statistics.triangle_count == 4);
    CHECK(statistics.bounds.minimum.x == Approx(0.0));
    CHECK(statistics.bounds.minimum.y == Approx(0.0));
    CHECK(statistics.bounds.minimum.z == Approx(0.0));
    CHECK(statistics.bounds.maximum.x == Approx(1.0));
    CHECK(statistics.bounds.maximum.y == Approx(1.0));
    CHECK(statistics.bounds.maximum.z == Approx(1.0));
}

TEST_CASE("STL loading converts the checked-in ASCII fixture to project-owned geometry")
{
    const std::filesystem::path input = fixture_path("tetra_ascii.stl");

    const irop::LoadedStl loaded = irop::read_stl(input, {});

    CHECK(loaded.encoding == irop::StlEncoding::ascii);
    CHECK(loaded.input_bytes == std::filesystem::file_size(input));
    require_tetrahedron_statistics(loaded.mesh);
}

TEST_CASE("ASCII STL preflight rejects a UTF-8 byte order mark unsupported by pinned VTK")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "bom-ascii.stl";
    std::string contents("\xEF\xBB\xBF", 3);
    contents += irop::test::ascii_tetrahedron();
    irop::test::write_text_file(input, contents);

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::read_stl(input, {}));
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("STL loading accepts a UTF-8 Windows input path")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / std::filesystem::path(u8"tętra-网.stl");
    irop::test::write_text_file(input, irop::test::ascii_tetrahedron());

    const irop::LoadedStl loaded = irop::read_stl(input, {});

    CHECK(loaded.encoding == irop::StlEncoding::ascii);
    require_tetrahedron_statistics(loaded.mesh);
}

TEST_CASE("STL loading accepts a compact binary tetrahedron")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "tetra-binary.stl";
    irop::test::write_binary_stl(input, irop::test::binary_tetrahedron_facets());

    const irop::LoadedStl loaded = irop::read_stl(input, {});

    CHECK(loaded.encoding == irop::StlEncoding::binary);
    CHECK(loaded.input_bytes == 84 + 4 * 50);
    require_tetrahedron_statistics(loaded.mesh);
}

TEST_CASE("binary STL detection does not trust a header beginning with solid")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "solid-header-binary.stl";
    irop::test::write_binary_stl(input, irop::test::binary_tetrahedron_facets(), std::nullopt, "solid binary data");

    const irop::LoadedStl loaded = irop::read_stl(input, {});

    CHECK(loaded.encoding == irop::StlEncoding::binary);
    require_tetrahedron_statistics(loaded.mesh);
}

TEST_CASE("STL writing emits a reloadable normalized binary mesh")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "normalized.stl";
    const irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
    static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));

    irop::write_stl(output, mesh);

    REQUIRE(std::filesystem::is_regular_file(output));
    const irop::LoadedStl reloaded = irop::read_stl(output, {});
    CHECK(reloaded.encoding == irop::StlEncoding::binary);
    require_tetrahedron_statistics(reloaded.mesh);
}

TEST_CASE("binary STL quantization exposes the exact serialized vertex coordinates")
{
    irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
    mesh.vertices[1].x = std::nextafter(1.0, 2.0);
    REQUIRE(mesh.vertices[1].x > 1.0);

    const irop::TriangleMesh serialized = irop::quantize_mesh_for_binary_stl(mesh);

    CHECK(serialized.triangles == mesh.triangles);
    REQUIRE(serialized.vertices.size() == mesh.vertices.size());
    CHECK(serialized.vertices[1].x == 1.0);
    CHECK(serialized.vertices[1].x == static_cast<double>(static_cast<float>(mesh.vertices[1].x)));
    CHECK(mesh.vertices[1].x > serialized.vertices[1].x);
}

TEST_CASE("STL writing serializes the same coordinates returned by the quantization helper")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "quantized.stl";
    irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
    mesh.vertices[1].x = std::nextafter(1.0, 2.0);
    const irop::TriangleMesh serialized = irop::quantize_mesh_for_binary_stl(mesh);

    irop::write_stl(output, mesh);
    const irop::LoadedStl reloaded = irop::read_stl(output, {});

    const irop::MeshStatistics serialized_statistics = irop::validate_and_measure_mesh(serialized, {});
    const irop::MeshStatistics reloaded_statistics = irop::validate_and_measure_mesh(reloaded.mesh, {});
    CHECK(reloaded_statistics.vertex_count == serialized_statistics.vertex_count);
    CHECK(reloaded_statistics.triangle_count == serialized_statistics.triangle_count);
    CHECK(reloaded_statistics.bounds.maximum.x == serialized_statistics.bounds.maximum.x);
    CHECK(reloaded_statistics.bounds.maximum.y == serialized_statistics.bounds.maximum.y);
    CHECK(reloaded_statistics.bounds.maximum.z == serialized_statistics.bounds.maximum.z);
}

TEST_CASE("binary STL quantization can close a sub-ULP gap between valid meshes")
{
    const irop::TriangleMesh first = irop::test::cube_mesh();
    irop::TriangleMesh second = irop::test::cube_mesh();
    const double translation = std::nextafter(2.0, 3.0);
    for (irop::Point3& point : second.vertices) {
        point.x += translation;
    }

    const irop::MeshStatistics first_before = irop::validate_and_measure_mesh(first, {});
    const irop::MeshStatistics second_before = irop::validate_and_measure_mesh(second, {});
    REQUIRE(first_before.bounds.maximum.x < second_before.bounds.minimum.x);

    const irop::TriangleMesh serialized_first = irop::quantize_mesh_for_binary_stl(first);
    const irop::TriangleMesh serialized_second = irop::quantize_mesh_for_binary_stl(second);
    const irop::MeshStatistics first_after = irop::validate_and_measure_mesh(serialized_first, {});
    const irop::MeshStatistics second_after = irop::validate_and_measure_mesh(serialized_second, {});

    CHECK(first_after.bounds.maximum.x == second_after.bounds.minimum.x);
}

TEST_CASE("STL preflight rejects missing, empty, and truncated inputs safely")
{
    irop::test::TempDirectory temporary;

    SECTION("missing file")
    {
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(temporary.path() / "missing.stl", {}));
        }, irop::ErrorCategory::input_io);
    }

    SECTION("empty file")
    {
        const std::filesystem::path input = temporary.path() / "empty.stl";
        irop::test::write_text_file(input, "");

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("binary file truncated before its count")
    {
        const std::filesystem::path input = temporary.path() / "short-binary.stl";
        irop::test::write_text_file(input, std::string(32, '\0'));

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("binary count exceeds complete records")
    {
        const std::filesystem::path input = temporary.path() / "count-mismatch.stl";
        std::vector<irop::test::BinaryFacet> facets = irop::test::binary_tetrahedron_facets();
        facets.pop_back();
        irop::test::write_binary_stl(input, facets, 4);

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("binary file has a trailing partial record")
    {
        const std::filesystem::path input = temporary.path() / "trailing-binary.stl";
        irop::test::write_binary_stl(input, irop::test::binary_tetrahedron_facets(), std::nullopt, "irop test fixture",
                                     "x");

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("malformed ASCII STL is rejected by the loading boundary")
{
    irop::test::TempDirectory temporary;

    SECTION("truncated facet")
    {
        const std::filesystem::path input = temporary.path() / "truncated-ascii.stl";
        irop::test::write_text_file(input,
                                    "solid truncated\n"
                                    "facet normal 0 0 1\n"
                                    "outer loop\n"
                                    "vertex 0 0 0\n"
                                    "vertex 1 0 0\n"
                                    "vertex 0 1 0\n");

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("unexpected record")
    {
        const std::filesystem::path input = temporary.path() / "malformed-ascii.stl";
        irop::test::write_text_file(input,
                                    "solid malformed\n"
                                    "facet normal 0 0 1\n"
                                    "not-an-outer-loop\n"
                                    "endsolid malformed\n");

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("overlong record")
    {
        const std::filesystem::path input = temporary.path() / "overlong-ascii.stl";
        irop::test::write_text_file(input, "solid overlong\n" + std::string(16 * 1024 + 1, 'x'));

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("STL loading enforces byte, triangle, and post-load merged vertex limits")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path ascii_input = temporary.path() / "tetra-ascii.stl";
    const std::filesystem::path binary_input = temporary.path() / "tetra-binary.stl";
    irop::test::write_text_file(ascii_input, irop::test::ascii_tetrahedron());
    irop::test::write_binary_stl(binary_input, irop::test::binary_tetrahedron_facets());

    SECTION("input bytes")
    {
        irop::MeshLimits limits;
        limits.max_input_bytes = std::filesystem::file_size(binary_input) - 1;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(binary_input, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("ASCII triangle records")
    {
        irop::MeshLimits limits;
        limits.max_triangles = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(ascii_input, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("ASCII merged vertices")
    {
        irop::MeshLimits limits;
        limits.max_vertices = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(ascii_input, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("binary declared triangles")
    {
        irop::MeshLimits limits;
        limits.max_triangles = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(binary_input, limits));
        }, irop::ErrorCategory::resource_limit);
    }

    SECTION("binary merged vertices")
    {
        irop::MeshLimits limits;
        limits.max_vertices = 3;

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(binary_input, limits));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("an excessive binary count fails as a resource limit before VTK allocation")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "excessive-count.stl";
    irop::test::write_binary_stl(input, {}, std::numeric_limits<std::uint32_t>::max());
    irop::MeshLimits limits;
    limits.max_triangles = std::numeric_limits<std::uint32_t>::max();

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::read_stl(input, limits));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("non-finite STL values are rejected by the loading boundary")
{
    irop::test::TempDirectory temporary;

    SECTION("ASCII NaN")
    {
        std::string text = irop::test::ascii_tetrahedron();
        const std::size_t first_coordinate = text.find("vertex 0 0 0");
        REQUIRE(first_coordinate != std::string::npos);
        text.replace(first_coordinate, std::string("vertex 0 0 0").size(), "vertex nan 0 0");
        const std::filesystem::path input = temporary.path() / "nan-ascii.stl";
        irop::test::write_text_file(input, text);

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }

    SECTION("binary infinity")
    {
        std::vector<irop::test::BinaryFacet> facets = irop::test::binary_tetrahedron_facets();
        facets[0].vertices[0][0] = std::numeric_limits<float>::infinity();
        const std::filesystem::path input = temporary.path() / "infinite-binary.stl";
        irop::test::write_binary_stl(input, facets);

        irop::test::require_error_category([&]() {
            static_cast<void>(irop::read_stl(input, {}));
        }, irop::ErrorCategory::invalid_mesh);
    }
}

TEST_CASE("STL encoding names are stable machine-readable values")
{
    CHECK(std::string_view(irop::to_string(irop::StlEncoding::ascii)) == "ascii");
    CHECK(std::string_view(irop::to_string(irop::StlEncoding::binary)) == "binary");
}

TEST_CASE("STL writing validates project-owned geometry before crossing into VTK")
{
    irop::test::TempDirectory temporary;
    irop::TriangleMesh mesh = irop::test::tetrahedron_mesh();
    mesh.triangles[0] = { 0, 0, 1 };

    irop::test::require_error_category([&]() {
        irop::write_stl(temporary.path() / "invalid.stl", mesh);
    }, irop::ErrorCategory::invalid_mesh);

    CHECK_FALSE(std::filesystem::exists(temporary.path() / "invalid.stl"));
}

TEST_CASE("STL writing rejects triangles that collapse during binary float quantization")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "quantized-degenerate.stl";
    const irop::TriangleMesh mesh {
        .vertices = {
            { 1.0, 0.0, 0.0 },
            { std::nextafter(1.0, 2.0), 0.0, 0.0 },
            { 1.0, 1.0, 0.0 },
        },
        .triangles = { { 0, 1, 2 } },
    };
    static_cast<void>(irop::validate_and_measure_mesh(mesh, {}));

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::quantize_mesh_for_binary_stl(mesh));
    }, irop::ErrorCategory::invalid_mesh);

    irop::test::require_error_category([&]() {
        irop::write_stl(output, mesh);
    }, irop::ErrorCategory::invalid_mesh);

    CHECK_FALSE(std::filesystem::exists(output));
}

}  // namespace
