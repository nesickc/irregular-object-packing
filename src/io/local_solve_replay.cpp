#include "irop/io/local_solve_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "irop/error.hpp"

namespace irop {
namespace {

using Json = nlohmann::json;
constexpr std::uint64_t maximum_file_bytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_constraints = 100'000;

[[noreturn]] void invalid(const char* reason)
{
    throw Error(ErrorCategory::input_io, std::string("invalid local-solve snapshot: ") + reason);
}

void exact_members(const Json& value, const std::initializer_list<const char*> names,
                   const std::initializer_list<const char*> optional_names = {})
{
    const auto optional_count = std::count_if(optional_names.begin(), optional_names.end(), [&](const char* name) {
        return value.contains(name);
    });
    if (!value.is_object() || value.size() != names.size() + static_cast<std::size_t>(optional_count)) {
        invalid("unexpected or missing object fields");
    }
    for (const char* name : names) {
        if (!value.contains(name)) {
            invalid("missing required field");
        }
    }
}

[[nodiscard]] double number(const Json& value)
{
    if (!value.is_number()) {
        invalid("expected a finite number");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        invalid("expected a finite number");
    }
    return result;
}

[[nodiscard]] std::uint64_t integer(const Json& value)
{
    if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<std::int64_t>() < 0)) {
        invalid("expected a nonnegative integer");
    }
    return value.get<std::uint64_t>();
}

void array_size(const Json& value, const std::size_t count)
{
    if (!value.is_array() || value.size() != count) {
        invalid("incorrect numeric array length");
    }
}

[[nodiscard]] std::array<double, 7> seven_numbers(const Json& value)
{
    array_size(value, 7);
    std::array<double, 7> result {};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = number(value[index]);
    }
    return result;
}

[[nodiscard]] Json optional_number(const std::optional<double>& value)
{
    if (!value) {
        return nullptr;
    }
    if (!std::isfinite(*value)) {
        invalid("non-finite optional bound");
    }
    return *value;
}

[[nodiscard]] Json encode(const LocalSolveSnapshot& snapshot)
{
    if (snapshot.constraints.empty() || snapshot.constraints.size() > maximum_constraints) {
        throw Error(ErrorCategory::resource_limit, "local-solve snapshot constraint count exceeds its bounded domain");
    }
    const auto& request = snapshot.request;
    const auto& current = request.current_transform;
    const auto& initial = request.initial_guess;
    const auto& bounds = request.bounds;
    Json current_json = { current.volume_scale,  current.rotation.x,    current.rotation.y,   current.rotation.z,
                          current.translation.x, current.translation.y, current.translation.z };
    Json initial_json = { initial.volume_scale_multiplier, initial.rotation_delta.x,    initial.rotation_delta.y,
                          initial.rotation_delta.z,        initial.translation_delta.x, initial.translation_delta.y,
                          initial.translation_delta.z };
    static_cast<void>(seven_numbers(current_json));
    static_cast<void>(seven_numbers(initial_json));
    for (const double value : { bounds.minimum_volume_scale_multiplier, bounds.maximum_absolute_rotation_delta_radians,
                                request.padding, request.maximum_result_volume_scale, request.tolerance }) {
        if (!std::isfinite(value)) {
            invalid("non-finite request field");
        }
    }
    Json constraints = Json::array();
    for (const auto& constraint : snapshot.constraints) {
        // This validates finite geometry, unit normals and evaluation inputs
        // using the same project-owned constraint API as solver diagnostics.
        static_cast<void>(evaluate_local_constraint(current.translation, constraint, request.padding, initial));
        constraints.push_back({ constraint.current_vertex.x, constraint.current_vertex.y, constraint.current_vertex.z,
                                constraint.plane_point.x, constraint.plane_point.y, constraint.plane_point.z,
                                constraint.inward_unit_normal.x, constraint.inward_unit_normal.y,
                                constraint.inward_unit_normal.z });
    }
    const auto& limits = snapshot.limits;
    return {
        { "schema_version", 1                      },
        { "kind",           "irop-local-solve"     },
        { "request",
         {
              { "participant", request.participant },
              { "current_transform", std::move(current_json) },
              { "initial_guess", std::move(initial_json) },
              { "bounds",
                { bounds.minimum_volume_scale_multiplier, optional_number(bounds.maximum_volume_scale_multiplier),
                  bounds.maximum_absolute_rotation_delta_radians,
                  optional_number(bounds.maximum_absolute_translation) } },
              { "padding", request.padding },
              { "maximum_result_volume_scale", request.maximum_result_volume_scale },
              { "tolerance", request.tolerance },
              { "use_exact_hessian", request.use_exact_hessian },
          }                                        },
        { "limits",
         {
              { "max_constraints", limits.max_constraints },
              { "max_dense_jacobian_entries", limits.max_dense_jacobian_entries },
              { "max_constraint_rows_evaluated", limits.max_constraint_rows_evaluated },
              { "max_jacobian_entries_evaluated", limits.max_jacobian_entries_evaluated },
              { "max_iterations", limits.max_iterations },
              { "max_elapsed_time_ms", limits.max_elapsed_time.count() },
          }                                        },
        { "constraints",    std::move(constraints) },
    };
}

[[nodiscard]] Json bounded_read(const std::filesystem::path& path, const LocalSolveSnapshotReadLimits& limits)
{
    if (limits.max_file_bytes == 0 || limits.max_file_bytes > maximum_file_bytes || limits.max_constraints == 0 ||
        limits.max_constraints > maximum_constraints) {
        throw Error(ErrorCategory::invalid_configuration,
                    "snapshot read byte/constraint limits exceed their bounded domain");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        invalid("path is not a readable regular file");
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        invalid("could not determine file size");
    }
    if (bytes > limits.max_file_bytes) {
        throw Error(ErrorCategory::resource_limit, "local-solve snapshot file exceeds the byte limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        invalid("file could not be opened");
    }
    std::string contents(static_cast<std::size_t>(bytes), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        invalid("file changed while reading");
    }
    std::uint64_t nodes = 0;
    std::array<std::set<std::string>, 5> keys;
    const std::uint64_t max_nodes = 128 + limits.max_constraints * 10;
    return Json::parse(contents, [&](const int depth, const Json::parse_event_t event, Json& value) {
        if (depth < 0 || depth > 4 || ++nodes > max_nodes * 2) {
            throw Error(ErrorCategory::resource_limit, "local-solve snapshot exceeds JSON depth/node limits");
        }
        auto& current_keys = keys[static_cast<std::size_t>(depth)];
        if (event == Json::parse_event_t::object_start) {
            current_keys.clear();
        }
        else if (event == Json::parse_event_t::key) {
            const auto& key = value.get_ref<const std::string&>();
            if (depth == 0 || key.size() > 64 || !keys[static_cast<std::size_t>(depth - 1)].insert(key).second) {
                invalid("oversized or duplicate field name");
            }
        }
        else if (event == Json::parse_event_t::value && value.is_string() &&
                 value.get_ref<const std::string&>().size() > 64) {
            invalid("oversized string");
        }
        return true;
    });
}

}  // namespace

void write_local_solve_snapshot(const std::filesystem::path& path, const LocalSolveSnapshot& snapshot)
{
    const std::string contents = encode(snapshot).dump();
    if (contents.size() > maximum_file_bytes) {
        throw Error(ErrorCategory::resource_limit, "local-solve snapshot exceeds its serialized byte limit");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw Error(ErrorCategory::output_io, "local-solve snapshot could not be created");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        throw Error(ErrorCategory::output_io, "local-solve snapshot could not be completely written");
    }
}

LocalSolveSnapshot read_local_solve_snapshot(const std::filesystem::path& path,
                                             const LocalSolveSnapshotReadLimits& limits)
{
    try {
        const Json root = bounded_read(path, limits);
        exact_members(root, { "schema_version", "kind", "request", "limits", "constraints" });
        if (integer(root.at("schema_version")) != 1 || root.at("kind") != "irop-local-solve") {
            invalid("unsupported kind or schema version");
        }
        const auto& request_json = root.at("request");
        exact_members(request_json,
                      { "participant", "current_transform", "initial_guess", "bounds", "padding",
                        "maximum_result_volume_scale", "tolerance" },
                      { "use_exact_hessian" });
        LocalSolveSnapshot result;
        auto& request = result.request;
        const auto participant = integer(request_json.at("participant"));
        if (participant > std::numeric_limits<ParticipantId>::max()) {
            invalid("participant is not representable");
        }
        request.participant = static_cast<ParticipantId>(participant);
        const auto current = seven_numbers(request_json.at("current_transform"));
        request.current_transform = {
            current[0], { current[1], current[2], current[3] },
             { current[4], current[5], current[6] }
        };
        const auto initial = seven_numbers(request_json.at("initial_guess"));
        request.initial_guess = {
            initial[0], { initial[1], initial[2], initial[3] },
             { initial[4], initial[5], initial[6] }
        };
        const auto& bounds = request_json.at("bounds");
        array_size(bounds, 4);
        request.bounds.minimum_volume_scale_multiplier = number(bounds[0]);
        if (!bounds[1].is_null()) {
            request.bounds.maximum_volume_scale_multiplier = number(bounds[1]);
        }
        request.bounds.maximum_absolute_rotation_delta_radians = number(bounds[2]);
        if (!bounds[3].is_null()) {
            request.bounds.maximum_absolute_translation = number(bounds[3]);
        }
        request.padding = number(request_json.at("padding"));
        request.maximum_result_volume_scale = number(request_json.at("maximum_result_volume_scale"));
        request.tolerance = number(request_json.at("tolerance"));
        if (request_json.contains("use_exact_hessian")) {
            const auto& exact_hessian = request_json.at("use_exact_hessian");
            if (!exact_hessian.is_boolean()) {
                invalid("use_exact_hessian must be a Boolean");
            }
            request.use_exact_hessian = exact_hessian.get<bool>();
        }
        const auto& saved_limits = root.at("limits");
        exact_members(saved_limits, { "max_constraints", "max_dense_jacobian_entries", "max_constraint_rows_evaluated",
                                      "max_jacobian_entries_evaluated", "max_iterations", "max_elapsed_time_ms" });
        auto& solve = result.limits;
        solve.max_constraints = integer(saved_limits.at("max_constraints"));
        solve.max_dense_jacobian_entries = integer(saved_limits.at("max_dense_jacobian_entries"));
        solve.max_constraint_rows_evaluated = integer(saved_limits.at("max_constraint_rows_evaluated"));
        solve.max_jacobian_entries_evaluated = integer(saved_limits.at("max_jacobian_entries_evaluated"));
        solve.max_iterations = integer(saved_limits.at("max_iterations"));
        const auto milliseconds = integer(saved_limits.at("max_elapsed_time_ms"));
        if (milliseconds == 0 || milliseconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            limits.solve_limits.max_elapsed_time.count() <= 0 ||
            milliseconds > static_cast<std::uint64_t>(limits.solve_limits.max_elapsed_time.count()) ||
            solve.max_iterations == 0 || solve.max_iterations > limits.solve_limits.max_iterations ||
            solve.max_constraints > limits.solve_limits.max_constraints ||
            solve.max_dense_jacobian_entries > limits.solve_limits.max_dense_jacobian_entries ||
            solve.max_constraint_rows_evaluated > limits.solve_limits.max_constraint_rows_evaluated ||
            solve.max_jacobian_entries_evaluated > limits.solve_limits.max_jacobian_entries_evaluated) {
            throw Error(
                ErrorCategory::resource_limit,
                "snapshot solver limits exceed replay allowance; explicitly configure a bounded replay allowance");
        }
        solve.max_elapsed_time = std::chrono::milliseconds(static_cast<std::int64_t>(milliseconds));
        const auto& constraints = root.at("constraints");
        if (!constraints.is_array() || constraints.empty()) {
            invalid("expected at least one constraint");
        }
        if (constraints.size() > limits.max_constraints || constraints.size() > solve.max_constraints) {
            throw Error(ErrorCategory::resource_limit, "snapshot constraint count exceeds replay allowance");
        }
        result.constraints.reserve(constraints.size());
        for (const auto& row : constraints) {
            array_size(row, 9);
            LocalPlaneConstraint constraint {
                { number(row[0]), number(row[1]), number(row[2]) },
                { number(row[3]), number(row[4]), number(row[5]) },
                { number(row[6]), number(row[7]), number(row[8]) },
            };
            static_cast<void>(evaluate_local_constraint(request.current_transform.translation, constraint,
                                                        request.padding, request.initial_guess));
            result.constraints.push_back(constraint);
        }
        return result;
    }
    catch (const Json::exception&) {
        invalid("malformed JSON or incompatible field type");
    }
}

}  // namespace irop
