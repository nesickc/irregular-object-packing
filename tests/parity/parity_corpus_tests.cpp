#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "../cpp/support/test_support.hpp"
#include "irop/cat/cat.hpp"
#include "irop/error.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/optimization/local_solver.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/packing/pack_scene.hpp"

#ifndef IROP_TEST_FIXTURE_DIR
#error "IROP_TEST_FIXTURE_DIR must name the checked-in fixture directory"
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

[[nodiscard]] const nlohmann::json& corpus()
{
    static const nlohmann::json value =
        read_json(std::filesystem::path(IROP_TEST_FIXTURE_DIR) / "parity_reference_v1.json");
    return value;
}

[[nodiscard]] irop::Point3 point_from_json(const nlohmann::json& value)
{
    return { value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>() };
}

[[nodiscard]] irop::EulerRotationRadians rotation_from_json(const nlohmann::json& value)
{
    return { value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>() };
}

[[nodiscard]] irop::TriangleMesh mesh_from_json(const nlohmann::json& value)
{
    irop::TriangleMesh mesh;
    for (const nlohmann::json& vertex : value.at("vertices")) {
        mesh.vertices.push_back(point_from_json(vertex));
    }
    for (const nlohmann::json& triangle : value.at("triangles")) {
        mesh.triangles.push_back({ triangle.at(0).get<irop::MeshIndex>(), triangle.at(1).get<irop::MeshIndex>(),
                                   triangle.at(2).get<irop::MeshIndex>() });
    }
    return mesh;
}

[[nodiscard]] irop::TriangleMesh primitive(const nlohmann::json& root, const std::string& name)
{
    if (root.at("primitives").contains(name)) {
        return mesh_from_json(root.at("primitives").at(name));
    }

    const nlohmann::json& definition = root.at("generated_primitives").at(name);
    const std::string kind = definition.at("kind").get<std::string>();
    if (kind == "faceted_cylinder") {
        return irop::test::cylinder_mesh(definition.at("radius").get<double>(),
                                         definition.at("half_height").get<double>(),
                                         definition.at("segment_count").get<std::size_t>());
    }
    if (kind == "box") {
        const nlohmann::json& half_extents = definition.at("half_extents");
        return irop::test::box_mesh(half_extents.at(0).get<double>(), half_extents.at(1).get<double>(),
                                    half_extents.at(2).get<double>());
    }
    throw std::runtime_error("unknown generated parity primitive");
}

[[nodiscard]] irop::Transform transform_from_json(const nlohmann::json& value)
{
    return {
        .volume_scale = value.at("volume_scale").get<double>(),
        .rotation = rotation_from_json(value.at("rotation")),
        .translation = point_from_json(value.at("translation")),
    };
}

void check_point(const irop::Point3& actual, const nlohmann::json& expected, const double tolerance = 2.0e-12)
{
    CHECK(actual.x == Approx(expected.at(0).get<double>()).margin(tolerance));
    CHECK(actual.y == Approx(expected.at(1).get<double>()).margin(tolerance));
    CHECK(actual.z == Approx(expected.at(2).get<double>()).margin(tolerance));
}

void check_transform(const irop::Transform& actual, const nlohmann::json& expected)
{
    CHECK(actual.volume_scale == Approx(expected.at("volume_scale").get<double>()).margin(2.0e-14));
    check_point({ actual.rotation.x, actual.rotation.y, actual.rotation.z }, expected.at("rotation"), 2.0e-14);
    check_point(actual.translation, expected.at("translation"), 2.0e-14);
}

[[nodiscard]] std::span<const irop::CatPolygon> polygons_for(const irop::CatConstructionResult& result,
                                                             const irop::ParticipantId participant)
{
    const irop::CatParticipantRange& range = result.participant_ranges.at(participant);
    return { result.polygons.data() + static_cast<std::size_t>(range.polygon_begin),
             static_cast<std::size_t>(range.polygon_count) };
}

[[nodiscard]] std::span<const irop::CatPlaneConstraint> constraints_for(const irop::CatConstructionResult& result,
                                                                        const irop::ParticipantId participant)
{
    const irop::CatParticipantRange& range = result.participant_ranges.at(participant);
    return { result.constraints.data() + static_cast<std::size_t>(range.constraint_begin),
             static_cast<std::size_t>(range.constraint_count) };
}

[[nodiscard]] nlohmann::json read_output_json(const std::filesystem::path& path) { return read_json(path); }

}  // namespace

TEST_CASE("parity corpus matches Python initialization transforms and work", "[parity]")
{
    const nlohmann::json& root = corpus();
    REQUIRE(root.at("schema_version") == 1);
    for (const nlohmann::json& test_case : root.at("initialization").at("success_cases")) {
        DYNAMIC_SECTION(test_case.at("name").get<std::string>())
        {
            irop::PackingConfig config;
            config.object_count = test_case.at("object_count").get<std::uint64_t>();
            config.initial_volume_scale = test_case.at("initial_volume_scale").get<double>();
            config.seed = test_case.at("seed").get<std::uint32_t>();
            const irop::PackingState state =
                irop::initialize_packing(primitive(root, test_case.at("object").get<std::string>()),
                                         primitive(root, test_case.at("container").get<std::string>()), config);
            const nlohmann::json& expected = test_case.at("expected");
            REQUIRE(state.transforms.size() == config.object_count);
            CHECK(state.rejected_candidate_count == expected.at("rejected_candidate_count").get<std::uint64_t>());
            CHECK(state.random_state.draw_count() == expected.at("random_draw_count").get<std::uint64_t>());
            CHECK(state.object_bounding_radius ==
                  Approx(expected.at("object_bounding_radius").get<double>()).margin(2.0e-15));
            for (const nlohmann::json& sampled : expected.at("sampled_transforms")) {
                const std::size_t index = sampled.at("index").get<std::size_t>();
                REQUIRE(index < state.transforms.size());
                check_transform(state.transforms[index], sampled);
            }
        }
    }
}

TEST_CASE("parity corpus maps Python initialization rejection to a project error", "[parity]")
{
    const nlohmann::json& root = corpus();
    const nlohmann::json& test_case = root.at("initialization").at("failure_cases").at(0);
    irop::PackingConfig config;
    config.object_count = test_case.at("object_count").get<std::uint64_t>();
    config.initial_volume_scale = test_case.at("initial_volume_scale").get<double>();
    config.seed = test_case.at("seed").get<std::uint32_t>();

    try {
        static_cast<void>(irop::initialize_packing(primitive(root, test_case.at("object").get<std::string>()),
                                                   primitive(root, test_case.at("container").get<std::string>()),
                                                   config));
    }
    catch (const irop::Error& error) {
        CHECK(std::string(irop::to_string(error.category())) ==
              test_case.at("expected_cpp_category").get<std::string>());
        return;
    }
    FAIL("the invalid initialization corpus case unexpectedly succeeded");
}
TEST_CASE("parity corpus preserves the dense Python greedy prefix and bounds its jam", "[parity]")
{
    const nlohmann::json& root = corpus();
    const nlohmann::json& test_case = root.at("dense_initialization");
    const irop::TriangleMesh object = primitive(root, test_case.at("object").get<std::string>());
    const irop::TriangleMesh container = primitive(root, test_case.at("container").get<std::string>());
    const nlohmann::json& accepted_prefix = test_case.at("accepted_prefix");
    const nlohmann::json& python_behavior = test_case.at("python_behavior");
    CHECK(python_behavior.at("compatibility_id") == "IROP-DEV-0007");
    CHECK(python_behavior.at("bounded_probe_expected_accepted_count") == accepted_prefix.at("object_count"));

    irop::PackingConfig success_config;
    success_config.object_count = accepted_prefix.at("object_count").get<std::uint64_t>();
    success_config.initial_volume_scale = test_case.at("initial_volume_scale").get<double>();
    success_config.seed = test_case.at("seed").get<std::uint32_t>();
    const irop::PackingState prefix = irop::initialize_packing(object, container, success_config);
    REQUIRE(prefix.transforms.size() == success_config.object_count);
    CHECK(prefix.object_bounding_radius ==
          Approx(test_case.at("expected_bounding_radius").get<double>()).margin(2.0e-12));
    CHECK(prefix.rejected_candidate_count == accepted_prefix.at("rejected_candidate_count").get<std::uint64_t>());
    CHECK(prefix.random_state.draw_count() == accepted_prefix.at("random_draw_count").get<std::uint64_t>());
    for (const nlohmann::json& expected : accepted_prefix.at("transforms")) {
        const std::size_t index = expected.at("index").get<std::size_t>();
        REQUIRE(index < prefix.transforms.size());
        check_transform(prefix.transforms[index], expected);
    }

    for (const nlohmann::json& failure : test_case.at("bounded_cpp_failures")) {
        DYNAMIC_SECTION("count " << failure.at("object_count").get<std::uint64_t>())
        {
            irop::PackingConfig failure_config = success_config;
            // Keep the historical failure oracle explicit when the Milestone 7 fallback is enabled by default.
            failure_config.enable_structured_fallback = false;
            CHECK(failure.at("expected_placed_count") == accepted_prefix.at("object_count"));
            failure_config.object_count = failure.at("object_count").get<std::uint64_t>();
            failure_config.max_sampling_attempts = failure.at("max_sampling_attempts").get<std::uint64_t>();
            bool failed_as_expected = false;
            try {
                static_cast<void>(irop::initialize_packing(object, container, failure_config));
            }
            catch (const irop::Error& error) {
                failed_as_expected = true;
                CHECK(std::string(irop::to_string(error.category())) ==
                      failure.at("expected_category").get<std::string>());
                CHECK(std::string(error.what()) == failure.at("expected_diagnostic").get<std::string>());
                const std::string placed =
                    "after placing " + std::to_string(failure.at("expected_placed_count").get<std::uint64_t>());
                CHECK(std::string(error.what()).find(placed) != std::string::npos);
            }
            CHECK(failed_as_expected);
        }
    }
}

TEST_CASE("parity corpus matches Python transform semantics", "[parity]")
{
    const nlohmann::json& test_case = corpus().at("transform");
    const irop::Transform transform = transform_from_json(test_case.at("transform"));
    const irop::Matrix4 matrix = irop::matrix_for(transform);
    const nlohmann::json& expected_matrix = test_case.at("expected_matrix");
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            CHECK(matrix(row, column) == Approx(expected_matrix.at(row).at(column).get<double>()).margin(2.0e-12));
        }
    }
    check_point(irop::transform_point(matrix, point_from_json(test_case.at("input_point"))),
                test_case.at("expected_point"));
}

TEST_CASE("parity corpus matches Python CAT polygons constraints and work", "[parity]")
{
    const nlohmann::json& test_case = corpus().at("cat");
    irop::TetrahedralMesh mesh;
    for (const nlohmann::json& point : test_case.at("points")) {
        mesh.points.push_back(point_from_json(point));
    }
    mesh.tetrahedra.push_back({ test_case.at("tetrahedron").at(0).get<irop::MeshIndex>(),
                                test_case.at("tetrahedron").at(1).get<irop::MeshIndex>(),
                                test_case.at("tetrahedron").at(2).get<irop::MeshIndex>(),
                                test_case.at("tetrahedron").at(3).get<irop::MeshIndex>() });
    for (const nlohmann::json& owner : test_case.at("owners")) {
        mesh.point_owners.push_back(owner.get<irop::ParticipantId>());
    }
    mesh.participant_count = test_case.at("participant_count").get<std::uint64_t>();

    const irop::CatConstructionResult result = irop::build_cat(mesh);
    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    const nlohmann::json& expected = test_case.at("expected");
    for (std::uint64_t participant_index = 0; participant_index < mesh.participant_count; ++participant_index) {
        const auto participant = static_cast<irop::ParticipantId>(participant_index);
        const std::span<const irop::CatPolygon> actual = polygons_for(result, participant);
        const nlohmann::json& expected_polygons =
            expected.at("polygons_by_participant").at(std::to_string(participant));
        REQUIRE(actual.size() == expected_polygons.size());
        for (std::size_t polygon = 0; polygon < actual.size(); ++polygon) {
            REQUIRE(actual[polygon].vertex_count == expected_polygons.at(polygon).size());
            for (std::size_t vertex = 0; vertex < actual[polygon].vertex_count; ++vertex) {
                check_point(actual[polygon].vertices[vertex], expected_polygons.at(polygon).at(vertex));
            }
        }

        const std::span<const irop::CatPlaneConstraint> actual_constraints = constraints_for(result, participant);
        const nlohmann::json& expected_constraints =
            expected.at("constraints_by_participant").at(std::to_string(participant));
        REQUIRE(actual_constraints.size() == expected_constraints.size());
        CHECK(actual_constraints.size() ==
              expected.at("constraints_per_participant").at(participant).get<std::uint64_t>());
        for (std::size_t constraint = 0; constraint < actual_constraints.size(); ++constraint) {
            const irop::CatPlaneConstraint& actual_constraint = actual_constraints[constraint];
            const nlohmann::json& expected_constraint = expected_constraints.at(constraint);
            CHECK(actual_constraint.owner == participant);
            REQUIRE(actual_constraint.source_point < mesh.points.size());
            check_point(mesh.points[static_cast<std::size_t>(actual_constraint.source_point)],
                        expected_constraint.at("source_point"));
            check_point(actual_constraint.plane_point, expected_constraint.at("plane_point"));
            check_point(actual_constraint.inward_unit_normal, expected_constraint.at("inward_unit_normal"));
        }
    }

    const nlohmann::json& work = expected.at("work");
    CHECK(result.work.points_examined == work.at("points_examined").get<std::uint64_t>());
    CHECK(result.work.tetrahedra_examined == work.at("tetrahedra_examined").get<std::uint64_t>());
    CHECK(result.work.relevant_tetrahedra == work.at("relevant_tetrahedra").get<std::uint64_t>());
    CHECK(result.work.skipped_single_participant_tetrahedra ==
          work.at("skipped_single_participant_tetrahedra").get<std::uint64_t>());
    CHECK(result.work.polygons_generated == work.at("polygons_generated").get<std::uint64_t>());
    CHECK(result.work.polygon_vertices_generated == work.at("polygon_vertices_generated").get<std::uint64_t>());
    CHECK(result.work.constraints_generated == work.at("constraints_generated").get<std::uint64_t>());
}

TEST_CASE("parity corpus matches Python local objective constraint and gradient", "[parity]")
{
    const nlohmann::json& test_case = corpus().at("local_constraint");
    const nlohmann::json& step_json = test_case.at("step");
    const irop::LocalTransformStep step {
        .volume_scale_multiplier = step_json.at("volume_scale_multiplier").get<double>(),
        .rotation_delta = rotation_from_json(step_json.at("rotation_delta")),
        .translation_delta = point_from_json(step_json.at("translation_delta")),
    };
    const irop::LocalPlaneConstraint constraint {
        .current_vertex = point_from_json(test_case.at("current_vertex")),
        .plane_point = point_from_json(test_case.at("plane_point")),
        .inward_unit_normal = point_from_json(test_case.at("inward_unit_normal")),
    };
    const irop::Point3 center = point_from_json(test_case.at("object_center"));
    const double padding = test_case.at("padding").get<double>();

    CHECK(irop::evaluate_local_objective(step) ==
          Approx(test_case.at("expected_objective").get<double>()).margin(2.0e-15));
    const auto objective_gradient = irop::evaluate_local_objective_gradient();
    for (std::size_t variable = 0; variable < objective_gradient.size(); ++variable) {
        CHECK(objective_gradient[variable] ==
              Approx(test_case.at("expected_objective_gradient").at(variable).get<double>()).margin(2.0e-15));
    }
    CHECK(irop::evaluate_local_constraint(center, constraint, padding, step) ==
          Approx(test_case.at("expected_constraint").get<double>()).margin(2.0e-12));
    const auto gradient = irop::evaluate_local_constraint_gradient(center, constraint, padding, step);
    for (std::size_t variable = 0; variable < gradient.size(); ++variable) {
        CHECK(gradient[variable] ==
              Approx(test_case.at("expected_gradient").at(variable).get<double>()).margin(2.0e-12));
    }
}

TEST_CASE("parity corpus produces a feasible no-growth outcome metrics and artifacts", "[parity]")
{
    const nlohmann::json& root = corpus();
    const nlohmann::json& test_case = root.at("no_growth_terminal");
    const nlohmann::json& expected = test_case.at("expected");
    irop::test::TempDirectory temporary;
    const std::filesystem::path object_path = temporary.path() / "object.stl";
    const std::filesystem::path container_path = temporary.path() / "container.stl";
    const std::filesystem::path output_path = temporary.path() / "output";
    irop::write_stl(object_path, primitive(root, test_case.at("object").get<std::string>()));
    irop::write_stl(container_path, primitive(root, test_case.at("container").get<std::string>()));

    irop::PackOptions options;
    options.initialization.object_count = test_case.at("object_count").get<std::uint64_t>();
    options.initialization.initial_volume_scale = test_case.at("initial_volume_scale").get<double>();
    options.initialization.seed = test_case.at("seed").get<std::uint32_t>();
    options.algorithm.final_volume_scale = test_case.at("final_volume_scale").get<double>();
    options.algorithm.scale_step_count = 1;
    options.algorithm.max_iterations_per_scale_step = 1;
    options.algorithm.adaptive_sampling = false;

    const irop::PackSceneResult result = irop::pack_scene(object_path, container_path, output_path, options);
    INFO(result.packing.diagnostic);
    REQUIRE(std::string(irop::to_string(result.packing.status)) == expected.at("cpp_status").get<std::string>());
    REQUIRE(result.packing.final_validation_performed);
    CHECK(result.packing.final_validation.physical_scene_valid() == expected.at("final_feasible").get<bool>());
    REQUIRE(result.packing.state.transforms.size() == 1);
    check_transform(result.packing.state.transforms.front(), expected.at("python_setup_transform"));
    CHECK(result.packing.work.completed_scale_steps == expected.at("completed_scale_steps").get<std::uint64_t>());
    CHECK(result.packing.work.iterations == expected.at("iterations").get<std::uint64_t>());
    CHECK(result.packing.work.tetrahedralization_attempts ==
          expected.at("tetrahedralization_attempts").get<std::uint64_t>());
    CHECK(result.packing.work.cat_builds == expected.at("cat_builds").get<std::uint64_t>());
    CHECK(result.packing.work.local_solves == expected.at("local_solves").get<std::uint64_t>());

    std::vector<std::string> artifacts;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(output_path)) {
        artifacts.push_back(entry.path().filename().string());
    }
    std::sort(artifacts.begin(), artifacts.end());
    CHECK(artifacts == expected.at("artifacts").get<std::vector<std::string>>());
    REQUIRE(result.packed_objects_path.has_value());
    CHECK(irop::read_stl(*result.packed_objects_path, {}).mesh.triangles.size() ==
          expected.at("packed_triangle_count").get<std::size_t>());

    const nlohmann::json summary = read_output_json(result.run_summary_path);
    const nlohmann::json& actual_metrics = summary.at("metrics");
    const nlohmann::json& expected_metrics = expected.at("metrics");
    CHECK(actual_metrics.at("object_count") == expected_metrics.at("object_count"));
    CHECK(actual_metrics.at("objects_at_final_target") == expected_metrics.at("objects_at_final_target"));
    for (const char* name : { "minimum_volume_scale", "maximum_volume_scale", "mean_volume_scale",
                              "packed_object_volume", "packing_fraction" }) {
        CHECK(actual_metrics.at(name).get<double>() == Approx(expected_metrics.at(name).get<double>()).margin(2.0e-12));
    }
}
