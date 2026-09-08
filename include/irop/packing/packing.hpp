#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "irop/cat/cat.hpp"
#include "irop/geometry/collision.hpp"
#include "irop/geometry/sampling_policy.hpp"
#include "irop/geometry/surface_resampling.hpp"
#include "irop/optimization/local_solver.hpp"
#include "irop/packing/initialization.hpp"
#include "irop/tetrahedralization/tetrahedralization.hpp"

namespace irop {

struct PackingDiagnosticsOptions {
    bool capture_failed_local_problem = false;
    std::uint64_t max_local_solve_records = 0;
    std::uint64_t max_trace_records_per_solve = 0;
    std::uint64_t max_failed_snapshot_constraints = 100'000;
    std::uint64_t max_recovery_records = 32;
};

struct PackingAlgorithmConfig {
    double final_volume_scale = 1.0;
    std::uint64_t scale_step_count = 9;
    std::uint64_t max_iterations_per_scale_step = 200;
    double maximum_rotation_delta_radians = 3.14159265358979323846 / 12.0;
    std::optional<double> maximum_translation_per_unit_scale;
    double padding = 0.0;
    double correction_volume_scale_factor = 0.93;
    double tetrahedralization_recovery_scale_factor = 0.99;
    double local_solve_tolerance = maximum_local_solve_tolerance;
    SurfaceSamplingPolicy sampling;
    bool adaptive_sampling = true;
    // Retain the tranche-1 random-start, slack-bound and sampling policy for reproducibility.
    bool use_reference_growth_policy = false;
    PackingDiagnosticsOptions diagnostics;
    // DEVIATION(IROP-DEV-0036): Avoid parallel algebra overhead for small local problems.
    // Zero preserves inherited dependency threading, independently of growth policy.
    std::uint32_t solver_openmp_threads = 1;
    // Effective only for default growth and immediate full-surface physical retries.
    bool reuse_physical_retry_results = true;
};

struct PackingEngineLimits {
    static constexpr std::uint64_t default_max_correction_passes_per_iteration = 100;
    static constexpr std::uint64_t default_max_history_records = 10'000;
    static constexpr std::uint64_t default_max_total_local_solves = 1'000'000;
    static constexpr std::uint64_t default_max_local_solve_iterations = 1'000;
    static constexpr std::uint64_t default_max_collision_triangle_pair_tests = 1'000'000'000ULL;
    static constexpr std::chrono::milliseconds default_max_elapsed_time { 300'000 };

    std::uint64_t max_correction_passes_per_iteration = default_max_correction_passes_per_iteration;
    std::uint64_t max_history_records = default_max_history_records;
    std::uint64_t max_total_local_solves = default_max_total_local_solves;
    std::chrono::milliseconds max_elapsed_time = default_max_elapsed_time;
    MeshLimits intermediate_mesh_limits;
    SurfaceResamplingLimits resampling;
    TetrahedralizationLimits tetrahedralization;
    CatConstructionLimits cat;
    // Representative CAT problems can require more work than the standalone
    // solver's conservative diagnostic default. Packing retains its own
    // explicitly bounded per-object limit.
    LocalSolveLimits local_solve { .max_iterations = default_max_local_solve_iterations };
    // Full-input physical checks across scale barriers need a cumulative engine
    // allowance distinct from a standalone scene query; the 300-second cap remains.
    SceneCollisionLimits collision { .max_triangle_pair_tests = default_max_collision_triangle_pair_tests };
};

enum class PackingStatus {
    success,
    cancelled,
    invalid_input,
    resource_exhausted,
    infeasible,
    iteration_limit,
    correction_limit,
    time_limit,
    numerical_failure,
    dependency_failure,
    internal_failure,
};

[[nodiscard]] const char* to_string(PackingStatus status) noexcept;

enum class PackingProgressPhase {
    input_preparation,
    initialization_started,
    scale_step_started,
    tetrahedralization_recovery,
    sampling_recovery,
    step_recovery,
    local_solve_started,
    iteration_completed,
    finished,
};

[[nodiscard]] const char* to_string(PackingProgressPhase phase) noexcept;

struct PackingProgress {
    PackingProgressPhase phase = PackingProgressPhase::scale_step_started;
    std::uint64_t scale_step = 0;
    std::uint64_t scale_step_count = 0;
    std::uint64_t iteration = 0;
    double target_volume_scale = 0.0;
    std::uint64_t objects_at_target = 0;
    std::uint64_t object_count = 0;
    std::optional<std::uint64_t> object_id;
    std::optional<std::uint64_t> initialization_attempt_limit;
    std::uint64_t local_iteration_limit = 0;
    std::chrono::milliseconds local_time_limit {};
    std::chrono::milliseconds engine_time_limit {};
};

struct PackingCallbacks {
    std::function<bool()> cancellation_requested;
    std::function<void(const PackingProgress&)> progress;
};

struct PackingIterationRecord {
    std::uint64_t scale_step = 0;
    std::uint64_t iteration = 0;
    double target_volume_scale = 0.0;
    std::uint64_t objects_at_target = 0;
    std::uint64_t sampled_object_triangles = 0;
    std::uint64_t sampled_container_triangles = 0;
    std::uint64_t cat_violations = 0;
    std::uint64_t container_violations = 0;
    std::uint64_t object_collisions = 0;
    std::uint64_t correction_passes = 0;
};

struct PackingStageTimings {
    std::chrono::microseconds resampling {};
    std::chrono::microseconds transform {};
    std::chrono::microseconds tetrahedralization {};
    std::chrono::microseconds cat {};
    std::chrono::microseconds local_solve {};
    std::chrono::microseconds correction {};
    std::chrono::microseconds final_validation {};
};

struct PackingLocalSolveRecord {
    std::uint64_t object_id = 0;
    std::uint64_t scale_step = 0;
    std::uint64_t iteration = 0;
    double target_volume_scale = 0.0;
    LocalSolveStatus status = LocalSolveStatus::invalid_input;
    LocalSolveLimits limits;
    LocalSolveWork work;
    std::string reason;
    std::vector<LocalSolveTraceRecord> trace;
    std::uint64_t trace_records_dropped = 0;
};

struct PackingRecoveryRecord {
    std::uint64_t scale_step = 0;
    std::uint64_t iteration = 0;
    double target_volume_scale = 0.0;
    TetrahedralizationStatus status = TetrahedralizationStatus::invalid_input;
    std::string reason;
    bool recovery_applied = false;
};

struct PackingDiagnostics {
    std::optional<PackingLocalSolveRecord> failure;
    std::optional<LocalSolveSnapshot> failed_local_problem;
    bool failed_snapshot_omitted = false;
    std::vector<PackingLocalSolveRecord> local_solve_records;
    std::uint64_t local_solve_records_dropped = 0;
    std::vector<PackingRecoveryRecord> recovery_records;
    std::uint64_t recovery_records_dropped = 0;
};

struct PackingWork {
    std::uint64_t completed_scale_steps = 0;
    std::uint64_t iterations = 0;
    std::uint64_t tetrahedralization_attempts = 0;
    std::uint64_t tetrahedralization_recoveries = 0;
    std::uint64_t cat_builds = 0;
    std::uint64_t local_solves = 0;
    std::uint64_t correction_passes = 0;
    std::uint64_t resampling_operations = 0;
    std::uint64_t sampling_refinements = 0;
    std::uint64_t physical_step_retries = 0;
    // Logical retry requests that reuse a previously successful local transform.
    std::uint64_t reused_local_solves = 0;
    // Immediate retry iterations that retain the unchanged TetGen/CAT context.
    std::uint64_t reused_prepared_batches = 0;
    TetrahedralizationWork tetrahedralization;
    CatConstructionWork cat;
    LocalSolveWork local_solve;
    SceneCollisionWork collision;
    std::chrono::milliseconds elapsed_time {};
    PackingStageTimings stage_timings;
};

struct PackingResult {
    explicit PackingResult(PackingState initial_state) : state(std::move(initial_state)) {}

    PackingStatus status = PackingStatus::invalid_input;
    PackingState state;
    PackingWork work;
    PackingDiagnostics diagnostics;
    std::vector<PackingIterationRecord> history;
    SceneCollisionReport final_validation;
    bool final_validation_performed = false;
    bool cat_diagnostics_complete = true;
    std::vector<std::uint64_t> final_cat_violation_object_ids;
    std::vector<std::string> warnings;
    std::string diagnostic;
    double resolved_maximum_translation_per_unit_scale = 0.0;

    [[nodiscard]] bool succeeded() const noexcept { return status == PackingStatus::success; }
};

// Validates algorithm and engine configuration that is independent of mesh
// data. `initial_volume_scale` is the initialization schedule's starting
// volume scale.
void validate_packing_algorithm_config(double initial_volume_scale, const PackingAlgorithmConfig& config,
                                       const PackingEngineLimits& limits);

namespace detail {

struct PhysicalCollisionCorrectionResult {
    PackingStatus status = PackingStatus::success;
    std::vector<Transform> transforms;
    SceneCollisionReport collision;
    std::uint64_t correction_passes = 0;
    std::string diagnostic;

    [[nodiscard]] bool succeeded() const noexcept { return status == PackingStatus::success; }
};

// Internal correction seam exposed for deterministic orchestration tests; not
// a stable API. Work references are updated after every completed validation or
// correction pass so an interrupted or failed caller retains partial evidence.
[[nodiscard]] PhysicalCollisionCorrectionResult correct_physical_collisions(
    const TriangleMesh& centered_object, const TriangleMesh& container, std::span<const TriangleMesh> cat_surfaces,
    std::vector<Transform> candidate_transforms, double correction_volume_scale_factor,
    std::uint64_t maximum_correction_passes, const MeshLimits& output_mesh_limits,
    const SceneCollisionLimits& collision_limits, SceneCollisionWork& cumulative_collision_work,
    std::uint64_t& cumulative_correction_passes, const std::function<std::optional<PackingStatus>()>& stopped = {});

// Internal numeric policy exposed for focused tests; not a stable API.
[[nodiscard]] double barrier_volume_scale_multiplier_bound(double current_volume_scale, double target_volume_scale);

}  // namespace detail

// The state is passed by value so an unsuccessful run can return its exact
// diagnostic state without mutating a caller-owned object.
[[nodiscard]] PackingResult run_packing(const TriangleMesh& centered_object, const TriangleMesh& container,
                                        PackingState state, const PackingAlgorithmConfig& config = {},
                                        const PackingEngineLimits& limits = {}, const PackingCallbacks& callbacks = {});

}  // namespace irop
