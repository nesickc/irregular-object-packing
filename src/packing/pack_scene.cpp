#include "irop/packing/pack_scene.hpp"

#include <vtkVersion.h>

#include <Eigen/Core>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/local_solve_replay.hpp"
#include "irop/io/stl_io.hpp"
#include "irop/model/mesh_validation.hpp"
#include "irop/packing/initialization.hpp"

#ifndef IROP_VERSION
#define IROP_VERSION "development"
#endif

namespace irop {
namespace {

constexpr std::size_t maximum_summary_warnings = 256;
constexpr std::size_t maximum_warning_bytes = 1024;
constexpr std::size_t maximum_diagnostic_bytes = 2048;

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

[[nodiscard]] std::filesystem::path resolve_input_path(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::canonical(path, error);
    if (error) {
        throw Error(ErrorCategory::input_io, "input STL does not exist or cannot be resolved");
    }
    return resolved;
}

class OutputDirectoryTransaction final {
public:
    explicit OutputDirectoryTransaction(const std::filesystem::path& requested_output)
    {
        if (requested_output.empty()) {
            throw Error(ErrorCategory::output_io, "packing output directory must not be empty");
        }

        std::error_code error;
        final_directory_ = std::filesystem::absolute(requested_output, error).lexically_normal();
        if (error || final_directory_.filename().empty()) {
            throw Error(ErrorCategory::output_io, "failed to resolve a named packing output directory");
        }

        std::filesystem::path parent = final_directory_.parent_path();
        if (parent.empty()) {
            throw Error(ErrorCategory::output_io, "packing output directory must have a resolvable parent");
        }
        if (!std::filesystem::exists(parent, error)) {
            error.clear();
            static_cast<void>(std::filesystem::create_directories(parent, error));
        }
        parent = std::filesystem::canonical(parent, error);
        if (error || !std::filesystem::is_directory(parent, error)) {
            throw Error(ErrorCategory::output_io, "failed to prepare the packing output parent directory");
        }

        final_directory_ = parent / final_directory_.filename();
        const std::filesystem::file_status final_status = std::filesystem::symlink_status(final_directory_, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw Error(ErrorCategory::output_io, "failed to inspect the packing output path");
        }
        if (!error && final_status.type() != std::filesystem::file_type::not_found) {
            throw Error(ErrorCategory::output_io,
                        "packing output directory already exists; choose a new artifact-set path");
        }

        std::random_device random;
        for (std::size_t attempt = 0; attempt < 64; ++attempt) {
            std::ostringstream name;
            name << ".irop-pack-staging-" << std::hex << std::setfill('0') << std::setw(8) << random() << std::setw(8)
                 << random() << std::setw(8) << random() << std::setw(8) << random();
            staging_directory_ = parent / name.str();
            error.clear();
            if (std::filesystem::create_directory(staging_directory_, error)) {
                return;
            }
            if (error) {
                throw Error(ErrorCategory::output_io, "failed to create a private packing staging directory");
            }
        }
        throw Error(ErrorCategory::output_io, "failed to reserve a unique packing staging directory");
    }

    OutputDirectoryTransaction(const OutputDirectoryTransaction&) = delete;
    OutputDirectoryTransaction& operator=(const OutputDirectoryTransaction&) = delete;

    ~OutputDirectoryTransaction() noexcept
    {
        if (committed_ || staging_directory_.empty()) {
            return;
        }
        try {
            std::error_code ignored;
            static_cast<void>(std::filesystem::remove_all(staging_directory_, ignored));
        }
        catch (...) {
            // Cleanup must not replace the original packing/publication error.
        }
    }

    [[nodiscard]] const std::filesystem::path& final_directory() const noexcept { return final_directory_; }
    [[nodiscard]] const std::filesystem::path& staging_directory() const noexcept { return staging_directory_; }

    void commit()
    {
        std::error_code error;
        std::filesystem::rename(staging_directory_, final_directory_, error);
        if (error) {
            throw Error(ErrorCategory::output_io, "failed to atomically publish the packing artifact set");
        }
        committed_ = true;
    }

private:
    std::filesystem::path final_directory_;
    std::filesystem::path staging_directory_;
    bool committed_ = false;
};

void remove_file_noexcept(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(path, ignored));
}

[[nodiscard]] bool cancellation_requested(const PackingCallbacks& callbacks)
{
    if (!callbacks.cancellation_requested) {
        return false;
    }
    try {
        return callbacks.cancellation_requested();
    }
    catch (...) {
        throw Error(ErrorCategory::internal, "packing cancellation callback failed");
    }
}

void discard_staged_success_artifacts(const std::filesystem::path& staging)
{
    for (const char* leaf : { "packed-objects.stl", "container.stl", "placements.json", "run-summary.json" }) {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(staging / leaf, error));
        if (error) {
            throw Error(ErrorCategory::output_io, "failed to discard a staged success artifact");
        }
    }
    std::error_code objects_error;
    static_cast<void>(std::filesystem::remove_all(staging / "objects", objects_error));
    if (objects_error) {
        throw Error(ErrorCategory::output_io, "failed to discard staged individual-object artifacts");
    }
}

[[nodiscard]] std::optional<SceneCollisionLimits> remaining_output_collision_limits(
    const SceneCollisionWork& consumed, const SceneCollisionLimits& configured) noexcept
{
    if (consumed.object_pairs_examined >= configured.max_object_pair_checks ||
        consumed.triangle_pairs_tested >= configured.max_triangle_pair_tests ||
        consumed.containment_triangle_visits >= configured.max_containment_triangle_visits) {
        return std::nullopt;
    }
    return SceneCollisionLimits {
        .max_triangle_pair_tests = configured.max_triangle_pair_tests - consumed.triangle_pairs_tested,
        .max_containment_triangle_visits =
            configured.max_containment_triangle_visits - consumed.containment_triangle_visits,
        .max_reported_violations = configured.max_reported_violations,
        .max_object_pair_checks = configured.max_object_pair_checks - consumed.object_pairs_examined,
        .max_cat_triangle_pair_tests =
            configured.max_cat_triangle_pair_tests -
            std::min(configured.max_cat_triangle_pair_tests, consumed.cat_triangle_pairs_tested),
    };
}

void add_collision_work(SceneCollisionWork& destination, const SceneCollisionWork& source)
{
    const auto add = [](std::uint64_t& value, const std::uint64_t amount) {
        if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
            throw Error(ErrorCategory::resource_limit, "packing collision work counter overflowed");
        }
        value += amount;
    };
    add(destination.object_pairs_examined, source.object_pairs_examined);
    add(destination.triangle_pairs_tested, source.triangle_pairs_tested);
    add(destination.containment_triangle_visits, source.containment_triangle_visits);
    add(destination.cat_triangle_pairs_tested, source.cat_triangle_pairs_tested);
}

void write_new_text_file(const std::filesystem::path& path, const std::string& contents)
{
#ifdef _WIN32
    HANDLE output =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw Error(ErrorCategory::output_io, "failed to reserve a new packing JSON artifact path");
    }

    bool succeeded = true;
    std::size_t written = 0;
    while (written < contents.size()) {
        const DWORD requested =
            static_cast<DWORD>(std::min<std::size_t>(contents.size() - written, static_cast<std::size_t>(MAXDWORD)));
        DWORD chunk_written = 0;
        if (WriteFile(output, contents.data() + written, requested, &chunk_written, nullptr) == 0 ||
            chunk_written != requested) {
            succeeded = false;
            break;
        }
        written += chunk_written;
    }
    if (succeeded && FlushFileBuffers(output) == 0) {
        succeeded = false;
    }
    if (CloseHandle(output) == 0) {
        succeeded = false;
    }
    if (!succeeded) {
        remove_file_noexcept(path);
        throw Error(ErrorCategory::output_io, "failed while writing a packing JSON artifact");
    }
#else
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw Error(ErrorCategory::output_io, "failed to open a packing JSON artifact for writing");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        remove_file_noexcept(path);
        throw Error(ErrorCategory::output_io, "failed while writing a packing JSON artifact");
    }
#endif
}

[[nodiscard]] std::string bounded_text(const std::string_view text, const std::size_t maximum_bytes)
{
    return std::string(text.substr(0, std::min(text.size(), maximum_bytes)));
}

[[nodiscard]] nlohmann::json finite_number_or_null(const double value)
{
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

[[nodiscard]] nlohmann::json optional_number(const std::optional<double>& value)
{
    return value.has_value() ? finite_number_or_null(*value) : nlohmann::json(nullptr);
}

[[nodiscard]] nlohmann::json point_json(const Point3& point)
{
    return nlohmann::json::array({ point.x, point.y, point.z });
}

[[nodiscard]] nlohmann::json bounds_json(const MeshBounds& bounds)
{
    return {
        { "minimum", point_json(bounds.minimum) },
        { "maximum", point_json(bounds.maximum) },
    };
}

[[nodiscard]] nlohmann::json matrix_json(const Matrix4& matrix)
{
    nlohmann::json rows = nlohmann::json::array();
    for (std::size_t row = 0; row < 4; ++row) {
        nlohmann::json values = nlohmann::json::array();
        for (std::size_t column = 0; column < 4; ++column) {
            values.push_back(matrix(row, column));
        }
        rows.push_back(std::move(values));
    }
    return rows;
}

[[nodiscard]] std::filesystem::path individual_object_path(const std::filesystem::path& directory,
                                                           const std::size_t index)
{
    std::ostringstream filename;
    filename << "object-" << std::setfill('0') << std::setw(6) << index << ".stl";
    return directory / filename.str();
}

[[nodiscard]] nlohmann::json placements_json(const PackSceneResult& result)
{
    nlohmann::json placements = nlohmann::json::array();
    for (std::size_t index = 0; index < result.packing.state.transforms.size(); ++index) {
        const Transform& transform = result.packing.state.transforms[index];
        const Transform centering {
            .translation = { -result.source_object_centroid.x, -result.source_object_centroid.y,
                            -result.source_object_centroid.z },
        };
        placements.push_back({
            { "object_id", index },
            { "object_identity", "object-template" },
            { "volume_scale", transform.volume_scale },
            { "rotation_radians",
             nlohmann::json::array({ transform.rotation.x, transform.rotation.y, transform.rotation.z }) },
            { "translation", point_json(transform.translation) },
            { "input_to_world_matrix", matrix_json(compose(matrix_for(transform), matrix_for(centering))) },
        });
    }

    return {
        { "schema_version",       1                      },
        { "command",              "pack"                 },
        { "phase",                "final"                },
        { "coordinate_units",     "arbitrary_consistent" },
        { "transform_convention",
         {
              { "vector_convention", "column" },
              { "rotation_units", "radians" },
              { "rotation_order", "Ry*Rz*Rx" },
              { "scale_kind", "volume" },
          }                                              },
        { "object_template",
         {
              { "identity", "object-template" },
              { "source_vertex_centroid", point_json(result.source_object_centroid) },
              { "centered_at_origin", true },
          }                                              },
        { "placements",           std::move(placements)  },
    };
}

[[nodiscard]] std::string eigen_version()
{
    return std::to_string(EIGEN_WORLD_VERSION) + "." + std::to_string(EIGEN_MAJOR_VERSION) + "." +
           std::to_string(EIGEN_MINOR_VERSION);
}

[[nodiscard]] nlohmann::json count_limits_json(const MeshLimits& limits)
{
    return {
        { "max_vertices",  limits.max_vertices  },
        { "max_triangles", limits.max_triangles },
    };
}

[[nodiscard]] nlohmann::json input_limits_json(const MeshLimits& limits)
{
    return {
        { "max_input_bytes", limits.max_input_bytes },
        { "max_vertices",    limits.max_vertices    },
        { "max_triangles",   limits.max_triangles   },
    };
}

[[nodiscard]] nlohmann::json algorithm_config_json(const PackOptions& options, const PackSceneResult& result)
{
    const PackingConfig& initialization = result.packing.state.config;
    const PackingAlgorithmConfig& algorithm = options.algorithm;
    const PackingEngineLimits& limits = options.limits;
    return {
        { "object_count",                                initialization.object_count                                   },
        { "initial_volume_scale",                        initialization.initial_volume_scale                           },
        { "final_volume_scale",                          algorithm.final_volume_scale                                  },
        { "scale_step_count",                            algorithm.scale_step_count                                    },
        { "max_iterations_per_scale_step",               algorithm.max_iterations_per_scale_step                       },
        { "maximum_rotation_delta_radians",              algorithm.maximum_rotation_delta_radians                      },
        { "maximum_translation_per_unit_scale",          optional_number(algorithm.maximum_translation_per_unit_scale) },
        { "resolved_maximum_translation_per_unit_scale",
         finite_number_or_null(result.packing.resolved_maximum_translation_per_unit_scale)                             },
        { "padding",                                     algorithm.padding                                             },
        { "correction_volume_scale_factor",              algorithm.correction_volume_scale_factor                      },
        { "tetrahedralization_recovery_scale_factor",    algorithm.tetrahedralization_recovery_scale_factor            },
        { "local_solve_tolerance",                       algorithm.local_solve_tolerance                               },
        { "adaptive_sampling",                           algorithm.adaptive_sampling                                   },
        { "use_reference_growth_policy",                 algorithm.use_reference_growth_policy                         },
        { "solver_openmp_threads",                       algorithm.solver_openmp_threads                               },
        { "reuse_physical_retry_results",                algorithm.reuse_physical_retry_results                        },
        { "diagnostics",
         { { "capture_failed_local_problem", algorithm.diagnostics.capture_failed_local_problem },
            { "max_local_solve_records", algorithm.diagnostics.max_local_solve_records },
            { "max_trace_records_per_solve", algorithm.diagnostics.max_trace_records_per_solve },
            { "max_failed_snapshot_constraints", algorithm.diagnostics.max_failed_snapshot_constraints },
            { "max_recovery_records", algorithm.diagnostics.max_recovery_records } }                                   },
        { "sampling",
         {
              { "alpha", algorithm.sampling.alpha },
              { "beta", algorithm.sampling.beta },
              { "minimum_triangle_count", algorithm.sampling.minimum_triangle_count },
          }                                                                                                            },
        { "seed",                                        initialization.seed                                           },
        { "initialization_limits",
         {
              { "max_sampling_attempts", initialization.max_sampling_attempts },
              { "enable_structured_fallback", initialization.enable_structured_fallback },
              { "max_structured_candidates", initialization.max_structured_candidates },
              { "max_geometry_query_triangle_visits", initialization.max_geometry_query_triangle_visits },
              { "max_pairwise_distance_checks", initialization.max_pairwise_distance_checks },
              { "max_surface_intersection_triangle_pairs", initialization.max_surface_intersection_triangle_pairs },
          }                                                                                                            },
        { "input_limits",                                input_limits_json(options.input_limits)                       },
        { "output_limits",                               count_limits_json(initialization.output_mesh_limits)          },
        { "engine_limits",
         {
              { "max_correction_passes_per_iteration", limits.max_correction_passes_per_iteration },
              { "max_history_records", limits.max_history_records },
              { "max_total_local_solves", limits.max_total_local_solves },
              { "max_elapsed_milliseconds", limits.max_elapsed_time.count() },
              { "intermediate_mesh", count_limits_json(limits.intermediate_mesh_limits) },
              { "resampling",
                {
                    { "max_subdivision_steps", limits.resampling.max_subdivision_steps },
                    { "mesh", count_limits_json(limits.resampling.mesh_limits) },
                } },
              { "tetrahedralization",
                {
                    { "max_participants", limits.tetrahedralization.max_participants },
                    { "max_input_points", limits.tetrahedralization.max_input_points },
                    { "max_input_triangles", limits.tetrahedralization.max_input_triangles },
                    { "max_output_points", limits.tetrahedralization.max_output_points },
                    { "max_output_tetrahedra", limits.tetrahedralization.max_output_tetrahedra },
                } },
              { "cat",
                {
                    { "max_participants", limits.cat.max_participants },
                    { "max_points", limits.cat.max_points },
                    { "max_tetrahedra", limits.cat.max_tetrahedra },
                    { "max_relevant_tetrahedra", limits.cat.max_relevant_tetrahedra },
                    { "max_polygons", limits.cat.max_polygons },
                    { "max_polygon_vertices", limits.cat.max_polygon_vertices },
                    { "max_constraints", limits.cat.max_constraints },
                } },
              { "local_solve",
                {
                    { "max_constraints", limits.local_solve.max_constraints },
                    { "max_dense_jacobian_entries", limits.local_solve.max_dense_jacobian_entries },
                    { "max_constraint_rows_evaluated", limits.local_solve.max_constraint_rows_evaluated },
                    { "max_jacobian_entries_evaluated", limits.local_solve.max_jacobian_entries_evaluated },
                    { "max_iterations", limits.local_solve.max_iterations },
                    { "max_elapsed_milliseconds", limits.local_solve.max_elapsed_time.count() },
                } },
              { "collision",
                {
                    { "max_triangle_pair_tests", limits.collision.max_triangle_pair_tests },
                    { "max_cat_triangle_pair_tests", limits.collision.max_cat_triangle_pair_tests },
                    { "max_containment_triangle_visits", limits.collision.max_containment_triangle_visits },
                    { "max_reported_violations", limits.collision.max_reported_violations },
                    { "max_object_pair_checks", limits.collision.max_object_pair_checks },
                } },
          }                                                                                                            },
    };
}

[[nodiscard]] nlohmann::json tetrahedralization_work_json(const TetrahedralizationWork& work)
{
    return {
        { "input_participants",                    work.input_participants                    },
        { "input_points",                          work.input_points                          },
        { "input_triangles",                       work.input_triangles                       },
        { "output_points",                         work.output_points                         },
        { "output_tetrahedra",                     work.output_tetrahedra                     },
        { "omitted_single_participant_tetrahedra", work.omitted_single_participant_tetrahedra },
    };
}

[[nodiscard]] nlohmann::json cat_work_json(const CatConstructionWork& work)
{
    return {
        { "points_examined",                       work.points_examined                       },
        { "tetrahedra_examined",                   work.tetrahedra_examined                   },
        { "relevant_tetrahedra",                   work.relevant_tetrahedra                   },
        { "skipped_single_participant_tetrahedra", work.skipped_single_participant_tetrahedra },
        { "polygons_generated",                    work.polygons_generated                    },
        { "polygon_vertices_generated",            work.polygon_vertices_generated            },
        { "constraints_generated",                 work.constraints_generated                 },
    };
}

[[nodiscard]] nlohmann::json local_solve_threading_json(const std::optional<irop::LocalSolveThreading>& threading)
{
    if (!threading.has_value()) {
        return nullptr;
    }
    const auto& value = *threading;
    return {
        { "requested_openmp_threads",       value.requested_openmp_threads                                                           },
        { "before_openmp_threads",          value.before_openmp_threads                                                              },
        { "scoped_openmp_threads",          value.scoped_openmp_threads                                                              },
        { "mkl_num_threads_present",        value.mkl_num_threads_present                                                            },
        { "mkl_num_threads",                value.mkl_num_threads ? nlohmann::json(*value.mkl_num_threads) : nlohmann::json(nullptr) },
        { "mkl_domain_num_threads_present", value.mkl_domain_num_threads_present                                                     },
        { "mkl_domain_num_threads",
         value.mkl_domain_num_threads ? nlohmann::json(*value.mkl_domain_num_threads) : nlohmann::json(nullptr)                      },
    };
}

[[nodiscard]] nlohmann::json local_solve_timings_json(const LocalSolveTimings& timings)
{
    return {
        { "preparation",         timings.preparation.count()         },
        { "dependency_setup",    timings.dependency_setup.count()    },
        { "dependency_solve",    timings.dependency_solve.count()    },
        { "postcheck",           timings.postcheck.count()           },
        { "constraint_callback", timings.constraint_callback.count() },
        { "jacobian_callback",   timings.jacobian_callback.count()   },
        { "hessian_callback",    timings.hessian_callback.count()    },
    };
}

[[nodiscard]] nlohmann::json local_solve_work_json(const LocalSolveWork& work)
{
    return {
        { "constraints_prepared",              work.constraints_prepared                        },
        { "objective_evaluations",             work.objective_evaluations                       },
        { "objective_gradient_evaluations",    work.objective_gradient_evaluations              },
        { "constraint_rows_evaluated",         work.constraint_rows_evaluated                   },
        { "jacobian_entries_evaluated",        work.jacobian_entries_evaluated                  },
        { "hessian_evaluations",               work.hessian_evaluations                         },
        { "hessian_constraint_rows_evaluated", work.hessian_constraint_rows_evaluated           },
        { "iterations",                        work.iterations                                  },
        { "elapsed_milliseconds",              work.elapsed_time.count()                        },
        { "timings_nanoseconds",               local_solve_timings_json(work.timings)           },
        { "threading",                         local_solve_threading_json(work.threading)       },
        { "threading_mixed",                   work.threading_mixed                             },
        { "minimum_solver_constraint",         optional_number(work.minimum_solver_constraint)  },
        { "minimum_applied_constraint",        optional_number(work.minimum_applied_constraint) },
    };
}

[[nodiscard]] nlohmann::json local_record_json(const PackingLocalSolveRecord& record)
{
    nlohmann::json trace = nlohmann::json::array();
    for (const auto& row : record.trace) {
        trace.push_back({
            { "iteration",            row.iteration                                   },
            { "restoration_phase",    row.restoration_phase                           },
            { "objective",            finite_number_or_null(row.objective)            },
            { "primal_infeasibility", finite_number_or_null(row.primal_infeasibility) },
            { "dual_infeasibility",   finite_number_or_null(row.dual_infeasibility)   },
            { "barrier_parameter",    finite_number_or_null(row.barrier_parameter)    }
        });
    }
    return {
        { "object_id", record.object_id },
        { "scale_step", record.scale_step },
        { "iteration", record.iteration },
        { "target_volume_scale", record.target_volume_scale },
        { "status", to_string(record.status) },
        { "reason", bounded_text(record.reason, maximum_diagnostic_bytes) },
        { "limits",
         { { "max_constraints", record.limits.max_constraints },
            { "max_dense_jacobian_entries", record.limits.max_dense_jacobian_entries },
            { "max_constraint_rows_evaluated", record.limits.max_constraint_rows_evaluated },
            { "max_jacobian_entries_evaluated", record.limits.max_jacobian_entries_evaluated },
            { "max_iterations", record.limits.max_iterations },
            { "max_elapsed_milliseconds", record.limits.max_elapsed_time.count() } } },
        { "work", local_solve_work_json(record.work) },
        { "trace", std::move(trace) },
        { "trace_records_dropped", record.trace_records_dropped }
    };
}

[[nodiscard]] nlohmann::json diagnostics_json(const PackingDiagnostics& diagnostics)
{
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : diagnostics.local_solve_records) {
        records.push_back(local_record_json(record));
    }
    nlohmann::json recoveries = nlohmann::json::array();
    for (const auto& record : diagnostics.recovery_records) {
        recoveries.push_back({
            { "scale_step", record.scale_step },
            { "iteration", record.iteration },
            { "target_volume_scale", record.target_volume_scale },
            { "status", to_string(record.status) },
            { "reason", bounded_text(record.reason, maximum_diagnostic_bytes) },
            { "recovery_applied", record.recovery_applied }
        });
    }
    return {
        { "failure",                     diagnostics.failure ? local_record_json(*diagnostics.failure) : nlohmann::json(nullptr) },
        { "failed_snapshot_omitted",     diagnostics.failed_snapshot_omitted                                                     },
        { "local_solve_records",         std::move(records)                                                                      },
        { "local_solve_records_dropped", diagnostics.local_solve_records_dropped                                                 },
        { "recovery_records",            std::move(recoveries)                                                                   },
        { "recovery_records_dropped",    diagnostics.recovery_records_dropped                                                    }
    };
}

[[nodiscard]] nlohmann::json collision_work_json(const SceneCollisionWork& work)
{
    return {
        { "object_pairs_examined",       work.object_pairs_examined       },
        { "triangle_pairs_tested",       work.triangle_pairs_tested       },
        { "containment_triangle_visits", work.containment_triangle_visits },
        { "cat_triangle_pairs_tested",   work.cat_triangle_pairs_tested   },
    };
}

[[nodiscard]] nlohmann::json packing_work_json(const PackingWork& work)
{
    return {
        { "completed_scale_steps",         work.completed_scale_steps                            },
        { "iterations",                    work.iterations                                       },
        { "tetrahedralization_attempts",   work.tetrahedralization_attempts                      },
        { "tetrahedralization_recoveries", work.tetrahedralization_recoveries                    },
        { "sampling_refinements",          work.sampling_refinements                             },
        { "physical_step_retries",         work.physical_step_retries                            },
        { "reused_local_solves",           work.reused_local_solves                              },
        { "reused_prepared_batches",       work.reused_prepared_batches                          },
        { "cat_builds",                    work.cat_builds                                       },
        { "local_solves",                  work.local_solves                                     },
        { "correction_passes",             work.correction_passes                                },
        { "resampling_operations",         work.resampling_operations                            },
        { "tetrahedralization",            tetrahedralization_work_json(work.tetrahedralization) },
        { "cat",                           cat_work_json(work.cat)                               },
        { "local_solve",                   local_solve_work_json(work.local_solve)               },
        { "collision",                     collision_work_json(work.collision)                   },
        { "elapsed_milliseconds",          work.elapsed_time.count()                             },
        { "stage_microseconds",
         { { "resampling", work.stage_timings.resampling.count() },
            { "transform", work.stage_timings.transform.count() },
            { "tetrahedralization", work.stage_timings.tetrahedralization.count() },
            { "cat", work.stage_timings.cat.count() },
            { "local_solve", work.stage_timings.local_solve.count() },
            { "correction", work.stage_timings.correction.count() },
            { "final_validation", work.stage_timings.final_validation.count() } }                },
    };
}

[[nodiscard]] nlohmann::json history_json(const std::vector<PackingIterationRecord>& history)
{
    nlohmann::json records = nlohmann::json::array();
    for (const PackingIterationRecord& record : history) {
        records.push_back({
            { "scale_step",                  record.scale_step                  },
            { "iteration",                   record.iteration                   },
            { "target_volume_scale",         record.target_volume_scale         },
            { "objects_at_target",           record.objects_at_target           },
            { "sampled_object_triangles",    record.sampled_object_triangles    },
            { "sampled_container_triangles", record.sampled_container_triangles },
            { "cat_violations",              record.cat_violations              },
            { "container_violations",        record.container_violations        },
            { "object_collisions",           record.object_collisions           },
            { "correction_passes",           record.correction_passes           },
        });
    }
    return records;
}

[[nodiscard]] nlohmann::json validation_json(const PackingResult& packing)
{
    const SceneCollisionReport& validation = packing.final_validation;
    const bool scene_is_valid = validation.physical_scene_valid();
    const char* status = !packing.final_validation_performed ? "not_run" : (scene_is_valid ? "passed" : "failed");
    nlohmann::json physical_scene_valid = nullptr;
    if (packing.final_validation_performed) {
        physical_scene_valid = scene_is_valid;
    }

    nlohmann::json collisions = nlohmann::json::array();
    for (const ObjectCollisionPair& collision : validation.object_collisions) {
        collisions.push_back({
            { "first",  collision.first  },
            { "second", collision.second }
        });
    }
    return {
        { "status",                         status                                    },
        { "physical_scene_valid",           std::move(physical_scene_valid)           },
        { "cat_diagnostics_complete",       packing.cat_diagnostics_complete          },
        { "cat_violation_object_ids",       packing.final_cat_violation_object_ids    },
        { "container_violation_object_ids", validation.container_violation_object_ids },
        { "object_collisions",              std::move(collisions)                     },
        { "work",                           collision_work_json(validation.work)      },
    };
}

[[nodiscard]] nlohmann::json metrics_json(const PackingResult& packing, const PackingAlgorithmConfig& algorithm)
{
    const std::vector<Transform>& transforms = packing.state.transforms;
    if (transforms.empty()) {
        return {
            { "object_count",            0                            },
            { "objects_at_final_target", 0                            },
            { "target_volume_scale",     algorithm.final_volume_scale },
            { "minimum_volume_scale",    nullptr                      },
            { "maximum_volume_scale",    nullptr                      },
            { "mean_volume_scale",       nullptr                      },
            { "packed_object_volume",    nullptr                      },
            { "packing_fraction",        nullptr                      },
        };
    }

    double minimum_scale = std::numeric_limits<double>::infinity();
    double maximum_scale = 0.0;
    double scale_sum = 0.0;
    std::uint64_t objects_at_target = 0;
    for (const Transform& transform : transforms) {
        minimum_scale = std::min(minimum_scale, transform.volume_scale);
        maximum_scale = std::max(maximum_scale, transform.volume_scale);
        scale_sum += transform.volume_scale;
        if (transform.volume_scale >= algorithm.final_volume_scale) {
            ++objects_at_target;
        }
    }
    const double packed_volume = packing.state.object_volume * scale_sum;
    const double packing_fraction = packed_volume / packing.state.container_volume;
    return {
        { "object_count",            transforms.size()                                                         },
        { "objects_at_final_target", objects_at_target                                                         },
        { "target_volume_scale",     algorithm.final_volume_scale                                              },
        { "minimum_volume_scale",    finite_number_or_null(minimum_scale)                                      },
        { "maximum_volume_scale",    finite_number_or_null(maximum_scale)                                      },
        { "mean_volume_scale",       finite_number_or_null(scale_sum / static_cast<double>(transforms.size())) },
        { "packed_object_volume",    finite_number_or_null(packed_volume)                                      },
        { "packing_fraction",        finite_number_or_null(packing_fraction)                                   },
    };
}

[[nodiscard]] nlohmann::json warnings_json(const PackSceneResult& result, const LoadedStl& object,
                                           const LoadedStl& container)
{
    std::vector<std::string> warnings;
    warnings.reserve(std::min(maximum_summary_warnings,
                              object.warnings.size() + container.warnings.size() + result.packing.warnings.size()));
    bool truncated = false;
    const auto append = [&](const std::string_view prefix, const std::string_view warning) {
        if (warnings.size() + 1 >= maximum_summary_warnings) {
            truncated = true;
            return;
        }
        std::string text(prefix);
        text.append(warning);
        warnings.push_back(bounded_text(text, maximum_warning_bytes));
    };
    for (const std::string& warning : object.warnings) {
        append("object: ", warning);
    }
    for (const std::string& warning : container.warnings) {
        append("container: ", warning);
    }
    for (const std::string& warning : result.packing.warnings) {
        append("packing: ", warning);
    }
    if (truncated) {
        warnings.emplace_back("additional warnings were omitted from the bounded run summary");
    }
    return warnings;
}

[[nodiscard]] nlohmann::json outputs_json(const PackSceneResult& result)
{
    nlohmann::json individual_paths = nlohmann::json::array();
    for (std::size_t index = 0; index < result.individual_object_paths.size(); ++index) {
        individual_paths.push_back(path_as_utf8(individual_object_path("objects", index)));
    }
    return {
        { "packed_objects_stl",
         result.packed_objects_path.has_value() ? nlohmann::json("packed-objects.stl") : nlohmann::json(nullptr) },
        { "container_stl",
         result.container_output_path.has_value() ? nlohmann::json("container.stl") : nlohmann::json(nullptr)    },
        { "placements_json",
         result.placements_path.has_value() ? nlohmann::json("placements.json") : nlohmann::json(nullptr)        },
        { "failed_local_solve_json",
         result.failed_local_solve_path ? nlohmann::json("failed-local-solve.json") : nlohmann::json(nullptr)    },
        { "run_summary_json",        "run-summary.json"                                                          },
        { "individual_object_stls",  std::move(individual_paths)                                                 },
    };
}

[[nodiscard]] nlohmann::json run_summary_json(const PackSceneResult& result, const LoadedStl& object,
                                              const LoadedStl& container, const PackOptions& options,
                                              const double initialization_seconds, const double packing_seconds,
                                              const double artifact_preparation_seconds)
{
    return {
        { "schema_version", 1 },
        { "command", "pack" },
        { "outcome",
         {
              { "category", to_string(result.packing.status) },
              { "diagnostic", bounded_text(result.packing.diagnostic, maximum_diagnostic_bytes) },
          } },
        { "inputs",
         {
              { "object",
                {
                    { "resolved_path", path_as_utf8(result.resolved_object_path) },
                    { "stl_encoding", to_string(object.encoding) },
                    { "size_bytes", object.input_bytes },
                    { "vertex_count", result.centered_object_statistics.vertex_count },
                    { "triangle_count", result.centered_object_statistics.triangle_count },
                    { "source_vertex_centroid", point_json(result.source_object_centroid) },
                    { "volume", result.packing.state.object_volume },
                    { "maximum_radius",
                      result.packing.state.object_bounding_radius / result.packing.state.initial_linear_scale },
                } },
              { "container",
                {
                    { "resolved_path", path_as_utf8(result.resolved_container_path) },
                    { "stl_encoding", to_string(container.encoding) },
                    { "size_bytes", container.input_bytes },
                    { "vertex_count", result.container_statistics.vertex_count },
                    { "triangle_count", result.container_statistics.triangle_count },
                    { "volume", result.packing.state.container_volume },
                    { "bounds", bounds_json(result.container_statistics.bounds) },
                } },
          } },
        { "config", algorithm_config_json(options, result) },
        { "initialization",
         {
              { "policy", result.packing.state.initialization_method == InitializationMethod::structured_grid
                              ? "structured-aabb-grid"
                              : "uniform-aabb-rejection-bounding-sphere" },
              { "method", to_string(result.packing.state.initialization_method) },
              { "sampling_attempts", result.packing.state.sampling_attempts },
              { "structured_candidates", result.packing.state.structured_candidates },
              { "orientations_examined", result.packing.state.orientations_examined },
              { "reference_accepted_count", result.packing.state.reference_accepted_count },
              { "initial_linear_scale", result.packing.state.initial_linear_scale },
              { "minimum_boundary_clearance",
                result.packing.state.initialization_method == InitializationMethod::structured_grid
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(result.packing.state.object_bounding_radius) },
              { "minimum_center_distance",
                result.packing.state.initialization_method == InitializationMethod::structured_grid
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(result.packing.state.minimum_center_distance) },
              { "rejected_candidate_count", result.packing.state.rejected_candidate_count },
              { "random_draw_count", result.packing.state.random_state.draw_count() },
              { "random_generator", "numpy-legacy-mt19937-compatible" },
              { "geometry_query_triangle_visits", result.packing.state.geometry_query_triangle_visits },
              { "pairwise_distance_checks", result.packing.state.pairwise_distance_checks },
              { "surface_intersection_triangle_pairs", result.packing.state.surface_intersection_triangle_pairs },
          } },
        { "work", packing_work_json(result.packing.work) },
        { "cat_diagnostics_complete", result.packing.cat_diagnostics_complete },
        { "diagnostics", diagnostics_json(result.packing.diagnostics) },
        { "history", history_json(result.packing.history) },
        { "validation", validation_json(result.packing) },
        { "metrics", metrics_json(result.packing, options.algorithm) },
        { "timings",
         {
              { "initialization_seconds", initialization_seconds },
              { "packing_seconds", packing_seconds },
              { "input_preparation_seconds", result.timings.preparation_seconds },
              { "placement_initialization_seconds", result.timings.initialization_seconds },
              { "output_validation_seconds", result.timings.output_validation_seconds },
              { "export_seconds", result.timings.export_seconds },
              { "artifact_preparation_seconds", artifact_preparation_seconds },
          } },
        { "outputs", outputs_json(result) },
        { "versions",
         {
              { "irop", IROP_VERSION },
              { "vtk", vtkVersion::GetVTKVersion() },
              { "eigen", eigen_version() },
              { "tetgen", "1.6.0" },
              { "ipopt", "3.14.19" },
              { "ipopt_linear_solver", "mumps" },
          } },
        { "warnings", warnings_json(result, object, container) },
    };
}

}  // namespace

PackSceneResult pack_scene(const std::filesystem::path& object_path, const std::filesystem::path& container_path,
                           const std::filesystem::path& output_directory, const PackOptions& options)
{
    validate_packing_config(options.initialization);
    validate_packing_algorithm_config(options.initialization.initial_volume_scale, options.algorithm, options.limits);
    const auto throw_if_preparation_cancelled = [&]() {
        if (cancellation_requested(options.callbacks)) {
            throw Error(ErrorCategory::cancelled, "packing was cancelled during input preparation or initialization");
        }
    };
    throw_if_preparation_cancelled();

    // Check the destination and reserve private staging before expensive input
    // preparation. RAII removes staging on every pre-engine failure; the final
    // directory remains absent until atomic publication.
    const auto application_started = std::chrono::steady_clock::now();
    OutputDirectoryTransaction output_transaction(output_directory);
    const std::filesystem::path& output = output_transaction.final_directory();
    const std::filesystem::path& staging = output_transaction.staging_directory();
    throw_if_preparation_cancelled();

    const auto report_preparation = [&](const PackingProgressPhase phase) {
        if (options.callbacks.progress) {
            options.callbacks.progress(PackingProgress {
                .phase = phase,
                .scale_step_count = options.algorithm.scale_step_count,
                .target_volume_scale = options.algorithm.final_volume_scale,
                .object_count = options.initialization.object_count,
                .initialization_attempt_limit = options.initialization.max_sampling_attempts,
                .local_iteration_limit = options.limits.local_solve.max_iterations,
                .local_time_limit = options.limits.local_solve.max_elapsed_time,
                .engine_time_limit = options.limits.max_elapsed_time,
            });
        }
    };
    report_preparation(PackingProgressPhase::input_preparation);
    const auto initialization_started = std::chrono::steady_clock::now();
    const std::filesystem::path resolved_object = resolve_input_path(object_path);
    throw_if_preparation_cancelled();
    const std::filesystem::path resolved_container = resolve_input_path(container_path);
    throw_if_preparation_cancelled();
    LoadedStl object = read_stl(resolved_object, options.input_limits);
    throw_if_preparation_cancelled();
    LoadedStl container = read_stl(resolved_container, options.input_limits);
    throw_if_preparation_cancelled();
    CenteredMesh centered_object = center_mesh_at_vertex_centroid(object.mesh);
    throw_if_preparation_cancelled();
    const MeshStatistics centered_statistics = validate_and_measure_mesh(centered_object.mesh, options.input_limits);
    throw_if_preparation_cancelled();
    const MeshStatistics container_statistics = validate_and_measure_mesh(container.mesh, options.input_limits);
    throw_if_preparation_cancelled();
    report_preparation(PackingProgressPhase::initialization_started);
    throw_if_preparation_cancelled();
    const auto placement_started = std::chrono::steady_clock::now();
    PackingState state = initialize_packing(centered_object.mesh, container.mesh, options.initialization,
                                            options.callbacks.cancellation_requested);
    throw_if_preparation_cancelled();
    const double initialization_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - initialization_started).count();

    const auto packing_started = std::chrono::steady_clock::now();
    PackingResult packing = run_packing(centered_object.mesh, container.mesh, std::move(state), options.algorithm,
                                        options.limits, options.callbacks);
    const double packing_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - packing_started).count();
    if (packing.status == PackingStatus::invalid_input) {
        const std::string diagnostic = bounded_text(packing.diagnostic, maximum_diagnostic_bytes);
        throw Error(ErrorCategory::invalid_configuration,
                    diagnostic.empty() ? "packing configuration is invalid" : diagnostic);
    }
    if (packing.succeeded() && cancellation_requested(options.callbacks)) {
        packing.status = PackingStatus::cancelled;
        packing.diagnostic = "packing was cancelled before artifact preparation";
    }

    std::optional<std::filesystem::path> packed_objects_path;
    std::optional<std::filesystem::path> container_output_path;
    std::optional<std::filesystem::path> placements_path;
    if (packing.succeeded()) {
        packed_objects_path = output / "packed-objects.stl";
        container_output_path = output / "container.stl";
        placements_path = output / "placements.json";
    }

    PackSceneResult result {
        .packing = std::move(packing),
        .timings = {
            .preparation_seconds = std::chrono::duration<double>(placement_started - initialization_started).count(),
            .initialization_seconds = std::chrono::duration<double>(packing_started - placement_started).count(),
            .packing_seconds = packing_seconds,
        },
        .resolved_object_path = resolved_object,
        .resolved_container_path = resolved_container,
        .packed_objects_path = std::move(packed_objects_path),
        .container_output_path = std::move(container_output_path),
        .placements_path = std::move(placements_path),
        .run_summary_path = output / "run-summary.json",
        .individual_object_paths = {},
        .centered_object_statistics = centered_statistics,
        .container_statistics = container_statistics,
        .source_object_centroid = centered_object.original_vertex_centroid,
    };

    const auto downgrade_success = [&](const PackingStatus status, const std::string_view diagnostic) {
        if (!result.packing.succeeded()) {
            return;
        }
        result.packing.status = status;
        result.packing.diagnostic = std::string(diagnostic);
        result.packed_objects_path.reset();
        result.container_output_path.reset();
        result.placements_path.reset();
        result.individual_object_paths.clear();
        discard_staged_success_artifacts(staging);
    };
    const auto observe_cancellation = [&]() {
        if (!result.packing.succeeded() || !cancellation_requested(options.callbacks)) {
            return false;
        }
        downgrade_success(PackingStatus::cancelled, "packing was cancelled during artifact preparation");
        return true;
    };

    const auto artifact_started = std::chrono::steady_clock::now();
    std::vector<TriangleMesh> objects;
    TriangleMesh combined;
    TriangleMesh output_container;
    if (result.packing.succeeded()) {
        objects = instantiate_objects(centered_object.mesh, result.packing.state);
        static_cast<void>(observe_cancellation());
    }
    if (result.packing.succeeded()) {
        const auto validation_started = std::chrono::steady_clock::now();
        try {
            result.packing.final_validation = {};
            result.packing.final_validation_performed = false;
            for (TriangleMesh& object_mesh : objects) {
                object_mesh = quantize_mesh_for_binary_stl(object_mesh);
                if (observe_cancellation()) {
                    break;
                }
            }
            if (result.packing.succeeded()) {
                output_container = quantize_mesh_for_binary_stl(container.mesh);
                static_cast<void>(observe_cancellation());
            }
            if (result.packing.succeeded()) {
                // DEVIATION(IROP-DEV-0019): Gate success on the exact
                // single-precision vertex representation written to binary
                // STL, not only the in-memory double-precision scene.
                const std::optional<SceneCollisionLimits> remaining =
                    remaining_output_collision_limits(result.packing.work.collision, options.limits.collision);
                if (!remaining.has_value()) {
                    downgrade_success(PackingStatus::resource_exhausted,
                                      "binary-STL output validation exhausted the total collision-work limit");
                }
                else {
                    SceneCollisionReport output_validation =
                        validate_scene_collisions(objects, output_container, {}, *remaining);
                    add_collision_work(result.packing.work.collision, output_validation.work);
                    result.packing.final_validation = std::move(output_validation);
                    result.packing.final_validation_performed = true;
                    if (!result.packing.final_validation.physical_scene_valid()) {
                        downgrade_success(PackingStatus::infeasible,
                                          "binary-STL quantization introduced a collision or containment violation");
                    }
                }
                static_cast<void>(observe_cancellation());
            }
        }
        catch (const Error& error) {
            PackingStatus status = PackingStatus::internal_failure;
            if (error.category() == ErrorCategory::resource_limit) {
                status = PackingStatus::resource_exhausted;
            }
            else if (error.category() == ErrorCategory::invalid_mesh) {
                status = PackingStatus::infeasible;
            }
            else if (error.category() == ErrorCategory::dependency_failure) {
                status = PackingStatus::dependency_failure;
            }
            const std::string diagnostic = std::string("binary-STL output validation failed: ") + error.what();
            downgrade_success(status, diagnostic);
        }
        catch (const std::bad_alloc&) {
            downgrade_success(PackingStatus::resource_exhausted,
                              "binary-STL output validation memory allocation failed");
        }
        catch (const std::exception& error) {
            const std::string diagnostic = std::string("binary-STL output validation failed: ") + error.what();
            downgrade_success(PackingStatus::internal_failure, diagnostic);
        }
        catch (...) {
            downgrade_success(PackingStatus::internal_failure,
                              "binary-STL output validation failed with an unknown exception");
        }
        result.timings.output_validation_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - validation_started).count();
    }
    if (result.packing.succeeded()) {
        combined = combine_meshes(objects, result.packing.state.config.output_mesh_limits);
        static_cast<void>(observe_cancellation());
    }
    if (result.packing.succeeded()) {
        if (options.write_individual_objects) {
            const std::filesystem::path objects_directory = output / "objects";
            result.individual_object_paths.reserve(objects.size());
            for (std::size_t index = 0; index < objects.size(); ++index) {
                result.individual_object_paths.push_back(individual_object_path(objects_directory, index));
            }
        }
    }

    const auto publication_started = std::chrono::steady_clock::now();
    if (result.packing.succeeded()) {
        write_stl(staging / "packed-objects.stl", combined);
        static_cast<void>(observe_cancellation());
    }
    if (result.packing.succeeded()) {
        write_stl(staging / "container.stl", output_container);
        static_cast<void>(observe_cancellation());
    }
    if (result.packing.succeeded()) {
        if (options.write_individual_objects) {
            const std::filesystem::path staging_objects_directory = staging / "objects";
            std::error_code directory_error;
            if (!std::filesystem::create_directory(staging_objects_directory, directory_error) || directory_error) {
                throw Error(ErrorCategory::output_io, "failed to create the staged packed-object directory");
            }
            for (std::size_t index = 0; index < objects.size(); ++index) {
                write_stl(individual_object_path(staging_objects_directory, index), objects[index]);
                if (observe_cancellation()) {
                    break;
                }
            }
        }
    }
    if (result.packing.succeeded()) {
        write_new_text_file(staging / "placements.json", placements_json(result).dump(2) + '\n');
        static_cast<void>(observe_cancellation());
    }
    static_cast<void>(observe_cancellation());
    if (result.packing.diagnostics.failed_local_problem) {
        try {
            write_local_solve_snapshot(staging / "failed-local-solve.json",
                                       *result.packing.diagnostics.failed_local_problem);
            result.failed_local_solve_path = output / "failed-local-solve.json";
        }
        catch (const std::exception&) {
            // Optional diagnostics must not discard the authoritative failure summary.
            remove_file_noexcept(staging / "failed-local-solve.json");
            result.packing.diagnostics.failed_snapshot_omitted = true;
            result.packing.warnings.emplace_back("failed local-solve snapshot could not be written; summary retained");
        }
    }
    result.timings.export_seconds =
        std::max(0.0, std::chrono::duration<double>(std::chrono::steady_clock::now() - artifact_started).count() -
                          result.timings.output_validation_seconds);
    double artifact_preparation_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - publication_started).count();
    write_new_text_file(staging / "run-summary.json",
                        run_summary_json(result, object, container, options, initialization_seconds, packing_seconds,
                                         artifact_preparation_seconds)
                                .dump(2) +
                            '\n');
    if (observe_cancellation()) {
        artifact_preparation_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - publication_started).count();
        write_new_text_file(staging / "run-summary.json",
                            run_summary_json(result, object, container, options, initialization_seconds,
                                             packing_seconds, artifact_preparation_seconds)
                                    .dump(2) +
                                '\n');
    }
    output_transaction.commit();
    result.timings.total_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - application_started).count();
    return result;
}

}  // namespace irop
