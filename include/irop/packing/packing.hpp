#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
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
};

struct PackingEngineLimits {
    static constexpr std::uint64_t default_max_correction_passes_per_iteration = 100;
    static constexpr std::uint64_t default_max_history_records = 10'000;
    static constexpr std::uint64_t default_max_total_local_solves = 1'000'000;
    static constexpr std::uint64_t default_max_local_solve_iterations = 1'000;
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
    SceneCollisionLimits collision;
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
    scale_step_started,
    tetrahedralization_recovery,
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

struct PackingWork {
    std::uint64_t completed_scale_steps = 0;
    std::uint64_t iterations = 0;
    std::uint64_t tetrahedralization_attempts = 0;
    std::uint64_t tetrahedralization_recoveries = 0;
    std::uint64_t cat_builds = 0;
    std::uint64_t local_solves = 0;
    std::uint64_t correction_passes = 0;
    std::uint64_t resampling_operations = 0;
    TetrahedralizationWork tetrahedralization;
    CatConstructionWork cat;
    LocalSolveWork local_solve;
    SceneCollisionWork collision;
    std::chrono::milliseconds elapsed_time {};
};

struct PackingResult {
    explicit PackingResult(PackingState initial_state) : state(std::move(initial_state)) {}

    PackingStatus status = PackingStatus::invalid_input;
    PackingState state;
    PackingWork work;
    std::vector<PackingIterationRecord> history;
    SceneCollisionReport final_validation;
    bool final_validation_performed = false;
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

// Internal numeric policy exposed for focused tests; not a stable API.
[[nodiscard]] double barrier_volume_scale_multiplier_bound(double current_volume_scale, double target_volume_scale);

}  // namespace detail

// The state is passed by value so an unsuccessful run can return its exact
// diagnostic state without mutating a caller-owned object.
[[nodiscard]] PackingResult run_packing(const TriangleMesh& centered_object, const TriangleMesh& container,
                                        PackingState state, const PackingAlgorithmConfig& config = {},
                                        const PackingEngineLimits& limits = {}, const PackingCallbacks& callbacks = {});

}  // namespace irop
