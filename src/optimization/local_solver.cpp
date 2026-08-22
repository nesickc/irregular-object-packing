#include "irop/optimization/local_solver.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include "irop/error.hpp"
#include "optimization/local_solver_internal.hpp"

namespace irop {
namespace {

constexpr double unit_normal_squared_tolerance = 1.0e-10;

struct RotationEvaluation {
    Eigen::Matrix3d rotation;
    Eigen::Matrix3d derivative_x;
    Eigen::Matrix3d derivative_y;
    Eigen::Matrix3d derivative_z;
};

struct StepEvaluation {
    RotationEvaluation rotation;
    Eigen::Vector3d translation;
    double linear_scale = 0.0;
    double linear_scale_derivative = 0.0;
};

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool is_finite(const EulerRotationRadians& rotation) noexcept
{
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z);
}

[[nodiscard]] bool is_finite(const LocalTransformStep& step) noexcept
{
    return std::isfinite(step.volume_scale_multiplier) && is_finite(step.rotation_delta) &&
           is_finite(step.translation_delta);
}

[[nodiscard]] Eigen::Vector3d vector_for(const Point3& point) noexcept { return { point.x, point.y, point.z }; }

[[nodiscard]] bool has_unit_normal(const Point3& normal) noexcept
{
    if (!is_finite(normal)) {
        return false;
    }
    const long double x = normal.x;
    const long double y = normal.y;
    const long double z = normal.z;
    const long double squared_length = x * x + y * y + z * z;
    return std::isfinite(squared_length) &&
           std::abs(squared_length - 1.0L) <= static_cast<long double>(unit_normal_squared_tolerance);
}

[[nodiscard]] bool make_rotation_evaluation(const EulerRotationRadians& angles, RotationEvaluation& result) noexcept
{
    const double cosine_x = std::cos(angles.x);
    const double cosine_y = std::cos(angles.y);
    const double cosine_z = std::cos(angles.z);
    const double sine_x = std::sin(angles.x);
    const double sine_y = std::sin(angles.y);
    const double sine_z = std::sin(angles.z);

    Eigen::Matrix3d rotation_x;
    rotation_x << 1.0, 0.0, 0.0, 0.0, cosine_x, -sine_x, 0.0, sine_x, cosine_x;
    Eigen::Matrix3d rotation_y;
    rotation_y << cosine_y, 0.0, sine_y, 0.0, 1.0, 0.0, -sine_y, 0.0, cosine_y;
    Eigen::Matrix3d rotation_z;
    rotation_z << cosine_z, -sine_z, 0.0, sine_z, cosine_z, 0.0, 0.0, 0.0, 1.0;

    Eigen::Matrix3d derivative_x;
    derivative_x << 0.0, 0.0, 0.0, 0.0, -sine_x, -cosine_x, 0.0, cosine_x, -sine_x;
    Eigen::Matrix3d derivative_y;
    derivative_y << -sine_y, 0.0, cosine_y, 0.0, 0.0, 0.0, -cosine_y, 0.0, -sine_y;
    Eigen::Matrix3d derivative_z;
    derivative_z << -sine_z, -cosine_z, 0.0, cosine_z, -sine_z, 0.0, 0.0, 0.0, 0.0;

    result.rotation = rotation_y * rotation_z * rotation_x;
    result.derivative_x = rotation_y * rotation_z * derivative_x;
    result.derivative_y = derivative_y * rotation_z * rotation_x;
    result.derivative_z = rotation_y * derivative_z * rotation_x;
    return result.rotation.allFinite() && result.derivative_x.allFinite() && result.derivative_y.allFinite() &&
           result.derivative_z.allFinite();
}

[[nodiscard]] bool make_step_evaluation(const LocalTransformStep& step, StepEvaluation& result) noexcept
{
    if (!is_finite(step) || step.volume_scale_multiplier <= 0.0 ||
        !make_rotation_evaluation(step.rotation_delta, result.rotation)) {
        return false;
    }
    result.linear_scale = std::cbrt(step.volume_scale_multiplier);
    result.linear_scale_derivative = 1.0 / (3.0 * result.linear_scale * result.linear_scale);
    result.translation = vector_for(step.translation_delta);
    return std::isfinite(result.linear_scale) && std::isfinite(result.linear_scale_derivative) &&
           result.translation.allFinite();
}

void validate_constraint_inputs(const Point3& object_center, const LocalPlaneConstraint& constraint,
                                const double padding, const LocalTransformStep& step)
{
    if (!is_finite(object_center) || !is_finite(constraint.current_vertex) || !is_finite(constraint.plane_point)) {
        throw Error(ErrorCategory::invalid_configuration, "local constraint geometry must be finite");
    }
    if (!has_unit_normal(constraint.inward_unit_normal)) {
        throw Error(ErrorCategory::invalid_configuration, "local constraint normal must be finite and unit length");
    }
    if (!std::isfinite(padding) || padding < 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "local constraint padding must be finite and nonnegative");
    }
    if (!is_finite(step) || step.volume_scale_multiplier <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration,
                    "local transform step must contain a finite positive volume multiplier");
    }
}

[[nodiscard]] bool add_is_finite(const double first, const double second, double& result) noexcept
{
    result = first + second;
    return std::isfinite(result);
}

}  // namespace

namespace detail {

bool evaluate_local_constraints_unchecked(const std::span<const LocalPlaneConstraint> constraints,
                                          const Point3& object_center, const double padding,
                                          const LocalTransformStep& step, const std::span<double> values) noexcept
{
    StepEvaluation evaluation;
    if (values.size() != constraints.size() || !make_step_evaluation(step, evaluation)) {
        return false;
    }

    const Eigen::Vector3d center = vector_for(object_center);
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        const LocalPlaneConstraint& constraint = constraints[index];
        const Eigen::Vector3d relative_vertex = vector_for(constraint.current_vertex) - center;
        const Eigen::Vector3d relative_plane = vector_for(constraint.plane_point) - center;
        const Eigen::Vector3d normal = vector_for(constraint.inward_unit_normal);
        const Eigen::Vector3d transformed =
            evaluation.linear_scale * evaluation.rotation.rotation * relative_vertex + evaluation.translation;
        values[index] = (transformed - relative_plane).dot(normal) - padding;
        if (!std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

bool evaluate_local_jacobian_unchecked(const std::span<const LocalPlaneConstraint> constraints,
                                       const Point3& object_center, const LocalTransformStep& step,
                                       const std::span<double> values) noexcept
{
    StepEvaluation evaluation;
    if (constraints.size() > std::numeric_limits<std::size_t>::max() / local_solve_variable_count ||
        values.size() != constraints.size() * local_solve_variable_count || !make_step_evaluation(step, evaluation)) {
        return false;
    }

    const Eigen::Vector3d center = vector_for(object_center);
    for (std::size_t constraint_index = 0; constraint_index < constraints.size(); ++constraint_index) {
        const LocalPlaneConstraint& constraint = constraints[constraint_index];
        const Eigen::Vector3d relative_vertex = vector_for(constraint.current_vertex) - center;
        const Eigen::Vector3d normal = vector_for(constraint.inward_unit_normal);
        const Eigen::Vector3d rotated = evaluation.rotation.rotation * relative_vertex;
        const std::size_t offset = constraint_index * local_solve_variable_count;
        values[offset] = evaluation.linear_scale_derivative * rotated.dot(normal);
        values[offset + 1] = evaluation.linear_scale * (evaluation.rotation.derivative_x * relative_vertex).dot(normal);
        values[offset + 2] = evaluation.linear_scale * (evaluation.rotation.derivative_y * relative_vertex).dot(normal);
        values[offset + 3] = evaluation.linear_scale * (evaluation.rotation.derivative_z * relative_vertex).dot(normal);
        values[offset + 4] = constraint.inward_unit_normal.x;
        values[offset + 5] = constraint.inward_unit_normal.y;
        values[offset + 6] = constraint.inward_unit_normal.z;
        for (std::size_t variable = 0; variable < local_solve_variable_count; ++variable) {
            if (!std::isfinite(values[offset + variable])) {
                return false;
            }
        }
    }
    return true;
}

bool evaluate_applied_constraints_unchecked(const std::span<const LocalPlaneConstraint> constraints,
                                            const Transform& current, const Transform& candidate, const double padding,
                                            const std::span<double> values) noexcept
{
    if (values.size() != constraints.size() || !std::isfinite(current.volume_scale) || current.volume_scale <= 0.0 ||
        !std::isfinite(candidate.volume_scale) || candidate.volume_scale <= 0.0 || !is_finite(current.rotation) ||
        !is_finite(candidate.rotation) || !is_finite(current.translation) || !is_finite(candidate.translation)) {
        return false;
    }

    RotationEvaluation current_rotation;
    RotationEvaluation candidate_rotation;
    if (!make_rotation_evaluation(current.rotation, current_rotation) ||
        !make_rotation_evaluation(candidate.rotation, candidate_rotation)) {
        return false;
    }
    const double current_linear_scale = std::cbrt(current.volume_scale);
    const double candidate_linear_scale = std::cbrt(candidate.volume_scale);
    if (!std::isfinite(current_linear_scale) || current_linear_scale <= 0.0 || !std::isfinite(candidate_linear_scale) ||
        candidate_linear_scale <= 0.0) {
        return false;
    }

    const Eigen::Vector3d current_translation = vector_for(current.translation);
    const Eigen::Vector3d candidate_translation = vector_for(candidate.translation);
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        const LocalPlaneConstraint& constraint = constraints[index];
        const Eigen::Vector3d original_vertex = current_rotation.rotation.transpose() *
                                                (vector_for(constraint.current_vertex) - current_translation) /
                                                current_linear_scale;
        const Eigen::Vector3d applied_vertex =
            candidate_linear_scale * candidate_rotation.rotation * original_vertex + candidate_translation;
        values[index] =
            (applied_vertex - vector_for(constraint.plane_point)).dot(vector_for(constraint.inward_unit_normal)) -
            padding;
        if (!std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace detail

const char* to_string(const LocalSolveStatus status) noexcept
{
    switch (status) {
    case LocalSolveStatus::success:
        return "success";
    case LocalSolveStatus::acceptable:
        return "acceptable";
    case LocalSolveStatus::invalid_input:
        return "invalid_input";
    case LocalSolveStatus::resource_exhausted:
        return "resource_exhausted";
    case LocalSolveStatus::infeasible:
        return "infeasible";
    case LocalSolveStatus::iteration_limit:
        return "iteration_limit";
    case LocalSolveStatus::time_limit:
        return "time_limit";
    case LocalSolveStatus::numerical_failure:
        return "numerical_failure";
    case LocalSolveStatus::postcheck_failed:
        return "postcheck_failed";
    case LocalSolveStatus::dependency_failure:
        return "dependency_failure";
    case LocalSolveStatus::internal_failure:
        return "internal_failure";
    }
    return "unknown";
}

void LocalSolveWorkspace::clear() noexcept
{
    constraints_.clear();
    first_constraint_buffer_.clear();
    second_constraint_buffer_.clear();
    variables_.fill(0.0);
    variable_lower_bounds_.fill(0.0);
    variable_upper_bounds_.fill(0.0);
}

std::size_t LocalSolveWorkspace::constraint_capacity() const noexcept { return constraints_.capacity(); }

double evaluate_local_objective(const LocalTransformStep& step) noexcept { return -step.volume_scale_multiplier; }

std::array<double, local_solve_variable_count> evaluate_local_objective_gradient() noexcept
{
    return { -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
}

double evaluate_local_constraint(const Point3& object_center, const LocalPlaneConstraint& constraint,
                                 const double padding, const LocalTransformStep& step)
{
    validate_constraint_inputs(object_center, constraint, padding, step);
    double value = 0.0;
    if (!detail::evaluate_local_constraints_unchecked(std::span(&constraint, 1), object_center, padding, step,
                                                      std::span(&value, 1))) {
        throw Error(ErrorCategory::invalid_configuration, "local constraint evaluation produced a non-finite value");
    }
    return value;
}

std::array<double, local_solve_variable_count> evaluate_local_constraint_gradient(
    const Point3& object_center, const LocalPlaneConstraint& constraint, const double padding,
    const LocalTransformStep& step)
{
    validate_constraint_inputs(object_center, constraint, padding, step);
    std::array<double, local_solve_variable_count> gradient {};
    if (!detail::evaluate_local_jacobian_unchecked(std::span(&constraint, 1), object_center, step,
                                                   std::span(gradient))) {
        throw Error(ErrorCategory::invalid_configuration,
                    "local constraint gradient evaluation produced a non-finite value");
    }
    return gradient;
}

Transform apply_reference_local_step(const Transform& current, const LocalTransformStep& step,
                                     const double maximum_result_volume_scale)
{
    if (!std::isfinite(current.volume_scale) || current.volume_scale <= 0.0 || !is_finite(current.rotation) ||
        !is_finite(current.translation) || !is_finite(step) || step.volume_scale_multiplier <= 0.0 ||
        !std::isfinite(maximum_result_volume_scale) || maximum_result_volume_scale <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "local transform update inputs must be finite and positive");
    }

    Transform result;
    // COMPATIBILITY(IROP-COMPAT-0001): The local NLP does not receive the
    // stage barrier; multiply the returned volume factor and clamp afterward.
    result.volume_scale = std::min(maximum_result_volume_scale, current.volume_scale * step.volume_scale_multiplier);

    // COMPATIBILITY(IROP-COMPAT-0006): Preserve componentwise Euler addition
    // even though it is not general rotation-matrix composition.
    if (!add_is_finite(current.rotation.x, step.rotation_delta.x, result.rotation.x) ||
        !add_is_finite(current.rotation.y, step.rotation_delta.y, result.rotation.y) ||
        !add_is_finite(current.rotation.z, step.rotation_delta.z, result.rotation.z) ||
        !add_is_finite(current.translation.x, step.translation_delta.x, result.translation.x) ||
        !add_is_finite(current.translation.y, step.translation_delta.y, result.translation.y) ||
        !add_is_finite(current.translation.z, step.translation_delta.z, result.translation.z) ||
        !std::isfinite(result.volume_scale) || result.volume_scale <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "local transform update overflowed");
    }
    return result;
}

}  // namespace irop
