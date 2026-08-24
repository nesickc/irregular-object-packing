#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "irop/cat/cat.hpp"
#include "irop/geometry/transform.hpp"

namespace irop {

inline constexpr std::size_t local_solve_variable_count = 7;
inline constexpr double maximum_local_solve_tolerance = 1.0e-6;
inline constexpr double maximum_local_solve_bound_magnitude_exclusive = 1.0e19;
inline constexpr double local_solve_barrier_relative_slack = 20.0 * maximum_local_solve_tolerance;
inline constexpr double maximum_local_solve_rotation_delta_radians = 3.14159265358979323846;

struct LocalTransformStep {
    double volume_scale_multiplier = 1.0;
    EulerRotationRadians rotation_delta;
    Point3 translation_delta;
};

struct LocalPlaneConstraint {
    Point3 current_vertex;
    Point3 plane_point;
    Point3 inward_unit_normal;
};

struct LocalSolveBounds {
    double minimum_volume_scale_multiplier = 0.1;
    std::optional<double> maximum_volume_scale_multiplier;
    // This is the effective symmetric solver bound. Milestone 5 wiring is
    // responsible for applying the reference's 0.9 configuration multiplier.
    double maximum_absolute_rotation_delta_radians = 0.0;
    std::optional<double> maximum_absolute_translation;
};

struct LocalSolveLimits {
    static constexpr std::uint64_t default_max_constraints = 1'000'000ULL;
    static constexpr std::uint64_t default_max_dense_jacobian_entries = 7'000'000ULL;
    static constexpr std::uint64_t default_max_constraint_rows_evaluated = 500'000'000ULL;
    static constexpr std::uint64_t default_max_jacobian_entries_evaluated = 3'500'000'000ULL;
    static constexpr std::uint64_t default_max_iterations = 200ULL;
    static constexpr std::chrono::milliseconds default_max_elapsed_time { 30'000 };

    std::uint64_t max_constraints = default_max_constraints;
    std::uint64_t max_dense_jacobian_entries = default_max_dense_jacobian_entries;
    std::uint64_t max_constraint_rows_evaluated = default_max_constraint_rows_evaluated;
    std::uint64_t max_jacobian_entries_evaluated = default_max_jacobian_entries_evaluated;
    std::uint64_t max_iterations = default_max_iterations;
    std::chrono::milliseconds max_elapsed_time = default_max_elapsed_time;
};

struct LocalSolveRequest {
    ParticipantId participant = 0;
    Transform current_transform;
    LocalTransformStep initial_guess;
    LocalSolveBounds bounds;
    double padding = 0.0;
    double maximum_result_volume_scale = 1.0;
    double tolerance = maximum_local_solve_tolerance;
};

enum class LocalSolveStatus {
    success,
    acceptable,
    invalid_input,
    resource_exhausted,
    infeasible,
    iteration_limit,
    time_limit,
    numerical_failure,
    postcheck_failed,
    dependency_failure,
    internal_failure,
};

[[nodiscard]] const char* to_string(LocalSolveStatus status) noexcept;

struct LocalSolveWork {
    std::uint64_t constraints_prepared = 0;
    std::uint64_t objective_evaluations = 0;
    std::uint64_t objective_gradient_evaluations = 0;
    std::uint64_t constraint_rows_evaluated = 0;
    std::uint64_t jacobian_entries_evaluated = 0;
    std::uint64_t iterations = 0;
    std::chrono::milliseconds elapsed_time {};
    std::optional<double> minimum_solver_constraint;
    std::optional<double> minimum_applied_constraint;
};

struct LocalSolveResult {
    LocalSolveStatus status = LocalSolveStatus::invalid_input;
    std::optional<LocalTransformStep> candidate_step;
    std::optional<Transform> accepted_transform;
    LocalSolveWork work;
    std::string diagnostic;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return (status == LocalSolveStatus::success || status == LocalSolveStatus::acceptable) &&
               accepted_transform.has_value();
    }
};

class LocalSolveWorkspace final {
public:
    LocalSolveWorkspace() = default;
    LocalSolveWorkspace(const LocalSolveWorkspace&) = delete;
    LocalSolveWorkspace& operator=(const LocalSolveWorkspace&) = delete;
    LocalSolveWorkspace(LocalSolveWorkspace&&) = delete;
    LocalSolveWorkspace& operator=(LocalSolveWorkspace&&) = delete;

    void clear() noexcept;
    [[nodiscard]] std::size_t constraint_capacity() const noexcept;

private:
    friend LocalSolveResult solve_local_transform(const TetrahedralMesh&, const CatConstructionResult&,
                                                  const LocalSolveRequest&, LocalSolveWorkspace&,
                                                  const LocalSolveLimits&) noexcept;

    std::vector<LocalPlaneConstraint> constraints_;
    std::vector<double> first_constraint_buffer_;
    std::vector<double> second_constraint_buffer_;
    std::array<double, local_solve_variable_count> variables_ {};
    std::array<double, local_solve_variable_count> variable_lower_bounds_ {};
    std::array<double, local_solve_variable_count> variable_upper_bounds_ {};
};

[[nodiscard]] double evaluate_local_objective(const LocalTransformStep& step) noexcept;
[[nodiscard]] std::array<double, local_solve_variable_count> evaluate_local_objective_gradient() noexcept;

[[nodiscard]] double evaluate_local_constraint(const Point3& object_center, const LocalPlaneConstraint& constraint,
                                               double padding, const LocalTransformStep& step);
[[nodiscard]] std::array<double, local_solve_variable_count> evaluate_local_constraint_gradient(
    const Point3& object_center, const LocalPlaneConstraint& constraint, double padding,
    const LocalTransformStep& step);

// Applies the local step represented by the nonlinear constraints. Incremental
// rotation is left-composed with the current orientation and encoded back into
// the project's Ry * Rz * Rx Euler representation.
[[nodiscard]] Transform apply_local_step(const Transform& current, const LocalTransformStep& step,
                                         double maximum_result_volume_scale);

// The caller supplies the initial guess and reusable workspace explicitly. Use
// a distinct workspace for every concurrent solve. The solver consumes no
// random state and publishes no accepted transform on failure. The final mesh
// participant is the container and is not a valid request participant.
[[nodiscard]] LocalSolveResult solve_local_transform(const TetrahedralMesh& mesh, const CatConstructionResult& cat,
                                                     const LocalSolveRequest& request, LocalSolveWorkspace& workspace,
                                                     const LocalSolveLimits& limits = {}) noexcept;

}  // namespace irop
