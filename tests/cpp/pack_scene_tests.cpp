#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/collision.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/packing/pack_scene.hpp"
#include "support/test_support.hpp"

#ifndef IROP_TEST_SCHEMA_DIR
#error "IROP_TEST_SCHEMA_DIR must name the checked-in JSON schema directory"
#endif

namespace {

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    nlohmann::json value;
    input >> value;
    REQUIRE((input.good() || input.eof()));
    return value;
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

[[nodiscard]] bool has_pack_staging_directory(const std::filesystem::path& parent)
{
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent)) {
        if (entry.path().filename().string().starts_with(".irop-pack-staging-")) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] irop::PackOptions no_growth_options()
{
    irop::PackOptions options;
    options.initialization.object_count = 1;
    options.initialization.initial_volume_scale = 0.1;
    options.algorithm.final_volume_scale = 0.1;
    options.algorithm.scale_step_count = 1;
    options.algorithm.max_iterations_per_scale_step = 1;
    options.algorithm.adaptive_sampling = false;
    options.write_individual_objects = true;
    return options;
}

TEST_CASE("packing scene atomically publishes the complete successful artifact set")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "packed-tętra-网";
    const irop::PackOptions options = no_growth_options();

    const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

    REQUIRE(result.packing.status == irop::PackingStatus::success);
    REQUIRE(result.packed_objects_path.has_value());
    REQUIRE(result.container_output_path.has_value());
    REQUIRE(result.placements_path.has_value());
    CHECK(std::filesystem::is_regular_file(*result.packed_objects_path));
    CHECK(std::filesystem::is_regular_file(*result.container_output_path));
    CHECK(std::filesystem::is_regular_file(*result.placements_path));
    CHECK(std::filesystem::is_regular_file(result.run_summary_path));
    REQUIRE(result.individual_object_paths.size() == 1);
    CHECK(std::filesystem::is_regular_file(result.individual_object_paths.front()));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));

    CHECK(irop::read_stl(*result.packed_objects_path, {}).mesh.triangles.size() == 4);
    CHECK(irop::read_stl(*result.container_output_path, {}).mesh.triangles.size() == 12);

    const nlohmann::json placements = read_json(*result.placements_path);
    CHECK(placements.at("schema_version") == 1);
    CHECK(placements.at("command") == "pack");
    CHECK(placements.at("phase") == "final");
    CHECK(placements.at("placements").size() == 1);
    CHECK(placements.at("placements").at(0).at("input_to_world_matrix").size() == 4);

    const nlohmann::json summary = read_json(result.run_summary_path);
    CHECK(summary.at("schema_version") == 1);
    CHECK(summary.at("command") == "pack");
    CHECK(summary.at("outcome").at("category") == "success");
    CHECK(summary.at("validation").at("status") == "passed");
    CHECK(summary.at("validation").at("physical_scene_valid") == true);
    CHECK(summary.at("config").at("object_count") == 1);
    CHECK(summary.at("config").at("final_volume_scale") == 0.1);
    CHECK(summary.at("outputs").at("packed_objects_stl") == "packed-objects.stl");
    CHECK(summary.at("outputs").at("container_stl") == "container.stl");
    CHECK(summary.at("outputs").at("placements_json") == "placements.json");
    CHECK(summary.at("outputs").at("run_summary_json") == "run-summary.json");
    REQUIRE(summary.at("outputs").at("individual_object_stls").size() == 1);
    CHECK(summary.at("outputs").at("individual_object_stls").at(0) == "objects/object-000000.stl");
    CHECK(summary.at("versions").at("tetgen") == "1.6.0");
    CHECK(summary.at("versions").at("ipopt") == "3.14.19");
}

TEST_CASE("packing publishes a physically valid full-scale structured cylinder layout")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs {
        .object = temporary.path() / "cylinder.stl",
        .container = temporary.path() / "box.stl",
    };
    const irop::TriangleMesh object = irop::test::cylinder_mesh(45.23, 52.7535, 12);
    irop::write_stl(inputs.object, object);
    irop::write_stl(inputs.container, irop::test::box_mesh(175.0, 200.0, 142.5));
    const std::filesystem::path output = temporary.path() / "structured-packed";
    irop::PackOptions options = no_growth_options();
    options.initialization.object_count = 36;
    options.initialization.initial_volume_scale = 1.0;
    options.initialization.seed = 1918;
    options.initialization.max_sampling_attempts = 100;
    options.initialization.enable_structured_fallback = true;
    options.initialization.max_structured_candidates = 10'000;
    options.algorithm.final_volume_scale = 1.0;
    // A completed initialization must survive without invoking TetGen.
    options.limits.tetrahedralization.max_input_points = 1;

    const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

    REQUIRE(result.packing.status == irop::PackingStatus::success);
    CHECK(result.packing.state.initialization_method == irop::InitializationMethod::structured_grid);
    CHECK(result.packing.final_validation_performed);
    CHECK(result.packing.final_validation.physical_scene_valid());
    CHECK(result.packing.work.tetrahedralization_attempts == 0);
    CHECK(result.packing.work.local_solves == 0);
    CHECK(result.packing.history.empty());
    REQUIRE(result.packing.state.transforms.size() == 36);
    for (const irop::Transform& transform : result.packing.state.transforms) {
        CHECK(transform.volume_scale == 1.0);
    }

    REQUIRE(result.packed_objects_path.has_value());
    REQUIRE(result.container_output_path.has_value());
    REQUIRE(result.placements_path.has_value());
    REQUIRE(result.individual_object_paths.size() == 36);
    std::vector<irop::TriangleMesh> serialized_objects;
    for (const std::filesystem::path& path : result.individual_object_paths) {
        const irop::LoadedStl serialized = irop::read_stl(path, {});
        CHECK(serialized.encoding == irop::StlEncoding::binary);
        CHECK(serialized.mesh.triangles.size() == object.triangles.size());
        serialized_objects.push_back(serialized.mesh);
    }
    const irop::LoadedStl serialized_container = irop::read_stl(*result.container_output_path, {});
    CHECK(irop::validate_scene_collisions(serialized_objects, serialized_container.mesh).physical_scene_valid());
    CHECK(irop::read_stl(*result.packed_objects_path, {}).mesh.triangles.size() == 36 * object.triangles.size());
    CHECK(read_json(*result.placements_path).at("placements").size() == 36);
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));

    const nlohmann::json summary = read_json(result.run_summary_path);
    CHECK(summary.at("validation").at("status") == "passed");
    CHECK(summary.at("config").at("object_count") == 36);
    CHECK(summary.at("config").at("initial_volume_scale") == 1.0);
    CHECK(summary.at("config").at("final_volume_scale") == 1.0);
    CHECK(summary.at("config").at("seed") == 1918);
    const nlohmann::json& initialization_limits = summary.at("config").at("initialization_limits");
    CHECK(initialization_limits.at("enable_structured_fallback") == true);
    CHECK(initialization_limits.at("max_sampling_attempts") == 100);
    CHECK(initialization_limits.at("max_structured_candidates") == 10'000);
    const nlohmann::json& initialization = summary.at("initialization");
    CHECK(initialization.at("policy") == "structured-aabb-grid");
    CHECK(initialization.at("method") == "structured_grid");
    CHECK(initialization.at("sampling_attempts") == 100);
    CHECK(initialization.at("structured_candidates").get<std::uint64_t>() >= 36);
    CHECK(initialization.at("structured_candidates").get<std::uint64_t>() <= 10'000);
    CHECK(initialization.at("orientations_examined").get<std::uint64_t>() > 0);
    CHECK(initialization.at("reference_accepted_count").get<std::uint64_t>() < 36);
    CHECK(initialization.at("minimum_boundary_clearance").is_null());
    CHECK(initialization.at("minimum_center_distance").is_null());
}

TEST_CASE("packing shares the object-pair budget with binary-STL output validation")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs {
        .object = temporary.path() / "cylinder.stl",
        .container = temporary.path() / "box.stl",
    };
    irop::write_stl(inputs.object, irop::test::cylinder_mesh(0.25, 0.5, 12));
    irop::write_stl(inputs.container, irop::test::cube_mesh(5.0));
    const std::filesystem::path output = temporary.path() / "pair-budget";
    irop::PackOptions options = no_growth_options();
    options.initialization.object_count = 3;
    options.initialization.initial_volume_scale = 1.0;
    options.initialization.enable_structured_fallback = false;
    options.algorithm.final_volume_scale = 1.0;

    SECTION("an engine-only budget cannot publish success artifacts")
    {
        options.limits.collision.max_object_pair_checks = 3;
        const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

        CHECK(result.packing.status == irop::PackingStatus::resource_exhausted);
        CHECK(result.packing.work.collision.object_pairs_examined == 3);
        CHECK_FALSE(result.packing.final_validation_performed);
        CHECK_FALSE(result.packed_objects_path.has_value());
        CHECK_FALSE(result.container_output_path.has_value());
        CHECK_FALSE(result.placements_path.has_value());
        CHECK(result.individual_object_paths.empty());
        CHECK_FALSE(std::filesystem::exists(output / "packed-objects.stl"));
        CHECK_FALSE(std::filesystem::exists(output / "container.stl"));
        CHECK_FALSE(std::filesystem::exists(output / "placements.json"));
        CHECK_FALSE(std::filesystem::exists(output / "objects"));
        CHECK_FALSE(has_pack_staging_directory(temporary.path()));
        const nlohmann::json summary = read_json(result.run_summary_path);
        CHECK(summary.at("outcome").at("category") == "resource_exhausted");
        CHECK(summary.at("validation").at("status") == "not_run");
        CHECK(summary.at("outputs").at("packed_objects_stl").is_null());
        CHECK(summary.at("config").at("engine_limits").at("collision").at("max_object_pair_checks") == 3);
    }

    SECTION("the exact budget for both validation passes succeeds")
    {
        options.limits.collision.max_object_pair_checks = 6;
        const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

        REQUIRE(result.packing.status == irop::PackingStatus::success);
        CHECK(result.packing.work.collision.object_pairs_examined == 6);
        CHECK(result.packing.final_validation.work.object_pairs_examined == 3);
        CHECK(result.packing.final_validation_performed);
        CHECK(result.packing.final_validation.physical_scene_valid());
        REQUIRE(result.packed_objects_path.has_value());
        CHECK(std::filesystem::is_regular_file(*result.packed_objects_path));
        const nlohmann::json summary = read_json(result.run_summary_path);
        CHECK(summary.at("outcome").at("category") == "success");
        CHECK(summary.at("work").at("collision").at("object_pairs_examined") == 6);
        CHECK(summary.at("config").at("engine_limits").at("collision").at("max_object_pair_checks") == 6);
    }
}

TEST_CASE("expected packing failure atomically publishes only an unsuccessful run summary")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "unsuccessful";
    irop::PackOptions options = no_growth_options();
    options.write_individual_objects = false;
    options.algorithm.final_volume_scale = 0.2;
    options.limits.tetrahedralization.max_input_points = 1;

    const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

    REQUIRE(result.packing.status == irop::PackingStatus::resource_exhausted);
    CHECK_FALSE(result.packed_objects_path.has_value());
    CHECK_FALSE(result.container_output_path.has_value());
    CHECK_FALSE(result.placements_path.has_value());
    CHECK(result.individual_object_paths.empty());
    CHECK(std::filesystem::is_regular_file(result.run_summary_path));
    CHECK_FALSE(std::filesystem::exists(output / "packed-objects.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "container.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "placements.json"));
    CHECK_FALSE(std::filesystem::exists(output / "objects"));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));

    const nlohmann::json summary = read_json(result.run_summary_path);
    CHECK(summary.at("outcome").at("category") == "resource_exhausted");
    CHECK(summary.at("outputs").at("packed_objects_stl").is_null());
    CHECK(summary.at("outputs").at("container_stl").is_null());
    CHECK(summary.at("outputs").at("placements_json").is_null());
    CHECK(summary.at("outputs").at("run_summary_json") == "run-summary.json");
    CHECK(summary.at("outputs").at("individual_object_stls").empty());
    CHECK(summary.at("work").at("tetrahedralization_attempts").get<std::uint64_t>() >= 1);
}

TEST_CASE("cancellation after engine success publishes only a cancelled run summary")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "cancelled";
    irop::PackOptions options = no_growth_options();
    bool cancel = false;
    options.callbacks.cancellation_requested = [&cancel]() {
        return cancel;
    };
    options.callbacks.progress = [&cancel](const irop::PackingProgress& progress) {
        if (progress.phase == irop::PackingProgressPhase::finished) {
            cancel = true;
        }
    };

    const irop::PackSceneResult result = irop::pack_scene(inputs.object, inputs.container, output, options);

    REQUIRE(result.packing.status == irop::PackingStatus::cancelled);
    CHECK(result.packing.final_validation_performed);
    CHECK_FALSE(result.packed_objects_path.has_value());
    CHECK_FALSE(result.container_output_path.has_value());
    CHECK_FALSE(result.placements_path.has_value());
    CHECK(result.individual_object_paths.empty());
    CHECK(std::filesystem::is_regular_file(result.run_summary_path));
    CHECK_FALSE(std::filesystem::exists(output / "packed-objects.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "container.stl"));
    CHECK_FALSE(std::filesystem::exists(output / "placements.json"));
    CHECK_FALSE(std::filesystem::exists(output / "objects"));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));

    const nlohmann::json summary = read_json(result.run_summary_path);
    CHECK(summary.at("outcome").at("category") == "cancelled");
    CHECK(summary.at("validation").at("status") == "passed");
    CHECK(summary.at("validation").at("physical_scene_valid") == true);
    CHECK(summary.at("outputs").at("packed_objects_stl").is_null());
    CHECK(summary.at("outputs").at("container_stl").is_null());
    CHECK(summary.at("outputs").at("placements_json").is_null());
    CHECK(summary.at("outputs").at("individual_object_stls").empty());
}

TEST_CASE("cancellation before input loading leaves no output artifacts")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "cancelled-before-input";
    irop::PackOptions options = no_growth_options();
    options.callbacks.cancellation_requested = []() {
        return true;
    };

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::pack_scene(temporary.path() / "missing-object.stl",
                                           temporary.path() / "missing-container.stl", output, options));
    }, irop::ErrorCategory::cancelled);

    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));
}

TEST_CASE("invalid packing configuration creates no output directory")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "invalid";
    irop::PackOptions options = no_growth_options();
    options.algorithm.final_volume_scale = 0.0;

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::pack_scene(inputs.object, inputs.container, output, options));
    }, irop::ErrorCategory::invalid_configuration);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));
}

TEST_CASE("invalid packing configuration is rejected before input path resolution")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path output = temporary.path() / "invalid-before-io";
    irop::PackOptions options;
    options.algorithm.final_volume_scale = 0.0;

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::pack_scene(temporary.path() / "missing-object.stl",
                                           temporary.path() / "missing-container.stl", output, options));
    }, irop::ErrorCategory::invalid_configuration);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));
}

TEST_CASE("malformed packing input creates no output directory")
{
    irop::test::TempDirectory temporary;
    const std::filesystem::path object = temporary.path() / "malformed.stl";
    const std::filesystem::path container = temporary.path() / "container.stl";
    const std::filesystem::path output = temporary.path() / "malformed-output";
    {
        std::ofstream malformed(object, std::ios::binary);
        REQUIRE(malformed.good());
        malformed << "not an STL";
    }
    irop::write_stl(container, irop::test::cube_mesh(5.0));

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::pack_scene(object, container, output, no_growth_options()));
    }, irop::ErrorCategory::invalid_mesh);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));
}

TEST_CASE("packing refuses to overwrite an existing output directory")
{
    irop::test::TempDirectory temporary;
    const SceneInputs inputs = write_scene_inputs(temporary.path());
    const std::filesystem::path output = temporary.path() / "existing";
    REQUIRE(std::filesystem::create_directory(output));
    irop::PackOptions options = no_growth_options();
    options.algorithm.final_volume_scale = 0.2;
    options.limits.tetrahedralization.max_input_points = 1;

    irop::test::require_error_category([&]() {
        static_cast<void>(irop::pack_scene(inputs.object, inputs.container, output, options));
    }, irop::ErrorCategory::output_io);
    CHECK(std::filesystem::is_empty(output));
    CHECK_FALSE(has_pack_staging_directory(temporary.path()));
}

TEST_CASE("checked-in packing schemas fix the version-one result contracts")
{
    const std::filesystem::path schema_directory = IROP_TEST_SCHEMA_DIR;
    const nlohmann::json placements = read_json(schema_directory / "packing-placements-v1.schema.json");
    CHECK(placements.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(placements.at("type") == "object");
    CHECK(placements.at("additionalProperties") == false);
    CHECK(placements.at("properties").at("schema_version").at("const") == 1);
    CHECK(placements.at("properties").at("command").at("const") == "pack");
    CHECK(placements.at("properties").at("phase").at("const") == "final");
    CHECK(placements.at("$defs").at("matrix").at("minItems") == 4);
    CHECK(placements.at("$defs").at("matrix").at("maxItems") == 4);

    const nlohmann::json summary = read_json(schema_directory / "packing-run-summary-v1.schema.json");
    CHECK(summary.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(summary.at("type") == "object");
    CHECK(summary.at("additionalProperties") == false);
    CHECK(summary.at("properties").at("schema_version").at("const") == 1);
    CHECK(summary.at("properties").at("command").at("const") == "pack");
    CHECK(summary.at("properties").at("outcome").at("additionalProperties") == false);
    CHECK(summary.at("properties").at("outputs").at("additionalProperties") == false);
    CHECK(summary.at("$defs").at("config").at("additionalProperties") == false);
    CHECK(summary.at("$defs").at("packing_work").at("additionalProperties") == false);
    CHECK(summary.at("$defs").at("validation").at("additionalProperties") == false);
    const nlohmann::json& initialization_schema = summary.at("$defs").at("initialization");
    CHECK(initialization_schema.at("properties").at("method").at("enum") ==
          nlohmann::json::array({ "reference_origin", "random_rejection", "structured_grid" }));
    for (const char* counter :
         { "sampling_attempts", "structured_candidates", "orientations_examined", "reference_accepted_count" }) {
        CHECK(initialization_schema.at("properties").at(counter).at("type") == "integer");
    }
    const nlohmann::json& structured_contract = initialization_schema.at("allOf").at(0).at("then");
    CHECK(structured_contract.at("properties").at("minimum_boundary_clearance").at("type") == "null");
    CHECK(structured_contract.at("properties").at("minimum_center_distance").at("type") == "null");
    const nlohmann::json& outcome_contract = summary.at("allOf").at(0);
    CHECK(outcome_contract.at("if").at("properties").at("outcome").at("properties").at("category").at("const") ==
          "success");
    CHECK(outcome_contract.at("then")
              .at("properties")
              .at("outputs")
              .at("properties")
              .at("packed_objects_stl")
              .at("const") == "packed-objects.stl");
    CHECK(outcome_contract.at("then")
              .at("properties")
              .at("validation")
              .at("properties")
              .at("physical_scene_valid")
              .at("const") == true);
    CHECK(outcome_contract.at("else")
              .at("properties")
              .at("outputs")
              .at("properties")
              .at("packed_objects_stl")
              .at("const")
              .is_null());
}

}  // namespace
