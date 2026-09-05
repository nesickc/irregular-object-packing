#include "irop/packing/packing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "irop/error.hpp"
#include "irop/geometry/transform.hpp"

namespace irop {
namespace {

using Clock = std::chrono::steady_clock;

class StageTimer final {
public:
    explicit StageTimer(std::chrono::microseconds& destination) : destination_(destination) {}
    ~StageTimer() { destination_ += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_); }
    StageTimer(const StageTimer&) = delete;
    StageTimer& operator=(const StageTimer&) = delete;

private:
    std::chrono::microseconds& destination_;
    Clock::time_point start_ = Clock::now();
};

template <typename Operation>
auto measure_stage(std::chrono::microseconds& destination, Operation&& operation)
{
    const StageTimer timer(destination);
    return std::forward<Operation>(operation)();
}

[[nodiscard]] std::string bounded_reason(const std::string& reason) { return reason.substr(0, 2'048); }

constexpr double reference_rotation_bound_factor = 0.9;
constexpr std::uint64_t maximum_exact_binary64_integer = 9'007'199'254'740'992ULL;

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

void add_count(std::uint64_t& destination, const std::uint64_t value)
{
    if (value > std::numeric_limits<std::uint64_t>::max() - destination) {
        throw Error(ErrorCategory::resource_limit, "packing work counter overflowed");
    }
    destination += value;
}

void increment(std::uint64_t& value) { add_count(value, 1); }

void add_elapsed(std::chrono::milliseconds& destination, const std::chrono::milliseconds value)
{
    using Representation = std::chrono::milliseconds::rep;
    if (value.count() < 0 || destination.count() > std::numeric_limits<Representation>::max() - value.count()) {
        throw Error(ErrorCategory::resource_limit, "packing elapsed-work counter overflowed");
    }
    destination += value;
}

void accumulate(TetrahedralizationWork& destination, const TetrahedralizationWork& source)
{
    add_count(destination.input_participants, source.input_participants);
    add_count(destination.input_points, source.input_points);
    add_count(destination.input_triangles, source.input_triangles);
    add_count(destination.output_points, source.output_points);
    add_count(destination.output_tetrahedra, source.output_tetrahedra);
}

void accumulate(CatConstructionWork& destination, const CatConstructionWork& source)
{
    add_count(destination.points_examined, source.points_examined);
    add_count(destination.tetrahedra_examined, source.tetrahedra_examined);
    add_count(destination.relevant_tetrahedra, source.relevant_tetrahedra);
    add_count(destination.skipped_single_participant_tetrahedra, source.skipped_single_participant_tetrahedra);
    add_count(destination.polygons_generated, source.polygons_generated);
    add_count(destination.polygon_vertices_generated, source.polygon_vertices_generated);
    add_count(destination.constraints_generated, source.constraints_generated);
}

void accumulate(LocalSolveWork& destination, const LocalSolveWork& source)
{
    add_count(destination.constraints_prepared, source.constraints_prepared);
    add_count(destination.objective_evaluations, source.objective_evaluations);
    add_count(destination.objective_gradient_evaluations, source.objective_gradient_evaluations);
    add_count(destination.constraint_rows_evaluated, source.constraint_rows_evaluated);
    add_count(destination.jacobian_entries_evaluated, source.jacobian_entries_evaluated);
    add_count(destination.iterations, source.iterations);
    add_elapsed(destination.elapsed_time, source.elapsed_time);

    if (source.minimum_solver_constraint.has_value() &&
        (!destination.minimum_solver_constraint.has_value() ||
         *source.minimum_solver_constraint < *destination.minimum_solver_constraint)) {
        destination.minimum_solver_constraint = source.minimum_solver_constraint;
    }
    if (source.minimum_applied_constraint.has_value() &&
        (!destination.minimum_applied_constraint.has_value() ||
         *source.minimum_applied_constraint < *destination.minimum_applied_constraint)) {
        destination.minimum_applied_constraint = source.minimum_applied_constraint;
    }
}

void accumulate(SceneCollisionWork& destination, const SceneCollisionWork& source)
{
    add_count(destination.object_pairs_examined, source.object_pairs_examined);
    add_count(destination.triangle_pairs_tested, source.triangle_pairs_tested);
    add_count(destination.containment_triangle_visits, source.containment_triangle_visits);
}

[[nodiscard]] SceneCollisionLimits remaining_collision_limits(const SceneCollisionWork& consumed,
                                                              const SceneCollisionLimits& configured)
{
    if (consumed.object_pairs_examined >= configured.max_object_pair_checks ||
        consumed.triangle_pairs_tested >= configured.max_triangle_pair_tests ||
        consumed.containment_triangle_visits >= configured.max_containment_triangle_visits) {
        throw Error(ErrorCategory::resource_limit, "packing total collision-validation work limit was exhausted");
    }
    return {
        .max_triangle_pair_tests = configured.max_triangle_pair_tests - consumed.triangle_pairs_tested,
        .max_containment_triangle_visits =
            configured.max_containment_triangle_visits - consumed.containment_triangle_visits,
        .max_reported_violations = configured.max_reported_violations,
        .max_object_pair_checks = configured.max_object_pair_checks - consumed.object_pairs_examined,
    };
}

[[nodiscard]] PackingStatus status_for(const ErrorCategory category) noexcept
{
    switch (category) {
    case ErrorCategory::invalid_configuration:
    case ErrorCategory::input_io:
    case ErrorCategory::invalid_mesh:
        return PackingStatus::invalid_input;
    case ErrorCategory::resource_limit:
        return PackingStatus::resource_exhausted;
    case ErrorCategory::dependency_failure:
        return PackingStatus::dependency_failure;
    case ErrorCategory::cancelled:
        return PackingStatus::cancelled;
    case ErrorCategory::output_io:
    case ErrorCategory::internal:
        return PackingStatus::internal_failure;
    }
    return PackingStatus::internal_failure;
}

[[nodiscard]] PackingStatus status_for(const TetrahedralizationStatus status) noexcept
{
    switch (status) {
    case TetrahedralizationStatus::success:
        return PackingStatus::internal_failure;
    case TetrahedralizationStatus::invalid_input:
        return PackingStatus::invalid_input;
    case TetrahedralizationStatus::resource_exhausted:
        return PackingStatus::resource_exhausted;
    case TetrahedralizationStatus::dependency_failure:
        return PackingStatus::dependency_failure;
    }
    return PackingStatus::internal_failure;
}

[[nodiscard]] PackingStatus status_for(const CatConstructionStatus status) noexcept
{
    switch (status) {
    case CatConstructionStatus::success:
        return PackingStatus::internal_failure;
    case CatConstructionStatus::invalid_input:
        return PackingStatus::invalid_input;
    case CatConstructionStatus::resource_exhausted:
        return PackingStatus::resource_exhausted;
    }
    return PackingStatus::internal_failure;
}

[[nodiscard]] PackingStatus status_for(const LocalSolveStatus status) noexcept
{
    switch (status) {
    case LocalSolveStatus::success:
    case LocalSolveStatus::acceptable:
        return PackingStatus::internal_failure;
    case LocalSolveStatus::invalid_input:
        return PackingStatus::invalid_input;
    case LocalSolveStatus::resource_exhausted:
        return PackingStatus::resource_exhausted;
    case LocalSolveStatus::infeasible:
    case LocalSolveStatus::postcheck_failed:
        return PackingStatus::infeasible;
    case LocalSolveStatus::iteration_limit:
        return PackingStatus::iteration_limit;
    case LocalSolveStatus::time_limit:
        return PackingStatus::time_limit;
    case LocalSolveStatus::numerical_failure:
        return PackingStatus::numerical_failure;
    case LocalSolveStatus::dependency_failure:
        return PackingStatus::dependency_failure;
    case LocalSolveStatus::internal_failure:
        return PackingStatus::internal_failure;
    }
    return PackingStatus::internal_failure;
}

[[nodiscard]] std::string nested_diagnostic(const char* stage, const char* status, const std::string& diagnostic)
{
    std::string message = stage;
    message += " returned ";
    message += status;
    if (!diagnostic.empty()) {
        message += ": ";
        message += diagnostic;
    }
    return message;
}

[[nodiscard]] std::uint64_t object_count_at_target(const std::vector<Transform>& transforms,
                                                   const double target) noexcept
{
    return static_cast<std::uint64_t>(
        std::count_if(transforms.begin(), transforms.end(), [target](const Transform& transform) {
        return transform.volume_scale >= target;
    }));
}

void require_scene_mesh_budget(const TriangleMesh& object, const std::uint64_t object_count, const MeshLimits& limits,
                               const char* description)
{
    if (limits.max_vertices == 0 || limits.max_triangles == 0) {
        throw Error(ErrorCategory::invalid_configuration, std::string(description) + " mesh limits must be positive");
    }
    if (object_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw Error(ErrorCategory::resource_limit, std::string(description) + " object count is not representable");
    }

    const std::uint64_t vertices = static_cast<std::uint64_t>(object.vertices.size());
    const std::uint64_t triangles = static_cast<std::uint64_t>(object.triangles.size());
    if ((vertices != 0 && object_count > limits.max_vertices / vertices) ||
        (triangles != 0 && object_count > limits.max_triangles / triangles)) {
        throw Error(ErrorCategory::resource_limit, std::string(description) + " object scene exceeds mesh limits");
    }
}

[[nodiscard]] std::vector<TriangleMesh> instantiate(const TriangleMesh& object,
                                                    const std::vector<Transform>& transforms, const MeshLimits& limits,
                                                    const char* description)
{
    require_scene_mesh_budget(object, static_cast<std::uint64_t>(transforms.size()), limits, description);
    std::vector<TriangleMesh> objects;
    objects.reserve(transforms.size());
    for (const Transform& transform : transforms) {
        objects.push_back(transform_mesh(object, transform));
    }
    return objects;
}

[[nodiscard]] bool range_is_valid(const std::uint64_t begin, const std::uint64_t count,
                                  const std::size_t available) noexcept
{
    return begin <= available && count <= static_cast<std::uint64_t>(available) - begin;
}

[[nodiscard]] std::vector<TriangleMesh> make_cat_surfaces(const CatConstructionResult& cat,
                                                          const std::uint64_t object_count, const MeshLimits& limits)
{
    if (object_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        cat.participant_ranges.size() != object_count + 1) {
        throw Error(ErrorCategory::internal, "CAT participant polygon ranges do not match the packing scene");
    }

    std::uint64_t total_vertices = 0;
    std::uint64_t total_triangles = 0;
    std::vector<TriangleMesh> surfaces(static_cast<std::size_t>(object_count));
    for (std::uint64_t participant = 0; participant < object_count; ++participant) {
        const CatParticipantRange& range = cat.participant_ranges[static_cast<std::size_t>(participant)];
        if (!range_is_valid(range.polygon_begin, range.polygon_count, cat.polygons.size())) {
            throw Error(ErrorCategory::internal, "CAT participant polygon range is invalid");
        }

        TriangleMesh& surface = surfaces[static_cast<std::size_t>(participant)];
        const std::uint64_t end = range.polygon_begin + range.polygon_count;
        for (std::uint64_t index = range.polygon_begin; index < end; ++index) {
            const CatPolygon& polygon = cat.polygons[static_cast<std::size_t>(index)];
            if (polygon.owner != participant || (polygon.vertex_count != 3 && polygon.vertex_count != 4)) {
                throw Error(ErrorCategory::internal, "CAT participant polygon ownership or arity is invalid");
            }

            const std::uint64_t polygon_triangles = polygon.vertex_count == 3 ? 1 : 2;
            add_count(total_vertices, polygon.vertex_count);
            add_count(total_triangles, polygon_triangles);
            if (total_vertices > limits.max_vertices || total_triangles > limits.max_triangles) {
                throw Error(ErrorCategory::resource_limit, "CAT collision surfaces exceed intermediate mesh limits");
            }

            const MeshIndex base = static_cast<MeshIndex>(surface.vertices.size());
            for (std::uint8_t vertex = 0; vertex < polygon.vertex_count; ++vertex) {
                const Point3 point = polygon.vertices[vertex];
                if (!is_finite(point)) {
                    throw Error(ErrorCategory::internal, "CAT collision surface contains a non-finite point");
                }
                surface.vertices.push_back(point);
            }
            surface.triangles.push_back({ base, base + 1, base + 2 });
            if (polygon.vertex_count == 4) {
                surface.triangles.push_back({ base, base + 2, base + 3 });
            }
        }
    }
    return surfaces;
}

[[nodiscard]] double validate_and_resolve(const PackingState& state, const PackingAlgorithmConfig& config,
                                          const PackingEngineLimits& limits)
{
    const double initial_scale = state.config.initial_volume_scale;
    if (state.transforms.empty() || state.transforms.size() != state.config.object_count ||
        state.transforms.size() > std::numeric_limits<ParticipantId>::max()) {
        throw Error(ErrorCategory::invalid_configuration, "packing state object ownership is inconsistent");
    }
    validate_packing_algorithm_config(initial_scale, config, limits);

    double maximum_translation = 0.0;
    if (config.maximum_translation_per_unit_scale.has_value()) {
        maximum_translation = *config.maximum_translation_per_unit_scale;
    }
    else {
        if (!std::isfinite(state.object_volume) || state.object_volume <= 0.0) {
            throw Error(ErrorCategory::invalid_configuration,
                        "packing object volume is invalid for the default translation bound");
        }
        // DEVIATION(IROP-DEV-0021): Python effectively treats an omitted
        // translation setting as unbounded. Use an object-size-derived finite
        // default so the local search remains bounded and reproducible.
        maximum_translation = 2.0 * std::cbrt(state.object_volume);
    }
    return maximum_translation;
}

[[nodiscard]] SurfaceResamplingLimits effective_resampling_limits(const PackingEngineLimits& limits)
{
    SurfaceResamplingLimits result = limits.resampling;
    result.mesh_limits.max_vertices =
        std::min(result.mesh_limits.max_vertices, limits.intermediate_mesh_limits.max_vertices);
    result.mesh_limits.max_triangles =
        std::min(result.mesh_limits.max_triangles, limits.intermediate_mesh_limits.max_triangles);
    return result;
}

[[nodiscard]] PackingState recovered_state(const PackingState& current, const double factor)
{
    PackingState recovered = current;
    std::vector<Transform> transforms = current.transforms;
    for (Transform& transform : transforms) {
        const double reduced = transform.volume_scale * factor;
        if (!std::isfinite(reduced) || reduced <= 0.0) {
            throw Error(ErrorCategory::internal, "tetrahedralization recovery produced an invalid volume scale");
        }
        transform.volume_scale = reduced;
    }
    recovered.transforms = std::move(transforms);
    return recovered;
}

}  // namespace
namespace detail {

PhysicalCollisionCorrectionResult correct_physical_collisions(
    const TriangleMesh& centered_object, const TriangleMesh& container,
    const std::span<const TriangleMesh> cat_surfaces, std::vector<Transform> candidate_transforms,
    const double correction_volume_scale_factor, const std::uint64_t maximum_correction_passes,
    const MeshLimits& output_mesh_limits, const SceneCollisionLimits& collision_limits,
    SceneCollisionWork& cumulative_collision_work, std::uint64_t& cumulative_correction_passes,
    const std::function<std::optional<PackingStatus>()>& stopped)
{
    PhysicalCollisionCorrectionResult result;
    result.transforms = std::move(candidate_transforms);

    const auto stop_status = [&]() -> std::optional<PackingStatus> {
        return stopped ? stopped() : std::nullopt;
    };
    const auto finish = [&](const PackingStatus status, std::string diagnostic = {}) {
        result.status = status;
        result.diagnostic = std::move(diagnostic);
        return std::move(result);
    };
    const auto validate_candidate = [&]() -> std::optional<PackingStatus> {
        const std::vector<TriangleMesh> objects =
            instantiate(centered_object, result.transforms, output_mesh_limits, "full-resolution correction");
        if (const std::optional<PackingStatus> status = stop_status(); status.has_value()) {
            return status;
        }

        SceneCollisionReport collision = validate_scene_collisions(
            objects, container, cat_surfaces, remaining_collision_limits(cumulative_collision_work, collision_limits));
        accumulate(cumulative_collision_work, collision.work);
        result.collision = std::move(collision);
        return stop_status();
    };

    if (const std::optional<PackingStatus> status = validate_candidate(); status.has_value()) {
        return finish(*status);
    }

    while (!result.collision.physical_scene_valid()) {
        if (result.correction_passes >= maximum_correction_passes) {
            // DEVIATION(IROP-DEV-0004): Bound the Python
            // collision-correction loop and return a structured
            // outcome when it cannot converge.
            return finish(PackingStatus::correction_limit, "packing collision-correction pass limit was exhausted");
        }
        if (const std::optional<PackingStatus> status = stop_status(); status.has_value()) {
            return finish(*status);
        }

        std::vector<bool> reduce(result.transforms.size(), false);
        for (const std::uint64_t object : result.collision.container_violation_object_ids) {
            if (object >= result.transforms.size()) {
                return finish(PackingStatus::internal_failure,
                              "collision report contains an invalid container-violation object ID");
            }
            reduce[static_cast<std::size_t>(object)] = true;
        }
        for (const ObjectCollisionPair& pair : result.collision.object_collisions) {
            if (pair.first >= result.transforms.size() || pair.second >= result.transforms.size() ||
                pair.first == pair.second) {
                return finish(PackingStatus::internal_failure, "collision report contains an invalid object pair");
            }
            reduce[static_cast<std::size_t>(pair.first)] = true;
            reduce[static_cast<std::size_t>(pair.second)] = true;
        }

        // COMPATIBILITY(IROP-COMPAT-0002): CAT contacts remain
        // diagnostic and are deliberately excluded from the
        // scale-correction selection.
        if (std::none_of(reduce.begin(), reduce.end(), [](const bool selected) {
            return selected;
        })) {
            return finish(PackingStatus::internal_failure,
                          "physically invalid collision report selected no object for correction");
        }

        std::vector<Transform> corrected = result.transforms;
        for (std::size_t object = 0; object < corrected.size(); ++object) {
            if (!reduce[object]) {
                continue;
            }
            const double reduced = corrected[object].volume_scale * correction_volume_scale_factor;
            if (!std::isfinite(reduced) || reduced <= 0.0) {
                return finish(PackingStatus::numerical_failure,
                              "collision correction produced an invalid volume scale");
            }
            corrected[object].volume_scale = reduced;
        }
        result.transforms = std::move(corrected);
        increment(result.correction_passes);
        increment(cumulative_correction_passes);

        if (const std::optional<PackingStatus> status = validate_candidate(); status.has_value()) {
            return finish(*status);
        }
    }

    return finish(PackingStatus::success);
}

double barrier_volume_scale_multiplier_bound(const double current_volume_scale, const double target_volume_scale)
{
    if (!std::isfinite(current_volume_scale) || current_volume_scale <= 0.0 || !std::isfinite(target_volume_scale) ||
        target_volume_scale <= 0.0 || current_volume_scale > target_volume_scale) {
        throw Error(ErrorCategory::invalid_configuration,
                    "packing barrier multiplier inputs are outside their supported domain");
    }

    const double largest_finite_bound = std::nextafter(maximum_local_solve_bound_magnitude_exclusive, 0.0);
    const double target_multiplier = target_volume_scale / current_volume_scale;
    const double slack_factor = 1.0 + local_solve_barrier_relative_slack;
    if (!std::isfinite(target_multiplier) || target_multiplier >= largest_finite_bound / slack_factor) {
        return largest_finite_bound;
    }

    double result = target_multiplier * slack_factor;
    if (!(result > target_multiplier)) {
        result = std::nextafter(target_multiplier, largest_finite_bound);
    }
    return std::min(result, largest_finite_bound);
}

}  // namespace detail

void validate_packing_algorithm_config(const double initial_scale, const PackingAlgorithmConfig& config,
                                       const PackingEngineLimits& limits)
{
    if (config.diagnostics.max_local_solve_records > 4'096 || config.diagnostics.max_trace_records_per_solve > 4'096 ||
        config.diagnostics.max_failed_snapshot_constraints > 100'000 || config.diagnostics.max_recovery_records > 256 ||
        (config.diagnostics.max_trace_records_per_solve > 0 &&
         config.diagnostics.max_local_solve_records > 8'192 / config.diagnostics.max_trace_records_per_solve)) {
        throw Error(ErrorCategory::invalid_configuration,
                    "packing diagnostic collection limits exceed their hard ceilings");
    }

    if (!std::isfinite(initial_scale) || initial_scale <= 0.0 || initial_scale > 1.0 ||
        !std::isfinite(config.final_volume_scale) || config.final_volume_scale < initial_scale ||
        config.final_volume_scale > 1.0 || config.scale_step_count == 0 ||
        config.scale_step_count > maximum_exact_binary64_integer || config.max_iterations_per_scale_step == 0) {
        throw Error(ErrorCategory::invalid_configuration, "packing scale schedule is outside its supported domain");
    }
    if (!std::isfinite(config.maximum_rotation_delta_radians) || config.maximum_rotation_delta_radians < 0.0 ||
        config.maximum_rotation_delta_radians > maximum_local_solve_rotation_delta_radians ||
        !std::isfinite(config.padding) || config.padding < 0.0 ||
        !std::isfinite(config.correction_volume_scale_factor) || config.correction_volume_scale_factor <= 0.0 ||
        config.correction_volume_scale_factor >= 1.0 ||
        !std::isfinite(config.tetrahedralization_recovery_scale_factor) ||
        config.tetrahedralization_recovery_scale_factor <= 0.0 ||
        config.tetrahedralization_recovery_scale_factor >= 1.0 || !std::isfinite(config.local_solve_tolerance) ||
        config.local_solve_tolerance <= 0.0 || config.local_solve_tolerance > maximum_local_solve_tolerance) {
        throw Error(ErrorCategory::invalid_configuration, "packing algorithm parameters are outside their domains");
    }
    const auto mesh_limits_are_positive = [](const MeshLimits& value) {
        return value.max_input_bytes > 0 && value.max_vertices > 0 && value.max_triangles > 0;
    };
    if (limits.max_correction_passes_per_iteration == 0 || limits.max_history_records == 0 ||
        limits.max_total_local_solves == 0 || limits.max_elapsed_time <= std::chrono::milliseconds::zero() ||
        !mesh_limits_are_positive(limits.intermediate_mesh_limits) ||
        !mesh_limits_are_positive(limits.resampling.mesh_limits) || limits.resampling.max_subdivision_steps == 0 ||
        limits.tetrahedralization.max_participants == 0 || limits.tetrahedralization.max_input_points == 0 ||
        limits.tetrahedralization.max_input_triangles == 0 || limits.tetrahedralization.max_output_points == 0 ||
        limits.tetrahedralization.max_output_tetrahedra == 0 || limits.cat.max_participants == 0 ||
        limits.cat.max_points == 0 || limits.cat.max_tetrahedra == 0 || limits.cat.max_relevant_tetrahedra == 0 ||
        limits.cat.max_polygons == 0 || limits.cat.max_polygon_vertices == 0 || limits.cat.max_constraints == 0 ||
        limits.local_solve.max_constraints == 0 || limits.local_solve.max_dense_jacobian_entries == 0 ||
        limits.local_solve.max_constraint_rows_evaluated == 0 ||
        limits.local_solve.max_jacobian_entries_evaluated == 0 || limits.local_solve.max_iterations == 0 ||
        limits.local_solve.max_elapsed_time <= std::chrono::milliseconds::zero() ||
        limits.collision.max_triangle_pair_tests == 0 || limits.collision.max_containment_triangle_visits == 0 ||
        limits.collision.max_reported_violations == 0 || limits.collision.max_object_pair_checks == 0) {
        throw Error(ErrorCategory::invalid_configuration, "packing engine limits must be positive");
    }

    static_cast<void>(surface_sampling_ratio(initial_scale, config.sampling));
    static_cast<void>(surface_sampling_ratio(config.final_volume_scale, config.sampling));

    if (config.maximum_translation_per_unit_scale.has_value()) {
        const double maximum_translation = *config.maximum_translation_per_unit_scale;
        if (!std::isfinite(maximum_translation) || maximum_translation <= 0.0) {
            throw Error(ErrorCategory::invalid_configuration,
                        "maximum translation per unit scale must be finite and positive");
        }
    }
}

const char* to_string(const PackingStatus status) noexcept
{
    switch (status) {
    case PackingStatus::success:
        return "success";
    case PackingStatus::cancelled:
        return "cancelled";
    case PackingStatus::invalid_input:
        return "invalid_input";
    case PackingStatus::resource_exhausted:
        return "resource_exhausted";
    case PackingStatus::infeasible:
        return "infeasible";
    case PackingStatus::iteration_limit:
        return "iteration_limit";
    case PackingStatus::correction_limit:
        return "correction_limit";
    case PackingStatus::time_limit:
        return "time_limit";
    case PackingStatus::numerical_failure:
        return "numerical_failure";
    case PackingStatus::dependency_failure:
        return "dependency_failure";
    case PackingStatus::internal_failure:
        return "internal_failure";
    }
    return "internal_failure";
}

const char* to_string(const PackingProgressPhase phase) noexcept
{
    switch (phase) {
    case PackingProgressPhase::input_preparation:
        return "input_preparation";
    case PackingProgressPhase::initialization_started:
        return "initialization_started";
    case PackingProgressPhase::scale_step_started:
        return "scale_step_started";
    case PackingProgressPhase::tetrahedralization_recovery:
        return "tetrahedralization_recovery";
    case PackingProgressPhase::local_solve_started:
        return "local_solve_started";
    case PackingProgressPhase::iteration_completed:
        return "iteration_completed";
    case PackingProgressPhase::finished:
        return "finished";
    }
    return "finished";
}

PackingResult run_packing(const TriangleMesh& centered_object, const TriangleMesh& container, PackingState state,
                          const PackingAlgorithmConfig& config, const PackingEngineLimits& limits,
                          const PackingCallbacks& callbacks)
{
    const Clock::time_point start_time = Clock::now();
    PackingResult result(std::move(state));
    PackingProgress progress;
    progress.scale_step_count = config.scale_step_count;
    progress.object_count = static_cast<std::uint64_t>(result.state.transforms.size());
    progress.engine_time_limit = limits.max_elapsed_time;
    progress.local_iteration_limit = limits.local_solve.max_iterations;
    progress.local_time_limit = limits.local_solve.max_elapsed_time;

    auto update_elapsed = [&]() noexcept {
        result.work.elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time);
    };
    auto finish = [&](const PackingStatus status, std::string diagnostic) -> PackingResult {
        result.status = status;
        result.diagnostic = std::move(diagnostic);
        update_elapsed();
        progress.phase = PackingProgressPhase::finished;
        progress.object_id.reset();
        progress.objects_at_target = object_count_at_target(result.state.transforms, progress.target_volume_scale);
        if (callbacks.progress) {
            callbacks.progress(progress);
        }
        return std::move(result);
    };

    try {
        result.resolved_maximum_translation_per_unit_scale = validate_and_resolve(result.state, config, limits);
        validate_initial_state(centered_object, container, result.state, callbacks.cancellation_requested);

        auto stopped = [&]() -> std::optional<PackingStatus> {
            if (callbacks.cancellation_requested && callbacks.cancellation_requested()) {
                return PackingStatus::cancelled;
            }
            if (Clock::now() - start_time >= limits.max_elapsed_time) {
                return PackingStatus::time_limit;
            }
            return std::nullopt;
        };
        auto stop_result = [&](const PackingStatus status) -> PackingResult {
            // DEVIATION(IROP-DEV-0018): Cancellation and elapsed-time
            // exhaustion are explicit unsuccessful outcomes instead of the
            // Python loop's ambiguous return. See docs/COMPATIBILITY.md.
            return finish(status, status == PackingStatus::cancelled ? "packing was cancelled"
                                                                     : "packing elapsed-time limit was exhausted");
        };
        auto emit_progress = [&]() {
            if (callbacks.progress) {
                callbacks.progress(progress);
            }
        };

        if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
            return stop_result(*status);
        }

        const std::uint64_t object_count = static_cast<std::uint64_t>(result.state.transforms.size());
        const double initial_scale = result.state.config.initial_volume_scale;
        const double scale_increment =
            (config.final_volume_scale - initial_scale) / static_cast<double>(config.scale_step_count);
        std::vector<std::uint64_t> last_cat_violations;

        for (std::uint64_t scale_step = 0; scale_step < config.scale_step_count; ++scale_step) {
            const double target_scale = scale_step + 1 == config.scale_step_count
                                            ? config.final_volume_scale
                                            : initial_scale + scale_increment * static_cast<double>(scale_step + 1);
            if (!std::isfinite(target_scale) || target_scale <= 0.0) {
                return finish(PackingStatus::numerical_failure, "scale barrier construction produced an invalid value");
            }

            progress.phase = PackingProgressPhase::scale_step_started;
            progress.object_id.reset();
            progress.scale_step = scale_step;
            progress.iteration = 0;
            progress.target_volume_scale = target_scale;
            progress.objects_at_target = object_count_at_target(result.state.transforms, target_scale);
            emit_progress();
            if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                return stop_result(*status);
            }
            // DEVIATION(IROP-DEV-0024): Python tests barrier completion only
            // after mutation-prone optimization work. An already-complete
            // barrier bypasses that work and proceeds to mandatory final
            // validation. See docs/COMPATIBILITY.md.
            if (progress.objects_at_target == object_count) {
                increment(result.work.completed_scale_steps);
                continue;
            }

            TriangleMesh sampled_object = centered_object;
            TriangleMesh sampled_container = container;
            if (config.adaptive_sampling) {
                const SurfaceResamplingLimits resampling_limits = effective_resampling_limits(limits);
                const std::uint64_t object_target = target_surface_triangle_count(
                    static_cast<std::uint64_t>(centered_object.triangles.size()), target_scale, config.sampling);
                SurfaceResamplingResult object_result = measure_stage(result.work.stage_timings.resampling, [&] {
                    return resample_closed_surface(centered_object, object_target, resampling_limits);
                });
                increment(result.work.resampling_operations);
                sampled_object = std::move(object_result.mesh);
                if (object_result.actual_triangle_count != sampled_object.triangles.size()) {
                    return finish(PackingStatus::internal_failure,
                                  "object resampling returned inconsistent triangle metadata");
                }
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }

                const std::uint64_t container_target = target_container_triangle_count(
                    sampled_object, container, 4, config.sampling.minimum_triangle_count);
                SurfaceResamplingResult container_result = measure_stage(result.work.stage_timings.resampling, [&] {
                    return resample_closed_surface(container, container_target, resampling_limits);
                });
                increment(result.work.resampling_operations);
                sampled_container = std::move(container_result.mesh);
                if (container_result.actual_triangle_count != sampled_container.triangles.size()) {
                    return finish(PackingStatus::internal_failure,
                                  "container resampling returned inconsistent triangle metadata");
                }
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }
            }

            require_scene_mesh_budget(sampled_object, object_count, limits.intermediate_mesh_limits, "sampled packing");
            bool scale_step_completed = false;
            for (std::uint64_t iteration = 0; iteration < config.max_iterations_per_scale_step; ++iteration) {
                increment(result.work.iterations);
                progress.iteration = iteration;
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }

                std::vector<TriangleMesh> participants = measure_stage(result.work.stage_timings.transform, [&] {
                    return instantiate(sampled_object, result.state.transforms, limits.intermediate_mesh_limits,
                                       "sampled packing");
                });
                participants.push_back(sampled_container);
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }

                increment(result.work.tetrahedralization_attempts);
                TetrahedralizationResult tetrahedralization =
                    measure_stage(result.work.stage_timings.tetrahedralization, [&] {
                    return tetrahedralize_surfaces(participants, limits.tetrahedralization);
                });
                accumulate(result.work.tetrahedralization, tetrahedralization.work);
                std::optional<std::size_t> recovery_record_index;
                if (!tetrahedralization.succeeded()) {
                    try {
                        if (result.diagnostics.recovery_records.size() < config.diagnostics.max_recovery_records) {
                            result.diagnostics.recovery_records.push_back(
                                { scale_step, iteration, target_scale, tetrahedralization.status,
                                  bounded_reason(tetrahedralization.diagnostic), false });
                            recovery_record_index = result.diagnostics.recovery_records.size() - 1;
                        }
                        else {
                            increment(result.diagnostics.recovery_records_dropped);
                        }
                    }
                    catch (...) {
                        increment(result.diagnostics.recovery_records_dropped);
                    }
                }
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }
                if (!tetrahedralization.succeeded()) {
                    if (tetrahedralization.status == TetrahedralizationStatus::dependency_failure &&
                        iteration + 1 < config.max_iterations_per_scale_step) {
                        // DEVIATION(IROP-DEV-0001): Iterate over valid object
                        // transforms on the reference recovery path instead of
                        // iterating over the integer object count.
                        PackingState recovered =
                            recovered_state(result.state, config.tetrahedralization_recovery_scale_factor);
                        result.state = std::move(recovered);
                        increment(result.work.tetrahedralization_recoveries);
                        if (recovery_record_index.has_value()) {
                            result.diagnostics.recovery_records[*recovery_record_index].recovery_applied = true;
                        }
                        progress.phase = PackingProgressPhase::tetrahedralization_recovery;
                        progress.object_id.reset();
                        progress.objects_at_target = object_count_at_target(result.state.transforms, target_scale);
                        emit_progress();
                        progress.phase = PackingProgressPhase::scale_step_started;
                        if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                            return stop_result(*status);
                        }
                        continue;
                    }
                    return finish(status_for(tetrahedralization.status),
                                  nested_diagnostic("tetrahedralization", to_string(tetrahedralization.status),
                                                    tetrahedralization.diagnostic));
                }

                increment(result.work.cat_builds);
                CatConstructionResult cat = measure_stage(result.work.stage_timings.cat, [&] {
                    return build_cat(tetrahedralization.mesh, limits.cat);
                });
                accumulate(result.work.cat, cat.work);
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }
                if (!cat.succeeded()) {
                    return finish(status_for(cat.status),
                                  nested_diagnostic("CAT construction", to_string(cat.status), cat.diagnostic));
                }
                const std::vector<TriangleMesh> cat_surfaces = measure_stage(result.work.stage_timings.cat, [&] {
                    return make_cat_surfaces(cat, object_count, limits.intermediate_mesh_limits);
                });
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }

                // DEVIATION(IROP-DEV-0017): Python launches an un-awaited
                // shared-state ThreadPoolExecutor batch. C++ solves in stable
                // object order into a transaction-local state and commits the
                // complete batch only after every result is accepted.
                PackingState candidate_state = result.state;
                std::vector<Transform> accepted_transforms;
                accepted_transforms.reserve(candidate_state.transforms.size());
                LocalSolveWorkspace workspace;
                const double effective_rotation_bound =
                    reference_rotation_bound_factor * config.maximum_rotation_delta_radians;
                // COMPATIBILITY(IROP-COMPAT-0007): The reference multiplies
                // the coordinate-unit translation setting by the current
                // volume-scale barrier, not by its cube root.
                const double effective_translation_bound =
                    result.resolved_maximum_translation_per_unit_scale * target_scale;
                if (!std::isfinite(effective_translation_bound) || effective_translation_bound <= 0.0) {
                    return finish(PackingStatus::invalid_input,
                                  "barrier-scaled maximum translation is outside its supported domain");
                }

                for (std::uint64_t object = 0; object < object_count; ++object) {
                    if (result.work.local_solves >= limits.max_total_local_solves) {
                        return finish(PackingStatus::resource_exhausted,
                                      "packing total local-solve limit was exhausted");
                    }
                    if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                        return stop_result(*status);
                    }

                    LocalTransformStep initial_guess;
                    initial_guess.volume_scale_multiplier = initial_scale;
                    // DEVIATION(IROP-DEV-0006): Continue the run-owned
                    // NumPy-compatible stream instead of process-global
                    // random state. The Python call consumes three draws even
                    // when its configured interval has zero width.
                    const auto draw_rotation = [&]() {
                        if (effective_rotation_bound == 0.0) {
                            static_cast<void>(candidate_state.random_state.uniform(0.0, 1.0));
                            return 0.0;
                        }
                        return candidate_state.random_state.uniform(-effective_rotation_bound,
                                                                    effective_rotation_bound);
                    };
                    initial_guess.rotation_delta = { draw_rotation(), draw_rotation(), draw_rotation() };

                    LocalSolveRequest request;
                    request.participant = static_cast<ParticipantId>(object);
                    request.current_transform = result.state.transforms[static_cast<std::size_t>(object)];
                    request.initial_guess = initial_guess;
                    request.bounds.minimum_volume_scale_multiplier = initial_scale;
                    // DEVIATION(IROP-DEV-0023): Stop the local objective near
                    // the active barrier instead of optimizing irrelevant
                    // growth and clamping it only after the solve. Fixed,
                    // representable slack lets Ipopt cross the exact barrier;
                    // extreme ratios saturate below the adapter's finite-bound
                    // sentinel and advance over bounded packing iterations.
                    request.bounds.maximum_volume_scale_multiplier = detail::barrier_volume_scale_multiplier_bound(
                        request.current_transform.volume_scale, target_scale);
                    request.bounds.maximum_absolute_rotation_delta_radians = effective_rotation_bound;
                    request.bounds.maximum_absolute_translation = effective_translation_bound;
                    request.padding = config.padding;
                    request.maximum_result_volume_scale = target_scale;
                    request.tolerance = config.local_solve_tolerance;

                    progress.phase = PackingProgressPhase::local_solve_started;
                    progress.object_id = object;
                    emit_progress();
                    if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                        return stop_result(*status);
                    }
                    const LocalSolveDiagnosticsOptions local_diagnostics {
                        .max_trace_records = config.diagnostics.max_trace_records_per_solve,
                        .max_failed_snapshot_constraints = config.diagnostics.capture_failed_local_problem
                                                               ? config.diagnostics.max_failed_snapshot_constraints
                                                               : 0
                    };
                    increment(result.work.local_solves);
                    LocalSolveResult local = measure_stage(result.work.stage_timings.local_solve, [&] {
                        return solve_local_transform(tetrahedralization.mesh, cat, request, workspace,
                                                     limits.local_solve, local_diagnostics);
                    });
                    accumulate(result.work.local_solve, local.work);
                    if (!local.succeeded() || config.diagnostics.max_local_solve_records > 0) {
                        try {
                            PackingLocalSolveRecord record { object,
                                                             scale_step,
                                                             iteration,
                                                             target_scale,
                                                             local.status,
                                                             limits.local_solve,
                                                             local.work,
                                                             bounded_reason(local.diagnostic),
                                                             std::move(local.trace),
                                                             local.trace_records_dropped };
                            if (!local.succeeded()) {
                                result.diagnostics.failure = record;
                                result.diagnostics.failed_local_problem = std::move(local.failed_snapshot);
                                result.diagnostics.failed_snapshot_omitted = local.snapshot_omitted;
                            }
                            if (config.diagnostics.max_local_solve_records > 0) {
                                if (result.diagnostics.local_solve_records.size() <
                                    config.diagnostics.max_local_solve_records) {
                                    result.diagnostics.local_solve_records.push_back(std::move(record));
                                }
                                else {
                                    increment(result.diagnostics.local_solve_records_dropped);
                                }
                            }
                        }
                        catch (...) {
                            increment(result.diagnostics.local_solve_records_dropped);
                            result.diagnostics.failed_snapshot_omitted =
                                config.diagnostics.capture_failed_local_problem;
                        }
                    }
                    if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                        return stop_result(*status);
                    }
                    if (!local.succeeded()) {
                        return finish(
                            status_for(local.status),
                            "object " + std::to_string(object + 1) + " (id " + std::to_string(object) +
                                "), scale step " + std::to_string(scale_step + 1) + "/" +
                                std::to_string(config.scale_step_count) + ", engine iteration " +
                                std::to_string(iteration + 1) + ", barrier " + std::to_string(target_scale) + ": " +
                                nested_diagnostic("local solve", to_string(local.status),
                                                  bounded_reason(local.diagnostic)) +
                                " [local limits: " + std::to_string(limits.local_solve.max_iterations) +
                                " iterations, " + std::to_string(limits.local_solve.max_elapsed_time.count()) +
                                " ms; engine limit: " + std::to_string(limits.max_elapsed_time.count()) + " ms]");
                    }
                    if (!local.accepted_transform.has_value()) {
                        return finish(PackingStatus::internal_failure,
                                      "successful local solve did not publish an accepted transform");
                    }
                    accepted_transforms.push_back(*local.accepted_transform);
                }
                candidate_state.transforms = std::move(accepted_transforms);

                // DEVIATION(IROP-DEV-0019): Sampled surfaces remain an
                // optimization input, but physical collision correction is
                // decided from the full-resolution object and container.
                detail::PhysicalCollisionCorrectionResult correction =
                    measure_stage(result.work.stage_timings.correction, [&] {
                    return detail::correct_physical_collisions(
                        centered_object, container, cat_surfaces, std::move(candidate_state.transforms),
                        config.correction_volume_scale_factor, limits.max_correction_passes_per_iteration,
                        result.state.config.output_mesh_limits, limits.collision, result.work.collision,
                        result.work.correction_passes, stopped);
                });
                if (!correction.succeeded()) {
                    if (correction.status == PackingStatus::cancelled ||
                        correction.status == PackingStatus::time_limit) {
                        return stop_result(correction.status);
                    }
                    return finish(correction.status, std::move(correction.diagnostic));
                }
                candidate_state.transforms = std::move(correction.transforms);
                SceneCollisionReport collision = std::move(correction.collision);
                const std::uint64_t correction_passes = correction.correction_passes;

                if (result.history.size() >= limits.max_history_records) {
                    return finish(PackingStatus::resource_exhausted, "packing history-record limit was exhausted");
                }
                const std::uint64_t objects_at_target =
                    object_count_at_target(candidate_state.transforms, target_scale);
                // DEVIATION(IROP-DEV-0002): History is owned and indexed by
                // this result; no class-level records can leak across runs.
                result.history.push_back({
                    .scale_step = scale_step,
                    .iteration = iteration,
                    .target_volume_scale = target_scale,
                    .objects_at_target = objects_at_target,
                    .sampled_object_triangles = static_cast<std::uint64_t>(sampled_object.triangles.size()),
                    .sampled_container_triangles = static_cast<std::uint64_t>(sampled_container.triangles.size()),
                    .cat_violations = static_cast<std::uint64_t>(collision.cat_violation_object_ids.size()),
                    .container_violations = static_cast<std::uint64_t>(collision.container_violation_object_ids.size()),
                    .object_collisions = static_cast<std::uint64_t>(collision.object_collisions.size()),
                    .correction_passes = correction_passes,
                });
                last_cat_violations = collision.cat_violation_object_ids;
                result.state = std::move(candidate_state);

                progress.phase = PackingProgressPhase::iteration_completed;
                progress.object_id.reset();
                progress.objects_at_target = objects_at_target;
                emit_progress();
                if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
                    return stop_result(*status);
                }
                if (objects_at_target == object_count) {
                    increment(result.work.completed_scale_steps);
                    scale_step_completed = true;
                    break;
                }
                progress.phase = PackingProgressPhase::scale_step_started;
            }

            if (!scale_step_completed) {
                // DEVIATION(IROP-DEV-0018): Do not silently advance to a
                // larger barrier after exhausting this barrier's iterations.
                return finish(PackingStatus::iteration_limit,
                              "packing iteration limit was exhausted before the current scale barrier");
            }
        }

        if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
            return stop_result(*status);
        }

        const std::vector<TriangleMesh> final_objects = measure_stage(result.work.stage_timings.transform, [&] {
            return instantiate(centered_object, result.state.transforms, result.state.config.output_mesh_limits,
                               "full-resolution output");
        });
        if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
            return stop_result(*status);
        }
        SceneCollisionReport final_validation = measure_stage(result.work.stage_timings.final_validation, [&] {
            return validate_scene_collisions(final_objects, container, {},
                                             remaining_collision_limits(result.work.collision, limits.collision));
        });
        accumulate(result.work.collision, final_validation.work);
        result.final_validation = std::move(final_validation);
        result.final_validation_performed = true;
        if (const std::optional<PackingStatus> status = stopped(); status.has_value()) {
            return stop_result(*status);
        }
        result.final_cat_violation_object_ids = std::move(last_cat_violations);
        if (!result.final_cat_violation_object_ids.empty()) {
            result.warnings.emplace_back(
                "CAT contacts remain diagnostic compatibility data and were excluded from scale correction");
        }
        if (!result.final_validation.physical_scene_valid()) {
            // DEVIATION(IROP-DEV-0019): Full-resolution containment and
            // object-overlap validation gates success rather than trusting
            // only the sampled Python collision pass.
            return finish(PackingStatus::infeasible,
                          "full-resolution packing validation found a physical collision or containment violation");
        }
        if (object_count_at_target(result.state.transforms, config.final_volume_scale) != object_count) {
            return finish(PackingStatus::iteration_limit,
                          "packing ended without every object at the final volume-scale barrier");
        }
        return finish(PackingStatus::success, "packing completed successfully");
    }
    catch (const Error& error) {
        result.status = status_for(error.category());
        result.diagnostic = error.what();
    }
    catch (const std::bad_alloc&) {
        result.status = PackingStatus::resource_exhausted;
        result.diagnostic = "packing memory allocation failed";
    }
    catch (const std::length_error&) {
        result.status = PackingStatus::resource_exhausted;
        result.diagnostic = "packing collection size is not representable";
    }
    catch (const std::exception& error) {
        result.status = PackingStatus::internal_failure;
        result.diagnostic = error.what();
    }
    catch (...) {
        result.status = PackingStatus::internal_failure;
        result.diagnostic = "packing failed with an unknown exception";
    }
    update_elapsed();
    return result;
}

}  // namespace irop
