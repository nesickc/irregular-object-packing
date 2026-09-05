#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/initialization/initialize_scene.hpp"
#include "irop/io/run_scene.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/packing/pack_scene.hpp"
#include "support/test_support.hpp"

namespace {

using Json = nlohmann::json;
using Catch::Approx;

[[nodiscard]] Json display_summary()
{
    return {
        { "schema_version", 1                                                                                      },
        { "command",        "pack"                                                                                 },
        { "outcome",        { { "category", "success" }, { "diagnostic", "completed" } }                           },
        { "config",
         { { "object_count", 2 }, { "seed", 42 }, { "initial_volume_scale", 1.0 }, { "final_volume_scale", 1.0 } } },
        { "metrics",
         { { "object_count", 2 },
            { "objects_at_final_target", 2 },
            { "target_volume_scale", 1.0 },
            { "minimum_volume_scale", 1.0 },
            { "maximum_volume_scale", 1.0 },
            { "mean_volume_scale", 1.0 },
            { "packing_fraction", 0.02 } }                                                                         },
        { "validation",     { { "status", "passed" }, { "physical_scene_valid", true } }                           },
        { "outputs",
         { { "packed_objects_stl", "packed-objects.stl" },
            { "container_stl", "container.stl" },
            { "placements_json", "placements.json" },
            { "run_summary_json", "run-summary.json" },
            { "individual_object_stls", Json::array() } }                                                          },
        { "warnings",       Json::array({ "example recorded warning" })                                            },
        // Source paths are metadata only; these deliberately do not exist.
        { "inputs",
         { { "object", { { "resolved_path", "missing-source-object.stl" } } },
            { "container", { { "resolved_path", "missing-source-container.stl" } } } }                             },
    };
}

void write_summary(const std::filesystem::path& path, const Json& summary)
{
    irop::test::write_text_file(path, summary.dump());
}

void write_display_meshes(const std::filesystem::path& directory)
{
    const auto cube = irop::test::cube_mesh();
    const std::vector objects {
        irop::transform_mesh(cube, irop::Transform { .translation = { -2.0, 0.0, 0.0 } }
          ),
        irop::transform_mesh(cube, irop::Transform { .translation = { 2.0, 0.0, 0.0 }  }
          ),
    };
    irop::write_stl(directory / "packed-objects.stl", irop::combine_meshes(objects));
    irop::write_stl(directory / "container.stl", irop::test::cube_mesh(6.0));
}

TEST_CASE("saved scenes load combined disconnected geometry without original inputs", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto directory = temporary.path() / std::filesystem::path(u8"saved-tętra-网");
    REQUIRE(std::filesystem::create_directory(directory));
    write_display_meshes(directory);
    const auto summary = directory / "run-summary.json";
    write_summary(summary, display_summary());

    const auto scene = irop::load_run_scene(summary);

    CHECK(scene.success);
    CHECK(scene.command == "pack");
    CHECK(scene.status == "success");
    CHECK(scene.object_count == 2);
    CHECK(scene.seed == 42);
    CHECK(scene.recorded_physical_validity == true);
    CHECK(scene.packing_fraction == Approx(0.02));
    CHECK(scene.warnings == std::vector<std::string> { "example recorded warning" });
    REQUIRE(scene.objects);
    REQUIRE(scene.container);
    CHECK(scene.objects->triangles.size() == 24);
    CHECK(scene.container->triangles.size() == 12);
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::ClosedMeshQuery(*scene.objects));
    }, irop::ErrorCategory::invalid_mesh);
}

TEST_CASE("saved scenes accept real packing and relocated initialization artifact contracts", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto object = temporary.path() / "source-object.stl";
    const auto container = temporary.path() / "source-container.stl";
    irop::write_stl(object, irop::test::tetrahedron_mesh());
    irop::write_stl(container, irop::test::cube_mesh(6.0));
    const auto output = temporary.path() / "original-output";
    const auto relocated = temporary.path() / "relocated-output";

    SECTION("packing")
    {
        irop::PackOptions options;
        options.initialization.object_count = 10;
        options.initialization.initial_volume_scale = 0.1;
        options.algorithm.final_volume_scale = 0.1;
        options.algorithm.scale_step_count = 1;
        const auto result = irop::pack_scene(object, container, output, options);
        REQUIRE(result.packing.succeeded());
        std::filesystem::rename(output, relocated);
        REQUIRE(std::filesystem::remove(object));
        REQUIRE(std::filesystem::remove(container));
        const auto scene = irop::load_run_scene(relocated / "run-summary.json");
        CHECK(scene.success);
        CHECK(scene.command == "pack");
        CHECK(scene.recorded_physical_validity == true);
        CHECK(scene.object_count == 10);
        CHECK(scene.minimum_volume_scale == 0.1);
        CHECK(scene.mean_volume_scale == Approx(0.1));
        CHECK(scene.objects->triangles.size() == 40);
    }
    SECTION("initialization keeps absolute publication paths as metadata after relocation")
    {
        irop::InitializationOptions options;
        options.packing.object_count = 2;
        options.packing.initial_volume_scale = 0.25;
        options.write_individual_objects = true;
        static_cast<void>(irop::initialize_scene(object, container, output, options));
        std::filesystem::rename(output, relocated);
        REQUIRE(std::filesystem::remove(object));
        REQUIRE(std::filesystem::remove(container));
        const auto scene = irop::load_run_scene(relocated / "run-summary.json");
        CHECK(scene.success);
        CHECK(scene.command == "initialize");
        CHECK_FALSE(scene.recorded_physical_validity);
        CHECK_FALSE(scene.packing_fraction);
        CHECK(scene.target_volume_scale == 0.25);
        CHECK(scene.object_count == 2);
        CHECK(scene.objects->triangles.size() == 8);
    }
}

TEST_CASE("saved unsuccessful scenes retain diagnostics without reading success geometry", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto path = temporary.path() / "run-summary.json";
    for (const auto* status : { "cancelled", "resource_exhausted", "infeasible", "iteration_limit", "time_limit" }) {
        auto document = display_summary();
        document["outcome"] = {
            { "category",   status                     },
            { "diagnostic", "bounded unsuccessful run" }
        };
        document["outputs"]["packed_objects_stl"] = nullptr;
        document["outputs"]["container_stl"] = nullptr;
        document["outputs"]["placements_json"] = nullptr;
        document["validation"] = {
            { "status",               "not_run" },
            { "physical_scene_valid", nullptr   }
        };
        document["metrics"]["minimum_volume_scale"] = nullptr;
        document["metrics"]["maximum_volume_scale"] = nullptr;
        document["metrics"]["mean_volume_scale"] = nullptr;
        document["metrics"]["packing_fraction"] = nullptr;
        write_summary(path, document);
        const auto scene = irop::load_run_scene(path);
        CHECK_FALSE(scene.success);
        CHECK(scene.status == status);
        CHECK(scene.diagnostic == "bounded unsuccessful run");
        CHECK_FALSE(scene.objects);
        CHECK_FALSE(scene.container);
        CHECK_FALSE(scene.recorded_physical_validity);
    }
    auto document = display_summary();
    document["outcome"]["category"] = "cancelled";
    document["outputs"]["packed_objects_stl"] = nullptr;
    document["outputs"]["container_stl"] = nullptr;
    document["outputs"]["placements_json"] = nullptr;
    write_summary(path, document);
    CHECK(irop::load_run_scene(path).recorded_physical_validity == true);
}

TEST_CASE("saved scene loading rejects inconsistent outcomes and unsafe artifact names", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    write_display_meshes(temporary.path());
    const auto path = temporary.path() / "run-summary.json";
    std::vector<Json> invalid;
    auto append = [&]() -> Json& {
        invalid.push_back(display_summary());
        return invalid.back();
    };
    append()["schema_version"] = 2;
    append()["command"] = "inspect";
    append()["outcome"]["category"] = "unknown";
    append()["outcome"]["category"] = "cancelled";
    append()["validation"]["physical_scene_valid"] = false;
    append()["validation"] = {
        { "status",               "not_run" },
        { "physical_scene_valid", nullptr   }
    };
    append()["metrics"]["object_count"] = 1;
    append()["metrics"]["objects_at_final_target"] = 1;
    append()["metrics"]["target_volume_scale"] = 0.5;
    append()["metrics"]["minimum_volume_scale"] = 0.5;
    append()["metrics"]["packing_fraction"] = -0.5;
    append()["config"]["object_count"] = -1;
    append()["config"]["object_count"] = 0;
    append()["config"]["seed"] = 1.5;
    append()["config"]["final_volume_scale"] = 0.0;
    append()["outputs"]["packed_objects_stl"] = "../packed-objects.stl";
    append()["outputs"]["packed_objects_stl"] = (temporary.path() / "packed-objects.stl").generic_string();
    append()["outputs"]["container_stl"] = "unexpected.stl";
    append()["outputs"]["placements_json"] = "../placements.json";
    append()["outputs"]["run_summary_json"] = "elsewhere.json";
    append()["warnings"] = "not an array";
    for (const auto& document : invalid) {
        write_summary(path, document);
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::input_io);
    }
}

TEST_CASE("saved scene JSON parsing is bounded and rejects ambiguous documents", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto path = temporary.path() / "run-summary.json";
    auto document = display_summary();
    SECTION("duplicate keys")
    {
        irop::test::write_text_file(path, R"({"schema_version":1,"schema_version":1})");
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::input_io);
    }
    SECTION("truncated or overflowing number")
    {
        for (const auto* text : { "{", "{\"schema_version\":1e999}" }) {
            irop::test::write_text_file(path, text);
            irop::test::require_error_category([&]() {
                static_cast<void>(irop::load_run_scene(path));
            }, irop::ErrorCategory::input_io);
        }
    }
    SECTION("configured structural budgets")
    {
        write_summary(path, document);
        std::vector<irop::RunSceneLimits> limits(7);
        limits[0].max_summary_bytes = 1;
        limits[1].max_json_depth = 1;
        limits[2].max_json_nodes = 2;
        limits[3].max_json_container_entries = 1;
        limits[4].max_string_bytes = 2;
        limits[5].max_object_count = 1;
        limits[6].max_summary_bytes = 0;
        for (std::size_t index = 0; index < limits.size(); ++index) {
            irop::test::require_error_category([&]() {
                static_cast<void>(irop::load_run_scene(path, limits[index]));
            }, index == 6 ? irop::ErrorCategory::invalid_configuration : irop::ErrorCategory::resource_limit);
        }
    }
    SECTION("integer range")
    {
        document["config"]["seed"] = std::numeric_limits<std::uint64_t>::max();
        write_summary(path, document);
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::resource_limit);
    }
    SECTION("bounded warning text")
    {
        document["warnings"] = Json::array({ std::string(1025, 'x') });
        write_summary(path, document);
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::resource_limit);
    }
}

TEST_CASE("saved scenes enforce an aggregate geometry budget and reject damaged artifacts", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto path = temporary.path() / "run-summary.json";
    write_summary(path, display_summary());
    write_display_meshes(temporary.path());
    irop::RunSceneLimits limits;
    limits.mesh_limits.max_vertices = 24;
    limits.mesh_limits.max_triangles = 36;
    limits.mesh_limits.max_input_bytes = std::filesystem::file_size(temporary.path() / "packed-objects.stl") +
                                         std::filesystem::file_size(temporary.path() / "container.stl");
    CHECK(irop::load_run_scene(path, limits).success);
    SECTION("triangles") { --limits.mesh_limits.max_triangles; }
    SECTION("vertices") { --limits.mesh_limits.max_vertices; }
    SECTION("bytes") { --limits.mesh_limits.max_input_bytes; }
    SECTION("truncated STL")
    {
        irop::test::write_text_file(temporary.path() / "container.stl", "truncated");
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::invalid_mesh);
        return;
    }
    SECTION("missing STL")
    {
        REQUIRE(std::filesystem::remove(temporary.path() / "container.stl"));
        irop::test::require_error_category([&]() {
            static_cast<void>(irop::load_run_scene(path));
        }, irop::ErrorCategory::input_io);
        return;
    }
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::load_run_scene(path, limits));
    }, irop::ErrorCategory::resource_limit);
}

TEST_CASE("saved scene artifacts cannot resolve through a symlink outside their run directory", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto directory = temporary.path() / "run";
    REQUIRE(std::filesystem::create_directory(directory));
    const auto outside = temporary.path() / "outside.stl";
    irop::write_stl(outside, irop::test::cube_mesh());
    std::error_code error;
    std::filesystem::create_symlink(outside, directory / "packed-objects.stl", error);
    if (error) {
        SKIP("This Windows account cannot create symbolic links");
    }
    const auto path = directory / "run-summary.json";
    write_summary(path, display_summary());
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::load_run_scene(path));
    }, irop::ErrorCategory::input_io);
}

TEST_CASE("saved scene loading observes cancellation before files and during parsing", "[run-scene]")
{
    irop::test::TempDirectory temporary;
    const auto path = temporary.path() / "run-summary.json";
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::load_run_scene(path, {}, [] {
            return true;
        }));
    }, irop::ErrorCategory::cancelled);
    write_summary(path, display_summary());
    std::uint64_t calls = 0;
    irop::test::require_error_category([&]() {
        static_cast<void>(irop::load_run_scene(path, {}, [&] {
            return ++calls == 4;
        }));
    }, irop::ErrorCategory::cancelled);
    CHECK(calls == 4);
}

}  // namespace
