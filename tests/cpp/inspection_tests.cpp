#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "irop/error.hpp"
#include "irop/inspection/inspect_stl.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/model/triangle_mesh.hpp"
#include "support/test_support.hpp"

#ifndef IROP_TEST_FIXTURE_DIR
#error "IROP_TEST_FIXTURE_DIR must name the checked-in test fixture directory"
#endif

#ifndef IROP_TEST_SCHEMA_DIR
#error "IROP_TEST_SCHEMA_DIR must name the checked-in JSON schema directory"
#endif

namespace {

using Catch::Approx;

[[nodiscard]] std::filesystem::path fixture_path(const std::string_view name)
{
    return std::filesystem::path(IROP_TEST_FIXTURE_DIR) / name;
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    nlohmann::json value;
    input >> value;
    REQUIRE((input.good() || input.eof()));
    return value;
}

void require_finite_point_array(const nlohmann::json& value)
{
    REQUIRE(value.is_array());
    REQUIRE(value.size() == 3);
    for (const nlohmann::json& coordinate : value) {
        REQUIRE(coordinate.is_number());
        CHECK(std::isfinite(coordinate.get<double>()));
    }
}

TEST_CASE("inspection emits normalized STL and a versioned machine-readable summary")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = fixture_path("tetra_ascii.stl");
    const std::filesystem::path output = temporary.path() / "inspection";
    irop::MeshLimits limits;
    limits.max_input_bytes = std::filesystem::file_size(input) + 1;
    limits.max_vertices = 4;
    limits.max_triangles = 4;

    const irop::InspectionResult result = irop::inspect_stl(input, output, limits);

    REQUIRE(std::filesystem::is_regular_file(result.normalized_stl_path));
    REQUIRE(std::filesystem::is_regular_file(result.summary_path));
    CHECK(result.resolved_input_path == std::filesystem::weakly_canonical(input));
    CHECK(result.normalized_stl_path == std::filesystem::absolute(output).lexically_normal() / "normalized.stl");
    CHECK(result.summary_path == std::filesystem::absolute(output).lexically_normal() / "inspection-summary.json");
    CHECK(result.input_encoding == irop::StlEncoding::ascii);
    CHECK(result.input_bytes == std::filesystem::file_size(input));
    CHECK(result.mesh.vertex_count == 4);
    CHECK(result.mesh.triangle_count == 4);
    CHECK(result.limits.max_input_bytes == limits.max_input_bytes);
    CHECK(result.limits.max_vertices == limits.max_vertices);
    CHECK(result.limits.max_triangles == limits.max_triangles);

    const nlohmann::json summary = read_json(result.summary_path);
    REQUIRE(summary.is_object());
    CHECK(summary.at("schema_version") == 1);
    CHECK(summary.at("command") == "inspect");
    CHECK(summary.at("outcome").at("category") == "success");
    CHECK(summary.at("input").at("resolved_path") == path_as_utf8(result.resolved_input_path));
    CHECK(summary.at("input").at("stl_encoding") == "ascii");
    CHECK(summary.at("input").at("size_bytes") == result.input_bytes);
    CHECK(summary.at("output").at("normalized_stl") == path_as_utf8(result.normalized_stl_path));
    CHECK(summary.at("limits").at("max_input_bytes") == limits.max_input_bytes);
    CHECK(summary.at("limits").at("max_vertices") == limits.max_vertices);
    CHECK(summary.at("limits").at("max_triangles") == limits.max_triangles);
    CHECK(summary.at("mesh").at("vertex_count") == 4);
    CHECK(summary.at("mesh").at("triangle_count") == 4);
    require_finite_point_array(summary.at("mesh").at("bounds").at("minimum"));
    require_finite_point_array(summary.at("mesh").at("bounds").at("maximum"));
    REQUIRE(summary.at("versions").at("irop").is_string());
    CHECK_FALSE(summary.at("versions").at("irop").get<std::string>().empty());
    REQUIRE(summary.at("versions").at("vtk").is_string());
    CHECK_FALSE(summary.at("versions").at("vtk").get<std::string>().empty());
    REQUIRE(summary.at("warnings").is_array());
    CHECK(summary.at("warnings").empty());

    const irop::LoadedStl normalized = irop::read_stl(result.normalized_stl_path, {});
    CHECK(normalized.encoding == irop::StlEncoding::binary);
    const irop::MeshStatistics normalized_statistics = irop::validate_and_measure_mesh(normalized.mesh, {});
    CHECK(normalized_statistics.vertex_count == result.mesh.vertex_count);
    CHECK(normalized_statistics.triangle_count == result.mesh.triangle_count);
    CHECK(normalized_statistics.bounds.minimum.x == Approx(result.mesh.bounds.minimum.x));
    CHECK(normalized_statistics.bounds.minimum.y == Approx(result.mesh.bounds.minimum.y));
    CHECK(normalized_statistics.bounds.minimum.z == Approx(result.mesh.bounds.minimum.z));
    CHECK(normalized_statistics.bounds.maximum.x == Approx(result.mesh.bounds.maximum.x));
    CHECK(normalized_statistics.bounds.maximum.y == Approx(result.mesh.bounds.maximum.y));
    CHECK(normalized_statistics.bounds.maximum.z == Approx(result.mesh.bounds.maximum.z));
}

TEST_CASE("the checked-in inspection schema fixes the version-one object contract")
{
    const nlohmann::json schema =
        read_json(std::filesystem::path(IROP_TEST_SCHEMA_DIR) / "inspection-summary-v1.schema.json");

    CHECK(schema.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("additionalProperties") == false);
    REQUIRE(schema.at("required").is_array());
    for (const char* required_key : {
             "schema_version",
             "command",
             "outcome",
             "input",
             "output",
             "limits",
             "mesh",
             "versions",
             "warnings",
         }) {
        CHECK(std::find(schema.at("required").begin(), schema.at("required").end(), nlohmann::json(required_key)) !=
              schema.at("required").end());
    }
    CHECK(schema.at("properties").at("schema_version").at("const") == 1);
    CHECK(schema.at("properties").at("command").at("const") == "inspect");
    CHECK(schema.at("properties").at("outcome").at("properties").at("category").at("const") == "success");
    CHECK(schema.at("$defs").at("point").at("minItems") == 3);
    CHECK(schema.at("$defs").at("point").at("maxItems") == 3);
}

TEST_CASE("inspection rejects malformed input before publishing artifacts")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "truncated.stl";
    const std::filesystem::path output = temporary.path() / "output";
    irop::test::write_text_file(input,
                                "solid truncated\n"
                                "facet normal 0 0 1\n"
                                "outer loop\n"
                                "vertex 0 0 0\n"
                                "vertex 1 0 0\n"
                                "vertex 0 1 0\n");

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(input, output));
    }, irop::ErrorCategory::invalid_mesh);

    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("inspection reports resource exhaustion without creating a success result")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "output";
    irop::MeshLimits limits;
    limits.max_input_bytes = 1;

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(fixture_path("tetra_ascii.stl"), output, limits));
    }, irop::ErrorCategory::resource_limit);

    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("inspection translates an unusable output path to the output category")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output_file = temporary.path() / "not-a-directory";
    irop::test::write_text_file(output_file, "occupied");

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(fixture_path("tetra_ascii.stl"), output_file));
    }, irop::ErrorCategory::output_io);
}

TEST_CASE("inspection refuses to overwrite a previously published artifact set")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "output";
    const irop::InspectionResult first = irop::inspect_stl(fixture_path("tetra_ascii.stl"), output);
    const nlohmann::json first_summary = read_json(first.summary_path);
    const std::uintmax_t first_stl_size = std::filesystem::file_size(first.normalized_stl_path);

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(fixture_path("tetra_ascii.stl"), output));
    }, irop::ErrorCategory::output_io);

    CHECK(read_json(first.summary_path) == first_summary);
    CHECK(std::filesystem::file_size(first.normalized_stl_path) == first_stl_size);
    CHECK_FALSE(std::filesystem::exists(output / ".normalized.stl.irop-tmp"));
    CHECK_FALSE(std::filesystem::exists(output / ".inspection-summary.json.irop-tmp"));
}

TEST_CASE("inspection refuses a pre-existing individual artifact without modifying it")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "output";
    REQUIRE(std::filesystem::create_directories(output));
    const std::filesystem::path collision = output / "normalized.stl";
    irop::test::write_text_file(collision, "do not overwrite");

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(fixture_path("tetra_ascii.stl"), output));
    }, irop::ErrorCategory::output_io);

    std::ifstream input(collision, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(contents == "do not overwrite");
    CHECK_FALSE(std::filesystem::exists(output / "inspection-summary.json"));
}

TEST_CASE("inspection rejects non-finite coordinates from an untrusted STL")
{
    irop::test::TempDirectory temporary;
    std::vector<irop::test::BinaryFacet> facets = irop::test::binary_tetrahedron_facets();
    facets[0].vertices[0][0] = std::numeric_limits<float>::infinity();
    const std::filesystem::path input = temporary.path() / "infinite.stl";
    const std::filesystem::path output = temporary.path() / "output";
    irop::test::write_binary_stl(input, facets);

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(input, output));
    }, irop::ErrorCategory::invalid_mesh);

    CHECK_FALSE(std::filesystem::exists(output / "inspection-summary.json"));
}

TEST_CASE("inspection rejects a degenerate facet even when VTK merging removes it")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path input = temporary.path() / "degenerate.stl";
    const std::filesystem::path output = temporary.path() / "output";
    std::string contents = irop::test::ascii_tetrahedron();
    const std::size_t end_solid = contents.find("endsolid tetrahedron");
    REQUIRE(end_solid != std::string::npos);
    contents.insert(end_solid,
                    "facet normal 0 0 1\n"
                    "outer loop\n"
                    "vertex 0 0 0\n"
                    "vertex 0 0 0\n"
                    "vertex 1 0 0\n"
                    "endloop\n"
                    "endfacet\n");
    irop::test::write_text_file(input, contents);

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::inspect_stl(input, output));
    }, irop::ErrorCategory::invalid_mesh);

    CHECK_FALSE(std::filesystem::exists(output / "inspection-summary.json"));
}

}  // namespace
