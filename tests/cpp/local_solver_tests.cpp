#include <array>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "irop/optimization/local_solver.hpp"

namespace {

using Catch::Approx;

struct PlaneSample {
    irop::Point3 current_vertex;
    irop::Point3 plane_point;
    irop::Point3 inward_unit_normal;
};

struct LocalSolveFixture {
    irop::TetrahedralMesh mesh;
    irop::CatConstructionResult cat;
};

class TemporaryWorkingDirectory final {
public:
    TemporaryWorkingDirectory() :
        original_(std::filesystem::current_path()),
        temporary_(
            std::filesystem::temp_directory_path() /
            ("irop-m4-ipopt-options-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        if (!std::filesystem::create_directory(temporary_)) {
            throw std::runtime_error("could not create the isolated Ipopt option-file test directory");
        }
        try {
            std::filesystem::current_path(temporary_);
        }
        catch (...) {
            try {
                std::error_code ignored;
                std::filesystem::remove_all(temporary_, ignored);
            }
            catch (...) {
                // Preserve the original construction failure if cleanup allocates internally.
            }
            throw;
        }
    }

    ~TemporaryWorkingDirectory() noexcept
    {
        try {
            std::error_code restore_error;
            std::filesystem::current_path(original_, restore_error);
            if (!restore_error) {
                std::error_code ignored;
                std::filesystem::remove_all(temporary_, ignored);
            }
        }
        catch (...) {
            // Test cleanup must not terminate the process if the filesystem implementation allocates internally.
        }
    }

    TemporaryWorkingDirectory(const TemporaryWorkingDirectory&) = delete;
    TemporaryWorkingDirectory& operator=(const TemporaryWorkingDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return temporary_; }

private:
    std::filesystem::path original_;
    std::filesystem::path temporary_;
};

[[nodiscard]] LocalSolveFixture make_fixture(const std::span<const PlaneSample> samples)
{
    LocalSolveFixture fixture;
    fixture.mesh.participant_count = 2;
    fixture.mesh.points.reserve(samples.size());
    fixture.mesh.point_owners.reserve(samples.size());
    fixture.cat.status = irop::CatConstructionStatus::success;
    fixture.cat.constraints.reserve(samples.size());

    for (const PlaneSample& sample : samples) {
        const irop::MeshIndex source_point = static_cast<irop::MeshIndex>(fixture.mesh.points.size());
        fixture.mesh.points.push_back(sample.current_vertex);
        fixture.mesh.point_owners.push_back(0);
        fixture.cat.constraints.push_back({
            .owner = 0,
            .source_point = source_point,
            .plane_point = sample.plane_point,
            .inward_unit_normal = sample.inward_unit_normal,
        });
    }
    fixture.cat.participant_ranges.push_back({
        .constraint_begin = 0,
        .constraint_count = static_cast<std::uint64_t>(samples.size()),
    });
    fixture.cat.participant_ranges.push_back({
        .constraint_begin = static_cast<std::uint64_t>(samples.size()),
        .constraint_count = 0,
    });
    return fixture;
}

[[nodiscard]] LocalSolveFixture make_box_fixture()
{
    constexpr std::array<PlaneSample, 6> samples {
        PlaneSample { { 0.9, 0.0, 0.0 },  { 1.0, 0.0, 0.0 },  { -1.0, 0.0, 0.0 } },
        PlaneSample { { -0.9, 0.0, 0.0 }, { -1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }  },
        PlaneSample { { 0.0, 0.9, 0.0 },  { 0.0, 1.0, 0.0 },  { 0.0, -1.0, 0.0 } },
        PlaneSample { { 0.0, -0.9, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 1.0, 0.0 }  },
        PlaneSample { { 0.0, 0.0, 0.9 },  { 0.0, 0.0, 1.0 },  { 0.0, 0.0, -1.0 } },
        PlaneSample { { 0.0, 0.0, -0.9 }, { 0.0, 0.0, -1.0 }, { 0.0, 0.0, 1.0 }  },
    };
    return make_fixture(samples);
}

[[nodiscard]] LocalSolveFixture make_asymmetric_fixture()
{
    constexpr std::array<PlaneSample, 6> samples {
        PlaneSample { { 0.8, 0.0, 0.0 },  { 1.0, 0.0, 0.0 },   { -1.0, 0.0, 0.0 } },
        PlaneSample { { 0.0, 0.6, 0.0 },  { 0.0, 0.9, 0.0 },   { 0.0, -1.0, 0.0 } },
        PlaneSample { { 0.0, 0.0, 0.4 },  { 0.0, 0.0, 0.7 },   { 0.0, 0.0, -1.0 } },
        PlaneSample { { -0.5, 0.0, 0.0 }, { -0.95, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }  },
        PlaneSample { { 0.0, -0.4, 0.0 }, { 0.0, -0.8, 0.0 },  { 0.0, 1.0, 0.0 }  },
        PlaneSample { { 0.0, 0.0, -0.3 }, { 0.0, 0.0, -0.65 }, { 0.0, 0.0, 1.0 }  },
    };
    return make_fixture(samples);
}

[[nodiscard]] irop::LocalSolveRequest make_scale_only_request()
{
    irop::LocalSolveRequest request;
    request.initial_guess.volume_scale_multiplier = 1.0;
    request.bounds.minimum_volume_scale_multiplier = 0.1;
    request.bounds.maximum_volume_scale_multiplier = 8.0;
    request.bounds.maximum_absolute_rotation_delta_radians = 0.0;
    request.bounds.maximum_absolute_translation = 0.0;
    request.maximum_result_volume_scale = 10.0;
    request.tolerance = 1.0e-8;
    return request;
}

void perturb(irop::LocalTransformStep& step, const std::size_t variable, const double amount)
{
    switch (variable) {
    case 0:
        step.volume_scale_multiplier += amount;
        break;
    case 1:
        step.rotation_delta.x += amount;
        break;
    case 2:
        step.rotation_delta.y += amount;
        break;
    case 3:
        step.rotation_delta.z += amount;
        break;
    case 4:
        step.translation_delta.x += amount;
        break;
    case 5:
        step.translation_delta.y += amount;
        break;
    case 6:
        step.translation_delta.z += amount;
        break;
    default:
        break;
    }
}

[[nodiscard]] irop::LocalPlaneConstraint golden_constraint()
{
    return {
        .current_vertex = { 2.7,  -1.2,  3.5 },
        .plane_point = { 2.1,  -0.7,  2.6 },
        .inward_unit_normal = { 0.36, -0.48, 0.8 },
    };
}

[[nodiscard]] irop::LocalTransformStep golden_step()
{
    return {
        .volume_scale_multiplier = 1.728,
        .rotation_delta = { 0.1, -0.2, 0.3 },
        .translation_delta = { 0.2, -0.4, 0.1 },
    };
}

TEST_CASE("local objective and constraint match the Python numerical golden")
{
    constexpr irop::Point3 object_center { 2.0, -1.0, 3.0 };
    const irop::LocalPlaneConstraint constraint = golden_constraint();
    const irop::LocalTransformStep step = golden_step();

    CHECK(irop::evaluate_local_objective(step) == Approx(-1.728).margin(1.0e-15));
    const auto objective_gradient = irop::evaluate_local_objective_gradient();
    CHECK(objective_gradient[0] == -1.0);
    for (std::size_t variable = 1; variable < objective_gradient.size(); ++variable) {
        CHECK(objective_gradient[variable] == 0.0);
    }

    CHECK(irop::evaluate_local_constraint(object_center, constraint, 0.05, step) ==
          Approx(1.603982531009444).margin(2.0e-12));
    const auto gradient = irop::evaluate_local_constraint_gradient(object_center, constraint, 0.05, step);
    constexpr std::array<double, irop::local_solve_variable_count> expected {
        0.170135519099044, 0.136605304896829, -0.341431708527381, -0.408564261347921, 0.36, -0.48, 0.8,
    };
    for (std::size_t variable = 0; variable < expected.size(); ++variable) {
        CAPTURE(variable);
        CHECK(gradient[variable] == Approx(expected[variable]).margin(2.0e-12));
    }
}

TEST_CASE("analytic local constraint gradient matches central differences")
{
    constexpr irop::Point3 object_center { 2.0, -1.0, 3.0 };
    const irop::LocalPlaneConstraint constraint = golden_constraint();
    const irop::LocalTransformStep step = golden_step();
    const auto analytic = irop::evaluate_local_constraint_gradient(object_center, constraint, 0.05, step);
    constexpr double step_size = 1.0e-6;

    for (std::size_t variable = 0; variable < analytic.size(); ++variable) {
        irop::LocalTransformStep lower = step;
        irop::LocalTransformStep upper = step;
        perturb(lower, variable, -step_size);
        perturb(upper, variable, step_size);
        const double finite_difference = (irop::evaluate_local_constraint(object_center, constraint, 0.05, upper) -
                                          irop::evaluate_local_constraint(object_center, constraint, 0.05, lower)) /
                                         (2.0 * step_size);

        CAPTURE(variable, analytic[variable], finite_difference);
        CHECK(analytic[variable] == Approx(finite_difference).margin(2.0e-7));
    }
}

TEST_CASE("local result application composes the optimized incremental rotation")
{
    const irop::Transform current {
        .volume_scale = 0.4,
        .rotation = { 0.2, -0.1, 0.3 },
        .translation = { 1.0, 2.0,  3.0 },
    };
    const irop::LocalTransformStep step {
        .volume_scale_multiplier = 2.0,
        .rotation_delta = { 0.05, 0.04, -0.02 },
        .translation_delta = { -0.5, 0.25, 1.0   },
    };

    const irop::Transform applied = irop::apply_local_step(current, step, 0.7);
    CHECK(applied.volume_scale == Approx(0.7));
    CHECK(applied.translation.x == Approx(0.5));
    CHECK(applied.translation.y == Approx(2.25));
    CHECK(applied.translation.z == Approx(4.0));

    const irop::Matrix4 expected_rotation = irop::compose(irop::matrix_for({ .rotation = step.rotation_delta }),
                                                          irop::matrix_for({ .rotation = current.rotation }));
    const irop::Matrix4 applied_rotation = irop::matrix_for({ .rotation = applied.rotation });
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            CAPTURE(row, column);
            CHECK(applied_rotation(row, column) == Approx(expected_rotation(row, column)).margin(2.0e-14));
        }
    }
}

TEST_CASE("local result rotation composition is stable at gimbal lock")
{
    constexpr double half_pi = std::numbers::pi_v<double> / 2.0;
    constexpr std::array<irop::EulerRotationRadians, 3> deltas {
        irop::EulerRotationRadians { .z = half_pi },
        irop::EulerRotationRadians { .z = -half_pi },
        irop::EulerRotationRadians { .z = half_pi - 1.0e-13 },
    };
    const irop::Transform current { .rotation = { .x = 0.4 } };

    for (const irop::EulerRotationRadians& delta : deltas) {
        const irop::LocalTransformStep step { .rotation_delta = delta };
        const irop::Transform applied = irop::apply_local_step(current, step, 1.0);
        const irop::Matrix4 expected_rotation =
            irop::compose(irop::matrix_for({ .rotation = delta }), irop::matrix_for({ .rotation = current.rotation }));
        const irop::Matrix4 applied_rotation = irop::matrix_for({ .rotation = applied.rotation });
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                CAPTURE(delta.z, row, column);
                CHECK(applied_rotation(row, column) == Approx(expected_rotation(row, column)).margin(2.0e-12));
            }
        }
    }
}

TEST_CASE("near-target local result snaps to an independently feasible exact barrier")
{
    const LocalSolveFixture fixture = make_box_fixture();
    irop::LocalSolveRequest request = make_scale_only_request();
    constexpr double geometric_scale_limit = 1.0 / (0.9 * 0.9 * 0.9);
    const double exact_feasible_barrier = std::nextafter(geometric_scale_limit, 0.0);
    request.bounds.maximum_volume_scale_multiplier =
        exact_feasible_barrier * (1.0 + irop::local_solve_barrier_relative_slack);
    request.maximum_result_volume_scale = exact_feasible_barrier;

    irop::LocalSolveWorkspace workspace;
    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.candidate_step.has_value());
    REQUIRE(result.accepted_transform.has_value());
    CHECK(result.candidate_step->volume_scale_multiplier < exact_feasible_barrier);
    CHECK(result.accepted_transform->volume_scale == exact_feasible_barrier);
    REQUIRE(result.work.minimum_applied_constraint.has_value());
    CHECK(*result.work.minimum_applied_constraint >= 0.0);
}

TEST_CASE("Ipopt solves the Python box scale fixture")
{
    const LocalSolveFixture fixture = make_box_fixture();
    const irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.candidate_step.has_value());
    REQUIRE(result.accepted_transform.has_value());
    CHECK((result.status == irop::LocalSolveStatus::success || result.status == irop::LocalSolveStatus::acceptable));
    CHECK(result.candidate_step->volume_scale_multiplier == Approx(1.3717421124828535).margin(5.0e-5));
    CHECK(result.accepted_transform->volume_scale == Approx(1.3717421124828535).margin(5.0e-5));
    REQUIRE(result.work.minimum_solver_constraint.has_value());
    REQUIRE(result.work.minimum_applied_constraint.has_value());
    CHECK(*result.work.minimum_solver_constraint >= -1.0e-7);
    CHECK(*result.work.minimum_applied_constraint >= -1.0e-7);
    CHECK(result.work.constraints_prepared == 6);
    CHECK(result.work.objective_evaluations > 0);
    CHECK(result.work.jacobian_entries_evaluated > 0);
}

TEST_CASE("Ipopt solves an asymmetric local scale fixture")
{
    const LocalSolveFixture fixture = make_asymmetric_fixture();
    const irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.candidate_step.has_value());
    REQUIRE(result.accepted_transform.has_value());
    CHECK(result.candidate_step->volume_scale_multiplier == Approx(1.953125).margin(7.0e-5));
    CHECK(result.accepted_transform->volume_scale == Approx(1.953125).margin(7.0e-5));
    REQUIRE(result.work.minimum_applied_constraint.has_value());
    CHECK(*result.work.minimum_applied_constraint >= -1.0e-7);
}

TEST_CASE("Ipopt solves a genuine seven-variable local problem")
{
    const LocalSolveFixture fixture = make_box_fixture();
    irop::LocalSolveRequest request = make_scale_only_request();
    request.initial_guess.rotation_delta = { 0.03, -0.02, 0.01 };
    request.initial_guess.translation_delta = { 0.01, -0.01, 0.005 };
    request.bounds.maximum_absolute_rotation_delta_radians = 0.25;
    request.bounds.maximum_absolute_translation = 0.2;
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    REQUIRE(result.candidate_step.has_value());
    REQUIRE(result.accepted_transform.has_value());
    CHECK(result.candidate_step->volume_scale_multiplier > 1.0);
    CHECK(std::abs(result.candidate_step->rotation_delta.x) <= 0.25);
    CHECK(std::abs(result.candidate_step->rotation_delta.y) <= 0.25);
    CHECK(std::abs(result.candidate_step->rotation_delta.z) <= 0.25);
    CHECK(std::abs(result.candidate_step->translation_delta.x) <= 0.2);
    CHECK(std::abs(result.candidate_step->translation_delta.y) <= 0.2);
    CHECK(std::abs(result.candidate_step->translation_delta.z) <= 0.2);
    REQUIRE(result.work.minimum_applied_constraint.has_value());
    CHECK(*result.work.minimum_applied_constraint >= -1.0e-7);
    CHECK(result.work.jacobian_entries_evaluated > 0);
}

TEST_CASE("local solver ignores an ambient Ipopt option file", "[optimization][ambient-option-file]")
{
    const TemporaryWorkingDirectory isolated_working_directory;
    REQUIRE(std::filesystem::current_path() == isolated_working_directory.path());
    constexpr const char* output_filename = "ambient-ipopt-output.txt";
    {
        std::ofstream options("ipopt.opt", std::ios::binary | std::ios::trunc);
        REQUIRE(options);
        options << "max_iter 0\n"
                   "output_file "
                << output_filename
                << "\n"
                   "file_print_level 12\n";
        REQUIRE(options.good());
    }

    const LocalSolveFixture fixture = make_box_fixture();
    const irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;
    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    REQUIRE(result.succeeded());
    CHECK_FALSE(std::filesystem::exists(output_filename));
}

TEST_CASE("local solver reports invalid input without publishing a transform")
{
    LocalSolveFixture fixture = make_box_fixture();
    irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;

    SECTION("unsuccessful CAT input")
    {
        fixture.cat.status = irop::CatConstructionStatus::invalid_input;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("container participant")
    {
        request.participant = 1;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("empty participant constraint range")
    {
        fixture.cat.constraints.clear();
        fixture.cat.participant_ranges.front().constraint_count = 0;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("non-finite constraint geometry")
    {
        fixture.cat.constraints.front().inward_unit_normal.x = std::numeric_limits<double>::quiet_NaN();
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("initial guess outside the requested bounds")
    {
        request.initial_guess.volume_scale_multiplier = 9.0;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("tolerance above the validated numerical ceiling")
    {
        request.tolerance = 2.0 * irop::maximum_local_solve_tolerance;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("rotation bound above the validated numerical ceiling")
    {
        request.bounds.maximum_absolute_rotation_delta_radians = 2.0 * irop::maximum_local_solve_rotation_delta_radians;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);
        CHECK(result.status == irop::LocalSolveStatus::invalid_input);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }
}

TEST_CASE("local solver enforces preparation and callback resource limits")
{
    const LocalSolveFixture fixture = make_box_fixture();
    irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;
    irop::LocalSolveLimits limits;

    SECTION("constraint count")
    {
        limits.max_constraints = 5;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace, limits);
        CHECK(result.status == irop::LocalSolveStatus::resource_exhausted);
        CHECK(result.work.constraints_prepared == 0);
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("dense Jacobian size")
    {
        limits.max_dense_jacobian_entries = 41;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace, limits);
        CHECK(result.status == irop::LocalSolveStatus::resource_exhausted);
        CHECK(result.work.constraints_prepared == 0);
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("fixed-point constraint work")
    {
        request.bounds.minimum_volume_scale_multiplier = 1.0;
        request.bounds.maximum_volume_scale_multiplier = 1.0;
        limits.max_constraint_rows_evaluated = 5;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace, limits);
        CHECK(result.status == irop::LocalSolveStatus::resource_exhausted);
        CHECK(result.work.constraints_prepared == 6);
        CHECK(result.work.constraint_rows_evaluated == 0);
        CHECK_FALSE(result.accepted_transform.has_value());
    }

    SECTION("Ipopt constraint callback work")
    {
        limits.max_constraint_rows_evaluated = 0;
        const irop::LocalSolveResult result =
            irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace, limits);
        INFO(result.diagnostic);
        CHECK(result.status == irop::LocalSolveStatus::resource_exhausted);
        CHECK(result.diagnostic == "local solve callback work limit exceeded");
        CHECK(result.work.constraints_prepared == 6);
        CHECK(result.work.constraint_rows_evaluated == 0);
        CHECK_FALSE(result.candidate_step.has_value());
        CHECK_FALSE(result.accepted_transform.has_value());
    }
}

TEST_CASE("fixed infeasible local problem returns a structured failure")
{
    const LocalSolveFixture fixture = make_box_fixture();
    irop::LocalSolveRequest request = make_scale_only_request();
    request.bounds.minimum_volume_scale_multiplier = 1.0;
    request.bounds.maximum_volume_scale_multiplier = 1.0;
    request.padding = 0.2;
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    CHECK(result.status == irop::LocalSolveStatus::infeasible);
    CHECK_FALSE(result.succeeded());
    CHECK_FALSE(result.candidate_step.has_value());
    CHECK_FALSE(result.accepted_transform.has_value());
    REQUIRE(result.work.minimum_solver_constraint.has_value());
    CHECK(*result.work.minimum_solver_constraint == Approx(-0.1).margin(1.0e-12));
}

TEST_CASE("solve rejects a feasible candidate made infeasible by reference scale clamping")
{
    constexpr std::array<PlaneSample, 1> samples {
        PlaneSample { { 1.0, 0.0, 0.0 }, { 1.5, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } },
    };
    const LocalSolveFixture fixture = make_fixture(samples);
    irop::LocalSolveRequest request = make_scale_only_request();
    request.initial_guess.volume_scale_multiplier = 8.0;
    request.bounds.minimum_volume_scale_multiplier = 8.0;
    request.bounds.maximum_volume_scale_multiplier = 8.0;
    request.maximum_result_volume_scale = 1.0;
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult result = irop::solve_local_transform(fixture.mesh, fixture.cat, request, workspace);

    INFO(result.diagnostic);
    CHECK(result.status == irop::LocalSolveStatus::postcheck_failed);
    CHECK_FALSE(result.succeeded());
    REQUIRE(result.candidate_step.has_value());
    CHECK_FALSE(result.accepted_transform.has_value());
    REQUIRE(result.work.minimum_solver_constraint.has_value());
    REQUIRE(result.work.minimum_applied_constraint.has_value());
    CHECK(*result.work.minimum_solver_constraint == Approx(0.5).margin(1.0e-12));
    CHECK(*result.work.minimum_applied_constraint == Approx(-0.5).margin(1.0e-12));
}

TEST_CASE("local solver workspace can be cleared and reused across solves")
{
    const LocalSolveFixture box = make_box_fixture();
    const LocalSolveFixture asymmetric = make_asymmetric_fixture();
    const irop::LocalSolveRequest request = make_scale_only_request();
    irop::LocalSolveWorkspace workspace;

    const irop::LocalSolveResult first = irop::solve_local_transform(box.mesh, box.cat, request, workspace);
    INFO(first.diagnostic);
    REQUIRE(first.succeeded());
    const std::size_t retained_capacity = workspace.constraint_capacity();
    REQUIRE(retained_capacity >= 6);

    workspace.clear();
    CHECK(workspace.constraint_capacity() == retained_capacity);
    const irop::LocalSolveResult second =
        irop::solve_local_transform(asymmetric.mesh, asymmetric.cat, request, workspace);
    INFO(second.diagnostic);
    REQUIRE(second.succeeded());
    REQUIRE(second.candidate_step.has_value());
    CHECK(second.candidate_step->volume_scale_multiplier == Approx(1.953125).margin(7.0e-5));
    CHECK(workspace.constraint_capacity() >= retained_capacity);
}

[[nodiscard]] std::vector<irop::LocalPlaneConstraint> make_derivative_benchmark_batch()
{
    constexpr std::size_t constraint_count = 2'048;
    std::vector<irop::LocalPlaneConstraint> constraints;
    constraints.reserve(constraint_count);
    for (std::size_t index = 0; index < constraint_count; ++index) {
        const double offset = static_cast<double>(index % 101) * 1.0e-3;
        constraints.push_back({
            .current_vertex = { 2.7 + offset, -1.2 - 0.5 * offset, 3.5 + 0.25 * offset },
            .plane_point = { 2.1,          -0.7,                2.6                 },
            .inward_unit_normal = { 0.36,         -0.48,               0.8                 },
        });
    }
    return constraints;
}

[[nodiscard]] double analytic_gradient_checksum(const std::span<const irop::LocalPlaneConstraint> constraints,
                                                const irop::Point3& object_center, const double padding,
                                                const irop::LocalTransformStep& step)
{
    double checksum = 0.0;
    for (const irop::LocalPlaneConstraint& constraint : constraints) {
        const auto gradient = irop::evaluate_local_constraint_gradient(object_center, constraint, padding, step);
        for (const double value : gradient) {
            checksum += value;
        }
    }
    return checksum;
}

[[nodiscard]] double forward_difference_checksum(const std::span<const irop::LocalPlaneConstraint> constraints,
                                                 const irop::Point3& object_center, const double padding,
                                                 const irop::LocalTransformStep& step)
{
    constexpr double step_size = 1.0e-7;
    double checksum = 0.0;
    for (const irop::LocalPlaneConstraint& constraint : constraints) {
        const double baseline = irop::evaluate_local_constraint(object_center, constraint, padding, step);
        for (std::size_t variable = 0; variable < irop::local_solve_variable_count; ++variable) {
            irop::LocalTransformStep perturbed = step;
            perturb(perturbed, variable, step_size);
            checksum +=
                (irop::evaluate_local_constraint(object_center, constraint, padding, perturbed) - baseline) / step_size;
        }
    }
    return checksum;
}

TEST_CASE("analytic local derivatives outperform Python-style forward differences", "[.m4-derivative-benchmark]")
{
    constexpr irop::Point3 object_center { 2.0, -1.0, 3.0 };
    const irop::LocalTransformStep step = golden_step();
    const std::vector<irop::LocalPlaneConstraint> constraints = make_derivative_benchmark_batch();

    CHECK(analytic_gradient_checksum(constraints, object_center, 0.05, step) ==
          Approx(forward_difference_checksum(constraints, object_center, 0.05, step)).margin(5.0e-4));

    BENCHMARK("analytic gradient for 2048 constraints")
    {
        return analytic_gradient_checksum(constraints, object_center, 0.05, step);
    };
    BENCHMARK("Python-style forward difference for 2048 constraints")
    {
        return forward_difference_checksum(constraints, object_center, 0.05, step);
    };
}

}  // namespace
