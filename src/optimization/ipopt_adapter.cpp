#include <IpLinearSolvers.h>
#include <IpStdCInterface.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "irop/optimization/local_solver.hpp"
#include "optimization/local_solver_internal.hpp"

namespace irop {
namespace {

constexpr ipindex variable_count = static_cast<ipindex>(local_solve_variable_count);
constexpr ipnumber ipopt_infinity = 1.0e19;
constexpr ipnumber constraint_upper_bound = 2.0e19;
constexpr double unit_normal_squared_tolerance = 1.0e-10;
constexpr std::size_t elapsed_check_stride = 4'096;

static_assert(std::is_same_v<ipnumber, double>);
static_assert(local_solve_variable_count == 7);

enum class CallbackStopReason {
    none,
    resource_limit,
    time_limit,
    invalid_number,
    invalid_callback,
};

struct CallbackContext {
    std::span<const LocalPlaneConstraint> constraints;
    Point3 object_center;
    double padding = 0.0;
    const LocalSolveLimits* limits = nullptr;
    LocalSolveWork* work = nullptr;
    const std::chrono::steady_clock::time_point* start_time = nullptr;
    CallbackStopReason stop_reason = CallbackStopReason::none;
};

class IpoptProblemOwner final {
public:
    explicit IpoptProblemOwner(IpoptProblem problem) noexcept : problem_(problem) {}
    ~IpoptProblemOwner()
    {
        if (problem_ != nullptr) {
            FreeIpoptProblem(problem_);
        }
    }

    IpoptProblemOwner(const IpoptProblemOwner&) = delete;
    IpoptProblemOwner& operator=(const IpoptProblemOwner&) = delete;

    [[nodiscard]] IpoptProblem get() const noexcept { return problem_; }

private:
    IpoptProblem problem_ = nullptr;
};

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool is_finite(const EulerRotationRadians& rotation) noexcept
{
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z);
}

[[nodiscard]] bool is_finite(const Transform& transform) noexcept
{
    return std::isfinite(transform.volume_scale) && transform.volume_scale > 0.0 && is_finite(transform.rotation) &&
           is_finite(transform.translation);
}

[[nodiscard]] bool is_finite(const LocalTransformStep& step) noexcept
{
    return std::isfinite(step.volume_scale_multiplier) && step.volume_scale_multiplier > 0.0 &&
           is_finite(step.rotation_delta) && is_finite(step.translation_delta);
}

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

[[nodiscard]] LocalTransformStep step_for(const ipnumber* const values) noexcept
{
    return {
        values[0],
        { values[1], values[2], values[3] },
        { values[4], values[5], values[6] },
    };
}

void increment_saturated(std::uint64_t& value) noexcept
{
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

[[nodiscard]] bool add_bounded_work(const std::uint64_t amount, const std::uint64_t limit,
                                    std::uint64_t& value) noexcept
{
    if (value > limit || amount > limit - value) {
        return false;
    }
    value += amount;
    return true;
}

[[nodiscard]] bool elapsed_limit_reached(CallbackContext& context) noexcept
{
    if (context.limits == nullptr || context.start_time == nullptr || context.work == nullptr) {
        context.stop_reason = CallbackStopReason::invalid_callback;
        return true;
    }
    const auto elapsed = std::chrono::steady_clock::now() - *context.start_time;
    if (elapsed >= context.limits->max_elapsed_time) {
        context.stop_reason = CallbackStopReason::time_limit;
        return true;
    }
    return false;
}

[[nodiscard]] CallbackContext* callback_context(void* const user_data) noexcept
{
    return static_cast<CallbackContext*>(user_data);
}

[[nodiscard]] bool evaluate_constraints_with_deadline(CallbackContext& context, const LocalTransformStep& step,
                                                      const std::span<double> values) noexcept
{
    if (values.size() != context.constraints.size()) {
        return false;
    }
    for (std::size_t offset = 0; offset < context.constraints.size(); offset += elapsed_check_stride) {
        if (elapsed_limit_reached(context)) {
            return false;
        }
        const std::size_t count = std::min(elapsed_check_stride, context.constraints.size() - offset);
        if (!detail::evaluate_local_constraints_unchecked(context.constraints.subspan(offset, count),
                                                          context.object_center, context.padding, step,
                                                          values.subspan(offset, count))) {
            return false;
        }
    }
    return !elapsed_limit_reached(context);
}

[[nodiscard]] bool evaluate_jacobian_with_deadline(CallbackContext& context, const LocalTransformStep& step,
                                                   const std::span<double> values) noexcept
{
    if (context.constraints.size() > std::numeric_limits<std::size_t>::max() / local_solve_variable_count ||
        values.size() != context.constraints.size() * local_solve_variable_count) {
        return false;
    }
    for (std::size_t offset = 0; offset < context.constraints.size(); offset += elapsed_check_stride) {
        if (elapsed_limit_reached(context)) {
            return false;
        }
        const std::size_t count = std::min(elapsed_check_stride, context.constraints.size() - offset);
        if (!detail::evaluate_local_jacobian_unchecked(
                context.constraints.subspan(offset, count), context.object_center, step,
                values.subspan(offset * local_solve_variable_count, count * local_solve_variable_count))) {
            return false;
        }
    }
    return !elapsed_limit_reached(context);
}

[[nodiscard]] bool evaluate_applied_with_deadline(CallbackContext& context, const Transform& current,
                                                  const Transform& candidate, const std::span<double> values) noexcept
{
    if (values.size() != context.constraints.size()) {
        return false;
    }
    for (std::size_t offset = 0; offset < context.constraints.size(); offset += elapsed_check_stride) {
        if (elapsed_limit_reached(context)) {
            return false;
        }
        const std::size_t count = std::min(elapsed_check_stride, context.constraints.size() - offset);
        if (!detail::evaluate_applied_constraints_unchecked(context.constraints.subspan(offset, count), current,
                                                            candidate, context.padding,
                                                            values.subspan(offset, count))) {
            return false;
        }
    }
    return !elapsed_limit_reached(context);
}

bool evaluate_objective_callback(const ipindex n, ipnumber* const x, const bool, ipnumber* const objective,
                                 void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    if (context == nullptr || context->work == nullptr || n != variable_count || x == nullptr || objective == nullptr) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }
    increment_saturated(context->work->objective_evaluations);
    if (elapsed_limit_reached(*context)) {
        return false;
    }
    *objective = evaluate_local_objective(step_for(x));
    if (!std::isfinite(*objective)) {
        context->stop_reason = CallbackStopReason::invalid_number;
        return false;
    }
    return true;
}

bool evaluate_objective_gradient_callback(const ipindex n, ipnumber* const x, const bool, ipnumber* const gradient,
                                          void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    if (context == nullptr || context->work == nullptr || n != variable_count || x == nullptr || gradient == nullptr) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }
    increment_saturated(context->work->objective_gradient_evaluations);
    if (elapsed_limit_reached(*context)) {
        return false;
    }
    const auto values = evaluate_local_objective_gradient();
    std::copy(values.begin(), values.end(), gradient);
    return true;
}

bool evaluate_constraints_callback(const ipindex n, ipnumber* const x, const bool, const ipindex m,
                                   ipnumber* const values, void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    if (context == nullptr || context->work == nullptr || context->limits == nullptr || n != variable_count || m < 0 ||
        static_cast<std::size_t>(m) != context->constraints.size() || x == nullptr || values == nullptr) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }
    if (!add_bounded_work(static_cast<std::uint64_t>(m), context->limits->max_constraint_rows_evaluated,
                          context->work->constraint_rows_evaluated)) {
        context->stop_reason = CallbackStopReason::resource_limit;
        return false;
    }
    if (elapsed_limit_reached(*context)) {
        return false;
    }
    if (!evaluate_constraints_with_deadline(*context, step_for(x), std::span(values, static_cast<std::size_t>(m)))) {
        if (context->stop_reason == CallbackStopReason::none) {
            context->stop_reason = CallbackStopReason::invalid_number;
        }
        return false;
    }
    return true;
}

bool evaluate_jacobian_callback(const ipindex n, ipnumber* const x, const bool, const ipindex m,
                                const ipindex nonzero_count, ipindex* const rows, ipindex* const columns,
                                ipnumber* const values, void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    const std::uint64_t expected_nonzeros = m < 0 ? 0 : static_cast<std::uint64_t>(m) * local_solve_variable_count;
    if (context == nullptr || context->work == nullptr || context->limits == nullptr || n != variable_count || m < 0 ||
        static_cast<std::size_t>(m) != context->constraints.size() || nonzero_count < 0 ||
        static_cast<std::uint64_t>(nonzero_count) != expected_nonzeros) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }

    if (values == nullptr) {
        if (rows == nullptr || columns == nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
            return false;
        }
        for (ipindex row = 0; row < m; ++row) {
            if (row % static_cast<ipindex>(elapsed_check_stride) == 0 && elapsed_limit_reached(*context)) {
                return false;
            }
            for (ipindex column = 0; column < variable_count; ++column) {
                const ipindex offset = row * variable_count + column;
                rows[offset] = row;
                columns[offset] = column;
            }
        }
        return true;
    }

    if (x == nullptr || !add_bounded_work(expected_nonzeros, context->limits->max_jacobian_entries_evaluated,
                                          context->work->jacobian_entries_evaluated)) {
        context->stop_reason = x == nullptr ? CallbackStopReason::invalid_callback : CallbackStopReason::resource_limit;
        return false;
    }
    if (elapsed_limit_reached(*context)) {
        return false;
    }
    if (!evaluate_jacobian_with_deadline(*context, step_for(x),
                                         std::span(values, static_cast<std::size_t>(nonzero_count)))) {
        if (context->stop_reason == CallbackStopReason::none) {
            context->stop_reason = CallbackStopReason::invalid_number;
        }
        return false;
    }
    return true;
}

bool evaluate_hessian_callback(const ipindex n, ipnumber*, const bool, const ipnumber, const ipindex m, ipnumber*,
                               const bool, const ipindex nonzero_count, ipindex*, ipindex*, ipnumber*,
                               void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    if (context == nullptr || n != variable_count || m < 0 ||
        static_cast<std::size_t>(m) != context->constraints.size() || nonzero_count != 0) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }
    return true;
}

bool intermediate_callback(const ipindex, const ipindex iteration, const ipnumber, const ipnumber, const ipnumber,
                           const ipnumber, const ipnumber, const ipnumber, const ipnumber, const ipnumber,
                           const ipindex, void* const user_data) noexcept
{
    CallbackContext* const context = callback_context(user_data);
    if (context == nullptr || context->work == nullptr || context->limits == nullptr || iteration < 0) {
        if (context != nullptr) {
            context->stop_reason = CallbackStopReason::invalid_callback;
        }
        return false;
    }
    context->work->iterations = std::max(context->work->iterations, static_cast<std::uint64_t>(iteration));
    return !elapsed_limit_reached(*context);
}

[[nodiscard]] bool add_string_option(const IpoptProblem problem, const char* const name,
                                     const char* const value) noexcept
{
    return AddIpoptStrOption(problem, const_cast<char*>(name), const_cast<char*>(value));
}

[[nodiscard]] bool add_number_option(const IpoptProblem problem, const char* const name, const ipnumber value) noexcept
{
    return AddIpoptNumOption(problem, const_cast<char*>(name), value);
}

[[nodiscard]] bool add_integer_option(const IpoptProblem problem, const char* const name, const ipindex value) noexcept
{
    return AddIpoptIntOption(problem, const_cast<char*>(name), value);
}

[[nodiscard]] bool configure_problem(const IpoptProblem problem, const LocalSolveRequest& request,
                                     const LocalSolveLimits& limits) noexcept
{
    const double maximum_seconds = std::chrono::duration<double>(limits.max_elapsed_time).count();
    return add_string_option(problem, "option_file_name", "") && add_number_option(problem, "tol", request.tolerance) &&
           add_integer_option(problem, "max_iter", static_cast<ipindex>(limits.max_iterations)) &&
           add_number_option(problem, "max_wall_time", maximum_seconds) &&
           add_integer_option(problem, "print_level", 0) && add_string_option(problem, "sb", "yes") &&
           add_string_option(problem, "mu_strategy", "adaptive") &&
           add_string_option(problem, "hessian_approximation", "limited-memory") &&
           add_string_option(problem, "linear_solver", "mumps") &&
           add_number_option(problem, "bound_relax_factor", 0.0);
}

struct StatusTranslation {
    LocalSolveStatus status = LocalSolveStatus::internal_failure;
    const char* diagnostic = "Ipopt returned an unknown status";
    bool candidate_may_be_accepted = false;
};

[[nodiscard]] StatusTranslation translate_status(const ApplicationReturnStatus status,
                                                 const CallbackStopReason callback_stop) noexcept
{
    if (callback_stop == CallbackStopReason::resource_limit) {
        return { LocalSolveStatus::resource_exhausted, "local solve callback work limit exceeded", false };
    }
    if (callback_stop == CallbackStopReason::time_limit) {
        return { LocalSolveStatus::time_limit, "local solve elapsed-time limit exceeded", false };
    }
    if (callback_stop == CallbackStopReason::invalid_number) {
        return { LocalSolveStatus::numerical_failure, "local solve callback produced a non-finite value", false };
    }
    if (callback_stop == CallbackStopReason::invalid_callback) {
        return { LocalSolveStatus::dependency_failure, "Ipopt invoked a callback with an invalid contract", false };
    }

    switch (status) {
    case Solve_Succeeded:
        return { LocalSolveStatus::success, "Ipopt solve succeeded", true };
    case Solved_To_Acceptable_Level:
        return { LocalSolveStatus::acceptable, "Ipopt reached an acceptable solution", true };
    case Feasible_Point_Found:
        return { LocalSolveStatus::acceptable, "Ipopt found a feasible point", true };
    case Infeasible_Problem_Detected:
        return { LocalSolveStatus::infeasible, "Ipopt detected an infeasible problem", false };
    case Maximum_Iterations_Exceeded:
        return { LocalSolveStatus::iteration_limit, "Ipopt iteration limit exceeded", false };
    case Maximum_CpuTime_Exceeded:
    case Maximum_WallTime_Exceeded:
        return { LocalSolveStatus::time_limit, "Ipopt time limit exceeded", false };
    case Search_Direction_Becomes_Too_Small:
        return { LocalSolveStatus::numerical_failure, "Ipopt search direction became too small", false };
    case Diverging_Iterates:
        return { LocalSolveStatus::numerical_failure, "Ipopt iterates diverged", false };
    case User_Requested_Stop:
        return { LocalSolveStatus::dependency_failure, "Ipopt stopped without a project callback reason", false };
    case Restoration_Failed:
        return { LocalSolveStatus::numerical_failure, "Ipopt restoration phase failed", false };
    case Error_In_Step_Computation:
        return { LocalSolveStatus::numerical_failure, "Ipopt step computation failed", false };
    case Not_Enough_Degrees_Of_Freedom:
        return { LocalSolveStatus::numerical_failure, "Ipopt reported insufficient degrees of freedom", false };
    case Invalid_Number_Detected:
        return { LocalSolveStatus::numerical_failure, "Ipopt detected a non-finite number", false };
    case Invalid_Problem_Definition:
        return { LocalSolveStatus::dependency_failure, "Ipopt rejected the problem definition", false };
    case Invalid_Option:
        return { LocalSolveStatus::dependency_failure, "Ipopt rejected a required option", false };
    case Insufficient_Memory:
        return { LocalSolveStatus::resource_exhausted, "Ipopt reported insufficient memory", false };
    case Unrecoverable_Exception:
    case NonIpopt_Exception_Thrown:
    case Internal_Error:
        return { LocalSolveStatus::dependency_failure, "Ipopt reported an internal dependency failure", false };
    }
    return {};
}

[[nodiscard]] bool step_is_within_bounds(const LocalTransformStep& step,
                                         const std::array<double, local_solve_variable_count>& lower,
                                         const std::array<double, local_solve_variable_count>& upper,
                                         const double tolerance) noexcept
{
    const std::array<double, local_solve_variable_count> values {
        step.volume_scale_multiplier, step.rotation_delta.x,    step.rotation_delta.y,    step.rotation_delta.z,
        step.translation_delta.x,     step.translation_delta.y, step.translation_delta.z,
    };
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            return false;
        }
        if (lower[index] > -ipopt_infinity) {
            const double lower_allowance = tolerance * std::max(1.0, std::abs(lower[index]));
            if (values[index] < lower[index] - lower_allowance) {
                return false;
            }
        }
        if (upper[index] < ipopt_infinity) {
            const double upper_allowance = tolerance * std::max(1.0, std::abs(upper[index]));
            if (values[index] > upper[index] + upper_allowance) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::optional<double> minimum_finite_value(const std::span<const double> values) noexcept
{
    if (values.empty()) {
        return std::nullopt;
    }
    double minimum = values.front();
    if (!std::isfinite(minimum)) {
        return std::nullopt;
    }
    for (const double value : values.subspan(1)) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        minimum = std::min(minimum, value);
    }
    return minimum;
}

[[nodiscard]] bool all_variables_fixed(const std::array<double, local_solve_variable_count>& lower,
                                       const std::array<double, local_solve_variable_count>& upper) noexcept
{
    for (std::size_t index = 0; index < lower.size(); ++index) {
        if (lower[index] != upper[index]) {
            return false;
        }
    }
    return true;
}

void set_diagnostic_best_effort(LocalSolveResult& result, const char* const diagnostic) noexcept
{
    try {
        result.diagnostic = diagnostic;
    }
    catch (...) {
        result.diagnostic.clear();
    }
}

}  // namespace

LocalSolveResult solve_local_transform(const TetrahedralMesh& mesh, const CatConstructionResult& cat,
                                       const LocalSolveRequest& request, LocalSolveWorkspace& workspace,
                                       const LocalSolveLimits& limits) noexcept
{
    const auto start_time = std::chrono::steady_clock::now();
    LocalSolveResult result;
    auto finish = [&result, &start_time]() noexcept -> LocalSolveResult {
        result.work.elapsed_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        return std::move(result);
    };

    try {
        // DEVIATION(IROP-DEV-0016): Reject singular, malformed, and
        // non-finite local problems instead of passing unsafe data to a
        // numerical dependency.
        if (!cat.succeeded()) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve requires a successful CAT result";
            return finish();
        }
        if (mesh.points.size() != mesh.point_owners.size() || mesh.participant_count < 2 ||
            request.participant >= mesh.participant_count - 1 ||
            static_cast<std::uint64_t>(request.participant) >= cat.participant_ranges.size()) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve participant or tetrahedral mesh ownership is invalid";
            return finish();
        }
        if (!is_finite(request.current_transform) || !std::isfinite(request.padding) || request.padding < 0.0 ||
            !std::isfinite(request.maximum_result_volume_scale) || request.maximum_result_volume_scale <= 0.0 ||
            !std::isfinite(request.tolerance) || request.tolerance <= 0.0 ||
            request.tolerance > maximum_local_solve_tolerance) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve transform, padding, scale barrier, or tolerance is outside its domain";
            return finish();
        }

        const LocalSolveBounds& bounds = request.bounds;
        if (!std::isfinite(bounds.minimum_volume_scale_multiplier) || bounds.minimum_volume_scale_multiplier <= 0.0 ||
            bounds.minimum_volume_scale_multiplier >= ipopt_infinity ||
            !std::isfinite(bounds.maximum_absolute_rotation_delta_radians) ||
            bounds.maximum_absolute_rotation_delta_radians < 0.0 ||
            bounds.maximum_absolute_rotation_delta_radians > maximum_local_solve_rotation_delta_radians) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve scale and rotation bounds are invalid";
            return finish();
        }
        if (bounds.maximum_volume_scale_multiplier.has_value() &&
            (!std::isfinite(*bounds.maximum_volume_scale_multiplier) ||
             *bounds.maximum_volume_scale_multiplier < bounds.minimum_volume_scale_multiplier ||
             *bounds.maximum_volume_scale_multiplier >= ipopt_infinity)) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve maximum volume multiplier is invalid";
            return finish();
        }
        if (bounds.maximum_absolute_translation.has_value() &&
            (!std::isfinite(*bounds.maximum_absolute_translation) || *bounds.maximum_absolute_translation < 0.0 ||
             *bounds.maximum_absolute_translation >= ipopt_infinity)) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve translation bound is invalid";
            return finish();
        }
        if (limits.max_iterations == 0 ||
            limits.max_iterations > static_cast<std::uint64_t>(std::numeric_limits<ipindex>::max()) ||
            limits.max_elapsed_time <= std::chrono::milliseconds::zero()) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve iteration and elapsed-time limits must be positive and representable";
            return finish();
        }

        const CatParticipantRange& participant_range = cat.participant_ranges[request.participant];
        if (participant_range.constraint_begin > cat.constraints.size() ||
            participant_range.constraint_count > cat.constraints.size() - participant_range.constraint_begin) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve CAT participant constraint range is invalid";
            return finish();
        }
        if (participant_range.constraint_count == 0) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve requires at least one CAT constraint";
            return finish();
        }
        if (participant_range.constraint_count > limits.max_constraints ||
            participant_range.constraint_count > static_cast<std::uint64_t>(std::numeric_limits<ipindex>::max())) {
            result.status = LocalSolveStatus::resource_exhausted;
            result.diagnostic = "local solve constraint count exceeds a configured or dependency limit";
            return finish();
        }
        const std::uint64_t jacobian_entry_count = participant_range.constraint_count * local_solve_variable_count;
        if (jacobian_entry_count > limits.max_dense_jacobian_entries ||
            jacobian_entry_count > static_cast<std::uint64_t>(std::numeric_limits<ipindex>::max())) {
            result.status = LocalSolveStatus::resource_exhausted;
            result.diagnostic = "local solve dense Jacobian size exceeds a configured or dependency limit";
            return finish();
        }

        workspace.constraints_.clear();
        workspace.constraints_.reserve(static_cast<std::size_t>(participant_range.constraint_count));
        const std::uint64_t constraint_end = participant_range.constraint_begin + participant_range.constraint_count;
        for (std::uint64_t index = participant_range.constraint_begin; index < constraint_end; ++index) {
            const CatPlaneConstraint& source = cat.constraints[static_cast<std::size_t>(index)];
            if (source.owner != request.participant || source.source_point >= mesh.points.size()) {
                result.status = LocalSolveStatus::invalid_input;
                result.diagnostic = "local solve CAT constraint ownership or source point is invalid";
                return finish();
            }
            const std::size_t source_index = static_cast<std::size_t>(source.source_point);
            if (mesh.point_owners[source_index] != request.participant || !is_finite(mesh.points[source_index]) ||
                !is_finite(source.plane_point) || !has_unit_normal(source.inward_unit_normal)) {
                result.status = LocalSolveStatus::invalid_input;
                result.diagnostic = "local solve CAT constraint geometry is malformed or non-finite";
                return finish();
            }
            workspace.constraints_.push_back(
                { mesh.points[source_index], source.plane_point, source.inward_unit_normal });
        }
        result.work.constraints_prepared = participant_range.constraint_count;
        workspace.first_constraint_buffer_.resize(workspace.constraints_.size());
        workspace.second_constraint_buffer_.resize(workspace.constraints_.size());

        workspace.variable_lower_bounds_ = {
            bounds.minimum_volume_scale_multiplier,
            -bounds.maximum_absolute_rotation_delta_radians,
            -bounds.maximum_absolute_rotation_delta_radians,
            -bounds.maximum_absolute_rotation_delta_radians,
            -bounds.maximum_absolute_translation.value_or(ipopt_infinity),
            -bounds.maximum_absolute_translation.value_or(ipopt_infinity),
            -bounds.maximum_absolute_translation.value_or(ipopt_infinity),
        };
        workspace.variable_upper_bounds_ = {
            bounds.maximum_volume_scale_multiplier.value_or(ipopt_infinity),
            bounds.maximum_absolute_rotation_delta_radians,
            bounds.maximum_absolute_rotation_delta_radians,
            bounds.maximum_absolute_rotation_delta_radians,
            bounds.maximum_absolute_translation.value_or(ipopt_infinity),
            bounds.maximum_absolute_translation.value_or(ipopt_infinity),
            bounds.maximum_absolute_translation.value_or(ipopt_infinity),
        };
        workspace.variables_ = {
            request.initial_guess.volume_scale_multiplier, request.initial_guess.rotation_delta.x,
            request.initial_guess.rotation_delta.y,        request.initial_guess.rotation_delta.z,
            request.initial_guess.translation_delta.x,     request.initial_guess.translation_delta.y,
            request.initial_guess.translation_delta.z,
        };
        if (!is_finite(request.initial_guess) ||
            !step_is_within_bounds(request.initial_guess, workspace.variable_lower_bounds_,
                                   workspace.variable_upper_bounds_, 0.0)) {
            result.status = LocalSolveStatus::invalid_input;
            result.diagnostic = "local solve initial guess must be finite, positive, and inside all bounds";
            return finish();
        }

        CallbackContext context {
            workspace.constraints_, request.current_transform.translation,
            request.padding,        &limits,
            &result.work,           &start_time,
        };
        StatusTranslation translation;
        if (all_variables_fixed(workspace.variable_lower_bounds_, workspace.variable_upper_bounds_)) {
            const std::uint64_t row_count = static_cast<std::uint64_t>(workspace.constraints_.size());
            if (!add_bounded_work(row_count, limits.max_constraint_rows_evaluated,
                                  result.work.constraint_rows_evaluated)) {
                result.status = LocalSolveStatus::resource_exhausted;
                result.diagnostic = "local solve fixed-point evaluation exceeds the callback work limit";
                return finish();
            }
            if (elapsed_limit_reached(context)) {
                result.status = LocalSolveStatus::time_limit;
                result.diagnostic = "local solve elapsed-time limit exceeded during preparation";
                return finish();
            }
            if (!evaluate_constraints_with_deadline(context, request.initial_guess,
                                                    std::span(workspace.first_constraint_buffer_))) {
                result.status = context.stop_reason == CallbackStopReason::time_limit
                                    ? LocalSolveStatus::time_limit
                                    : LocalSolveStatus::numerical_failure;
                result.diagnostic = context.stop_reason == CallbackStopReason::time_limit
                                        ? "fixed local solve elapsed-time limit exceeded"
                                        : "fixed local solve produced a non-finite constraint value";
                return finish();
            }
            const auto minimum = minimum_finite_value(workspace.first_constraint_buffer_);
            if (!minimum.has_value() || *minimum < -request.tolerance) {
                result.status = LocalSolveStatus::infeasible;
                result.work.minimum_solver_constraint = minimum;
                result.diagnostic = "fixed local solve is infeasible";
                return finish();
            }
            translation = { LocalSolveStatus::success, "fixed local solve is feasible", true };
        }
        else {
            int version_major = 0;
            int version_minor = 0;
            int version_release = 0;
            GetIpoptVersion(&version_major, &version_minor, &version_release);
            if (version_major != 3 || version_minor != 14 || version_release != 19) {
                result.status = LocalSolveStatus::dependency_failure;
                result.diagnostic = "loaded Ipopt runtime does not match required version 3.14.19";
                return finish();
            }
            if ((IpoptGetAvailableLinearSolvers(1) & IPOPTLINEARSOLVER_MUMPS) == 0U) {
                result.status = LocalSolveStatus::dependency_failure;
                result.diagnostic = "loaded Ipopt runtime does not contain the required MUMPS backend";
                return finish();
            }

            std::fill(workspace.first_constraint_buffer_.begin(), workspace.first_constraint_buffer_.end(), 0.0);
            std::fill(workspace.second_constraint_buffer_.begin(), workspace.second_constraint_buffer_.end(),
                      constraint_upper_bound);
            const auto constraint_count = static_cast<ipindex>(workspace.constraints_.size());
            const auto nonzero_count = static_cast<ipindex>(jacobian_entry_count);
            IpoptProblemOwner problem(CreateIpoptProblem(
                variable_count, workspace.variable_lower_bounds_.data(), workspace.variable_upper_bounds_.data(),
                constraint_count, workspace.first_constraint_buffer_.data(), workspace.second_constraint_buffer_.data(),
                nonzero_count, 0, 0, evaluate_objective_callback, evaluate_constraints_callback,
                evaluate_objective_gradient_callback, evaluate_jacobian_callback, evaluate_hessian_callback));
            if (problem.get() == nullptr) {
                result.status = LocalSolveStatus::dependency_failure;
                result.diagnostic = "Ipopt could not create the local nonlinear problem";
                return finish();
            }
            if (!configure_problem(problem.get(), request, limits) ||
                !SetIntermediateCallback(problem.get(), intermediate_callback)) {
                result.status = LocalSolveStatus::dependency_failure;
                result.diagnostic = "Ipopt rejected required local-solve options or callback setup";
                return finish();
            }
            if (elapsed_limit_reached(context)) {
                result.status = LocalSolveStatus::time_limit;
                result.diagnostic = "local solve elapsed-time limit exceeded during preparation";
                return finish();
            }

            ipnumber objective_value = 0.0;
            const ApplicationReturnStatus raw_status =
                IpoptSolve(problem.get(), workspace.variables_.data(), workspace.first_constraint_buffer_.data(),
                           &objective_value, nullptr, nullptr, nullptr, &context);
            translation = translate_status(raw_status, context.stop_reason);
        }

        // DEVIATION(IROP-DEV-0015): The Python implementation discards the
        // backend status and applies its iterate unconditionally. Publish no
        // accepted transform unless Ipopt and both project postchecks agree.
        result.status = translation.status;
        result.diagnostic = translation.diagnostic;
        if (!translation.candidate_may_be_accepted) {
            return finish();
        }

        const LocalTransformStep candidate = step_for(workspace.variables_.data());
        if (!is_finite(candidate) || !step_is_within_bounds(candidate, workspace.variable_lower_bounds_,
                                                            workspace.variable_upper_bounds_, request.tolerance)) {
            result.status = LocalSolveStatus::postcheck_failed;
            result.diagnostic = "Ipopt candidate is non-finite or outside the requested bounds";
            return finish();
        }
        result.candidate_step = candidate;

        const std::uint64_t row_count = static_cast<std::uint64_t>(workspace.constraints_.size());
        if (!add_bounded_work(row_count, limits.max_constraint_rows_evaluated, result.work.constraint_rows_evaluated)) {
            result.status = LocalSolveStatus::resource_exhausted;
            result.diagnostic = "local solve constraint postcheck exceeds the callback work limit";
            return finish();
        }
        if (!evaluate_constraints_with_deadline(context, candidate, std::span(workspace.first_constraint_buffer_))) {
            result.status = context.stop_reason == CallbackStopReason::time_limit ? LocalSolveStatus::time_limit
                                                                                  : LocalSolveStatus::postcheck_failed;
            result.diagnostic = context.stop_reason == CallbackStopReason::time_limit
                                    ? "local solve elapsed-time limit exceeded during solver postcheck"
                                    : "Ipopt candidate produced non-finite solver constraints";
            return finish();
        }
        result.work.minimum_solver_constraint = minimum_finite_value(workspace.first_constraint_buffer_);
        const double feasibility_tolerance = std::max(1.0e-10, 10.0 * request.tolerance);
        if (!result.work.minimum_solver_constraint.has_value() ||
            *result.work.minimum_solver_constraint < -feasibility_tolerance) {
            result.status = LocalSolveStatus::postcheck_failed;
            result.diagnostic = "Ipopt candidate failed independent solver-constraint validation";
            return finish();
        }

        Transform applied;
        try {
            applied =
                apply_reference_local_step(request.current_transform, candidate, request.maximum_result_volume_scale);
        }
        catch (...) {
            result.status = LocalSolveStatus::postcheck_failed;
            result.diagnostic = "Ipopt candidate could not be applied safely";
            return finish();
        }
        if (!add_bounded_work(row_count, limits.max_constraint_rows_evaluated, result.work.constraint_rows_evaluated)) {
            result.status = LocalSolveStatus::resource_exhausted;
            result.diagnostic = "local solve applied-transform postcheck exceeds the callback work limit";
            return finish();
        }
        if (!evaluate_applied_with_deadline(context, request.current_transform, applied,
                                            std::span(workspace.second_constraint_buffer_))) {
            result.status = context.stop_reason == CallbackStopReason::time_limit ? LocalSolveStatus::time_limit
                                                                                  : LocalSolveStatus::postcheck_failed;
            result.diagnostic = context.stop_reason == CallbackStopReason::time_limit
                                    ? "local solve elapsed-time limit exceeded during applied-transform postcheck"
                                    : "applied local transform produced non-finite constraints";
            return finish();
        }
        result.work.minimum_applied_constraint = minimum_finite_value(workspace.second_constraint_buffer_);
        if (!result.work.minimum_applied_constraint.has_value() ||
            *result.work.minimum_applied_constraint < -feasibility_tolerance) {
            result.status = LocalSolveStatus::postcheck_failed;
            result.diagnostic = "reference-compatible applied transform is infeasible";
            return finish();
        }
        if (elapsed_limit_reached(context)) {
            result.status = LocalSolveStatus::time_limit;
            result.diagnostic = "local solve elapsed-time limit exceeded before acceptance";
            return finish();
        }

        result.status = translation.status;
        result.diagnostic = translation.diagnostic;
        result.accepted_transform = applied;
        return finish();
    }
    catch (const std::bad_alloc&) {
        result.accepted_transform.reset();
        result.status = LocalSolveStatus::resource_exhausted;
        set_diagnostic_best_effort(result, "local solve allocation failed");
        return finish();
    }
    catch (const std::length_error&) {
        result.accepted_transform.reset();
        result.status = LocalSolveStatus::resource_exhausted;
        set_diagnostic_best_effort(result, "local solve workspace size is not representable");
        return finish();
    }
    catch (...) {
        result.accepted_transform.reset();
        result.status = LocalSolveStatus::internal_failure;
        set_diagnostic_best_effort(result, "unexpected local solve failure");
        return finish();
    }
}

}  // namespace irop
