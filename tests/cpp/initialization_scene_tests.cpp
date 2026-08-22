#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

#include "irop/error.hpp"
#include "irop/initialization/initialize_scene.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"
#include "support/test_support.hpp"

#ifndef IROP_TEST_SCHEMA_DIR
#error "IROP_TEST_SCHEMA_DIR must name the checked-in JSON schema directory"
#endif

namespace {

using Catch::Approx;

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    nlohmann::json value;
    input >> value;
    REQUIRE((input.good() || input.eof()));
    return value;
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

struct SceneInputs {
    std::filesystem::path object;
    std::filesystem::path container;
};

[[nodiscard]] SceneInputs write_scene_inputs(const std::filesystem::path& directory)
{
    const SceneInputs inputs {
        .object = directory / "object.stl",
        .container = directory / "container.stl",
    };
    irop::write_stl(inputs.object, irop::test::tetrahedron_mesh());
    irop::write_stl(inputs.container, irop::test::cube_mesh(5.0));
    return inputs;
}

TEST_CASE("initialization scene emits combined individual container and placement artifacts")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "initial-tętra-网";
    irop::InitializationOptions options;
    options.packing.object_count = 3;
    options.packing.seed = 123;
    options.write_individual_objects = true;

    const irop::InitializationResult result = irop::initialize_scene(inputs.object, inputs.container, output, options);

    CHECK(std::filesystem::is_regular_file(result.initialized_objects_path));
    CHECK(std::filesystem::is_regular_file(result.container_output_path));
    CHECK(std::filesystem::is_regular_file(result.placements_path));
    CHECK(std::filesystem::is_regular_file(result.run_summary_path));
    REQUIRE(result.individual_object_paths.size() == 3);
    for (const std::filesystem::path& path : result.individual_object_paths) {
        CHECK(std::filesystem::is_regular_file(path));
        CHECK(irop::read_stl(path, {}).mesh.triangles.size() == 4);
    }

    const irop::LoadedStl combined = irop::read_stl(result.initialized_objects_path, {});
    const irop::LoadedStl container = irop::read_stl(result.container_output_path, {});
    CHECK(combined.mesh.triangles.size() == 12);
    CHECK(combined.mesh.vertices.size() == 12);
    CHECK(container.mesh.triangles.size() == 12);

    const nlohmann::json placements = read_json(result.placements_path);
    CHECK(placements.at("schema_version") == 1);
    CHECK(placements.at("command") == "initialize");
    CHECK(placements.at("phase") == "initialization");
    CHECK(placements.at("coordinate_units") == "arbitrary_consistent");
    CHECK(placements.at("transform_convention").at("rotation_order") == "Ry*Rz*Rx");
    CHECK(placements.at("transform_convention").at("scale_kind") == "volume");
    REQUIRE(placements.at("placements").size() == 3);
    for (std::size_t index = 0; index < 3; ++index) {
        const nlohmann::json& placement = placements.at("placements").at(index);
        CHECK(placement.at("object_id") == index);
        CHECK(placement.at("object_identity") == "object-template");
        REQUIRE(placement.at("rotation_radians").size() == 3);
        REQUIRE(placement.at("translation").size() == 3);
        REQUIRE(placement.at("input_to_world_matrix").size() == 4);
        for (const nlohmann::json& row : placement.at("input_to_world_matrix")) {
            CHECK(row.size() == 4);
        }
    }
    const nlohmann::json& first_matrix = placements.at("placements").at(0).at("input_to_world_matrix");
    const irop::Point3 transformed_source_origin {
        first_matrix.at(0).at(3).get<double>(),
        first_matrix.at(1).at(3).get<double>(),
        first_matrix.at(2).at(3).get<double>(),
    };
    const irop::LoadedStl first_object = irop::read_stl(result.individual_object_paths.front(), {});
    CHECK(std::any_of(first_object.mesh.vertices.begin(), first_object.mesh.vertices.end(),
                      [&](const irop::Point3& point) {
        return point.x == Approx(transformed_source_origin.x).margin(1.0e-5) &&
               point.y == Approx(transformed_source_origin.y).margin(1.0e-5) &&
               point.z == Approx(transformed_source_origin.z).margin(1.0e-5);
    }));

    const nlohmann::json summary = read_json(result.run_summary_path);
    CHECK(summary.at("schema_version") == 1);
    CHECK(summary.at("command") == "initialize");
    CHECK(summary.at("outcome").at("category") == "success");
    CHECK(summary.at("config").at("object_count") == 3);
    CHECK(summary.at("config").at("seed") == 123);
    CHECK(summary.at("config").at("max_geometry_query_triangle_visits") ==
          options.packing.max_geometry_query_triangle_visits);
    CHECK(summary.at("config").at("max_pairwise_distance_checks") == options.packing.max_pairwise_distance_checks);
    CHECK(summary.at("config").at("max_surface_intersection_triangle_pairs") ==
          options.packing.max_surface_intersection_triangle_pairs);
    CHECK(summary.at("sampling").at("random_generator") == "numpy-legacy-mt19937-compatible");
    CHECK(summary.at("sampling").at("geometry_query_triangle_visits").get<std::uint64_t>() > 0);
    CHECK(summary.at("sampling").at("pairwise_distance_checks").get<std::uint64_t>() > 0);
    CHECK(summary.at("sampling").at("surface_intersection_triangle_pairs").get<std::uint64_t>() == 0);
    CHECK(summary.at("timings").at("initialization_seconds").get<double>() >= 0.0);
    CHECK(summary.at("timings").at("artifact_preparation_seconds").get<double>() >= 0.0);
    CHECK(summary.at("outputs").at("placements_json") == path_as_utf8(result.placements_path));
    REQUIRE(summary.at("outputs").at("individual_object_stls").size() == 3);
    CHECK(summary.at("versions").at("eigen") == "3.4.1");
}

TEST_CASE("placement transform content is reproducible across output directories")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    irop::InitializationOptions options;
    options.packing.object_count = 4;
    options.packing.seed = 77;

    const irop::InitializationResult first =
        irop::initialize_scene(inputs.object, inputs.container, temporary.path() / "first", options);
    const irop::InitializationResult second =
        irop::initialize_scene(inputs.object, inputs.container, temporary.path() / "second", options);
    const nlohmann::json first_json = read_json(first.placements_path);
    const nlohmann::json second_json = read_json(second.placements_path);

    CHECK(first_json == second_json);
    const nlohmann::json first_summary = read_json(first.run_summary_path);
    const nlohmann::json second_summary = read_json(second.run_summary_path);
    CHECK(first_summary.at("sampling") == second_summary.at("sampling"));
    CHECK(first_summary.at("config") == second_summary.at("config"));
}

TEST_CASE("input-to-world matrix includes source point-centroid centering")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    irop::InitializationOptions options;
    options.packing.object_count = 1;
    options.packing.initial_volume_scale = 1.0;

    const irop::InitializationResult result =
        irop::initialize_scene(inputs.object, inputs.container, temporary.path() / "output", options);
    const nlohmann::json matrix = read_json(result.placements_path).at("placements").at(0).at("input_to_world_matrix");
    CHECK(matrix.at(0).at(3).get<double>() == Approx(-0.25));
    CHECK(matrix.at(1).at(3).get<double>() == Approx(-0.25));
    CHECK(matrix.at(2).at(3).get<double>() == Approx(-0.25));
    CHECK(matrix.at(3).at(3).get<double>() == Approx(1.0));
}

TEST_CASE("checked-in placement schema fixes the version-one contract")
{
    const nlohmann::json schema = read_json(std::filesystem::path(IROP_TEST_SCHEMA_DIR) / "placements-v1.schema.json");
    CHECK(schema.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("additionalProperties") == false);
    for (const char* required : {
             "schema_version",
             "command",
             "phase",
             "coordinate_units",
             "transform_convention",
             "object_template",
             "placements",
         }) {
        CHECK(std::find(schema.at("required").begin(), schema.at("required").end(), nlohmann::json(required)) !=
              schema.at("required").end());
    }
    CHECK(schema.at("properties").at("schema_version").at("const") == 1);
    CHECK(schema.at("properties").at("command").at("const") == "initialize");
    CHECK(schema.at("properties").at("transform_convention").at("properties").at("rotation_order").at("const") ==
          "Ry*Rz*Rx");
    CHECK(schema.at("$defs").at("matrix").at("minItems") == 4);
    CHECK(schema.at("$defs").at("matrix").at("maxItems") == 4);
}

TEST_CASE("checked-in initialization run-summary schema fixes the success contract")
{
    const nlohmann::json schema =
        read_json(std::filesystem::path(IROP_TEST_SCHEMA_DIR) / "initialization-run-summary-v1.schema.json");
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("additionalProperties") == false);
    for (const char* required : {
             "schema_version",
             "command",
             "outcome",
             "inputs",
             "config",
             "sampling",
             "timings",
             "outputs",
             "versions",
             "warnings",
         }) {
        CHECK(std::find(schema.at("required").begin(), schema.at("required").end(), nlohmann::json(required)) !=
              schema.at("required").end());
    }
    CHECK(schema.at("properties").at("outcome").at("properties").at("category").at("const") == "success");
    CHECK(schema.at("properties").at("versions").at("additionalProperties") == false);
}

TEST_CASE("initialization refuses to overwrite any planned artifact")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "output";
    REQUIRE(std::filesystem::create_directory(output));
    const std::filesystem::path collision = output / "container.stl";
    irop::test::write_text_file(collision, "preserve me");

    irop::InitializationOptions options;
    options.packing.object_count = 2;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_scene(inputs.object, inputs.container, output, options));
    }, irop::ErrorCategory::output_io);

    std::ifstream input(collision, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(contents == "preserve me");
    CHECK_FALSE(std::filesystem::exists(output / "initialized-objects.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "placements.json"));
    CHECK_FALSE(std::filesystem::exists(output / "run-summary.json"));
}

TEST_CASE("failed initialization publishes no success artifacts")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path object_path = temporary.path() / "large-object.stl";
    const std::filesystem::path container_path = temporary.path() / "small-container.stl";
    irop::write_stl(object_path, irop::test::cube_mesh(2.0));
    irop::write_stl(container_path, irop::test::cube_mesh(1.0));
    const std::filesystem::path output = temporary.path() / "output";
    irop::InitializationOptions options;
    options.packing.object_count = 2;
    options.packing.initial_volume_scale = 1.0;
    options.packing.max_sampling_attempts = 2;

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::initialize_scene(object_path, container_path, output, options));
    }, irop::ErrorCategory::resource_limit);
    CHECK_FALSE(std::filesystem::exists(output));
}

}  // namespace
