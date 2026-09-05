#include "irop/io/run_scene.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "irop/error.hpp"
#include "irop/io/stl_io.hpp"

namespace irop {
namespace {

using Json = nlohmann::json;

[[noreturn]] void invalid_summary(const char* reason)
{
    throw Error(ErrorCategory::input_io, std::string("invalid run summary: ") + reason);
}

void check_cancelled(const std::function<bool()>& callback)
{
    if (!callback) {
        return;
    }
    bool cancelled = false;
    try {
        cancelled = callback();
    }
    catch (...) {
        throw Error(ErrorCategory::internal, "saved-result cancellation callback failed");
    }
    if (cancelled) {
        throw Error(ErrorCategory::cancelled, "saved-result loading was cancelled");
    }
}

void validate_limits(const RunSceneLimits& limits)
{
    if (limits.max_summary_bytes == 0 || limits.max_json_depth == 0 || limits.max_json_nodes == 0 ||
        limits.max_json_container_entries == 0 || limits.max_string_bytes == 0 || limits.max_object_count == 0 ||
        limits.mesh_limits.max_input_bytes == 0 || limits.mesh_limits.max_vertices == 0 ||
        limits.mesh_limits.max_triangles == 0) {
        throw Error(ErrorCategory::invalid_configuration, "saved-result limits must be positive");
    }
}

[[nodiscard]] std::filesystem::path canonical_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto resolved = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
        throw Error(ErrorCategory::input_io, "saved-result file is missing or is not a regular file");
    }
    return resolved;
}

struct JsonFrame {
    bool object = false;
    std::uint64_t entries = 0;
    std::set<std::string> keys;
};

[[nodiscard]] Json read_summary(const std::filesystem::path& path, const RunSceneLimits& limits,
                                const std::function<bool()>& cancellation)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw Error(ErrorCategory::input_io, "failed to inspect run-summary size");
    }
    if (size > limits.max_summary_bytes || size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw Error(ErrorCategory::resource_limit, "run-summary byte size exceeds the display limit");
    }
    if (size == 0) {
        invalid_summary("document is empty");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error(ErrorCategory::input_io, "failed to open run summary");
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size) || input.peek() != std::char_traits<char>::eof() ||
        input.bad()) {
        throw Error(ErrorCategory::input_io, "run summary changed or failed while being read");
    }
    check_cancelled(cancellation);
    std::deque<JsonFrame> frames;
    std::uint64_t nodes = 0;
    const auto count_entry = [&](JsonFrame& frame) {
        if (++frame.entries > limits.max_json_container_entries) {
            throw Error(ErrorCategory::resource_limit, "run-summary collection exceeds the display limit");
        }
    };
    const auto callback = [&](int, const Json::parse_event_t event, Json& value) {
        check_cancelled(cancellation);
        if (value.is_string() && value.get_ref<const std::string&>().size() > limits.max_string_bytes) {
            throw Error(ErrorCategory::resource_limit, "run-summary string exceeds the display limit");
        }
        if (event == Json::parse_event_t::key) {
            if (frames.empty() || !frames.back().object) {
                invalid_summary("invalid object structure");
            }
            count_entry(frames.back());
            if (!frames.back().keys.insert(value.get<std::string>()).second) {
                invalid_summary("duplicate object key");
            }
        }
        else if (event == Json::parse_event_t::object_end || event == Json::parse_event_t::array_end) {
            if (!frames.empty()) {
                frames.pop_back();
            }
        }
        else {
            if (++nodes > limits.max_json_nodes) {
                throw Error(ErrorCategory::resource_limit, "run-summary value count exceeds the display limit");
            }
            if (!frames.empty() && !frames.back().object) {
                count_entry(frames.back());
            }
            if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start) {
                if (frames.size() >= limits.max_json_depth) {
                    throw Error(ErrorCategory::resource_limit, "run-summary nesting exceeds the display limit");
                }
                frames.emplace_back();
                frames.back().object = event == Json::parse_event_t::object_start;
            }
        }
        return true;
    };
    return Json::parse(contents, callback);
}

[[nodiscard]] const Json& field(const Json& object, const char* key)
{
    if (!object.is_object() || !object.contains(key)) {
        invalid_summary("required display field is missing");
    }
    return object.at(key);
}

[[nodiscard]] std::string string_value(const Json& value, const std::size_t maximum)
{
    if (!value.is_string()) {
        invalid_summary("display text must be a string");
    }
    const auto& text = value.get_ref<const std::string&>();
    if (text.size() > maximum) {
        throw Error(ErrorCategory::resource_limit, "run-summary display text exceeds its limit");
    }
    if (text.find('\0') != std::string::npos) {
        invalid_summary("display text contains a null character");
    }
    return text;
}

[[nodiscard]] std::uint64_t unsigned_value(const Json& value, const std::uint64_t maximum)
{
    if (!value.is_number_integer() ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        invalid_summary("count must be a nonnegative integer");
    }
    const auto number = value.get<std::uint64_t>();
    if (number > maximum) {
        throw Error(ErrorCategory::resource_limit, "run-summary count exceeds the display limit");
    }
    return number;
}

[[nodiscard]] double finite_number(const Json& value)
{
    if (!value.is_number()) {
        invalid_summary("metric must be numeric");
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        invalid_summary("metric must be finite");
    }
    return number;
}

[[nodiscard]] double volume_scale(const Json& value)
{
    const double number = finite_number(value);
    if (number <= 0.0 || number > 1.0) {
        invalid_summary("volume scale must be in (0, 1]");
    }
    return number;
}

[[nodiscard]] std::optional<double> nullable_metric(const Json& value, const bool scale)
{
    if (value.is_null()) {
        return std::nullopt;
    }
    const double number = scale ? volume_scale(value) : finite_number(value);
    if (number < 0.0) {
        invalid_summary("metric must be nonnegative");
    }
    return number;
}

[[nodiscard]] bool known_status(const std::string& status)
{
    constexpr std::array names { "success",           "cancelled",          "invalid_input",    "resource_exhausted",
                                 "infeasible",        "iteration_limit",    "correction_limit", "time_limit",
                                 "numerical_failure", "dependency_failure", "internal_failure" };
    for (const auto* name : names) {
        if (status == name) {
            return true;
        }
    }
    return false;
}

void require_artifact_name(const Json& value, const char* expected, const bool initialization,
                           const RunSceneLimits& limits)
{
    const std::string text = string_value(value, static_cast<std::size_t>(limits.max_string_bytes));
    if (text == expected) {
        return;
    }
    // Initialization v1 writes absolute original publication paths. Use only
    // their expected leaf identity so a moved artifact set remains inspectable.
    if (initialization) {
        const std::filesystem::path path(std::u8string(text.begin(), text.end()));
        bool traversal = false;
        for (const auto& component : path) {
            traversal = traversal || component == "..";
        }
        if (path.is_absolute() && !traversal && path.filename() == expected) {
            return;
        }
    }
    invalid_summary("unexpected artifact path");
}

[[nodiscard]] TriangleMesh load_artifact(const std::filesystem::path& directory, const char* name,
                                         MeshLimits& remaining, const std::function<bool()>& cancellation)
{
    check_cancelled(cancellation);
    const auto path = canonical_file(directory / name);
    if (path.parent_path() != directory) {
        throw Error(ErrorCategory::input_io, "saved-result artifact resolves outside its directory");
    }
    LoadedStl loaded = read_stl(path, remaining);
    remaining.max_input_bytes -= loaded.input_bytes;
    remaining.max_vertices -= static_cast<std::uint64_t>(loaded.mesh.vertices.size());
    remaining.max_triangles -= static_cast<std::uint64_t>(loaded.mesh.triangles.size());
    check_cancelled(cancellation);
    return std::move(loaded.mesh);
}

void read_pack_metadata(const Json& document, const Json& config, const RunSceneLimits& limits, LoadedRunScene& scene)
{
    scene.target_volume_scale = volume_scale(field(config, "final_volume_scale"));
    if (scene.target_volume_scale < scene.initial_volume_scale) {
        invalid_summary("target volume scale precedes initial volume scale");
    }
    const auto& validation = field(document, "validation");
    const auto validation_status = string_value(field(validation, "status"), 16);
    const auto& physical = field(validation, "physical_scene_valid");
    if (validation_status == "not_run" && physical.is_null()) {
        scene.recorded_physical_validity.reset();
    }
    else if (validation_status == "passed" && physical.is_boolean() && physical.get<bool>()) {
        scene.recorded_physical_validity = true;
    }
    else if (validation_status == "failed" && physical.is_boolean() && !physical.get<bool>()) {
        scene.recorded_physical_validity = false;
    }
    else {
        invalid_summary("inconsistent recorded physical validation");
    }
    if (scene.success && scene.recorded_physical_validity != true) {
        invalid_summary("successful packing lacks passed physical validation");
    }
    const auto& metrics = field(document, "metrics");
    if (unsigned_value(field(metrics, "object_count"), limits.max_object_count) != scene.object_count ||
        volume_scale(field(metrics, "target_volume_scale")) != scene.target_volume_scale) {
        invalid_summary("metrics disagree with run configuration");
    }
    const auto at_target = unsigned_value(field(metrics, "objects_at_final_target"), scene.object_count);
    if (scene.success && at_target != scene.object_count) {
        invalid_summary("successful packing has unfinished objects");
    }
    scene.packing_fraction = nullable_metric(field(metrics, "packing_fraction"), false);
    scene.minimum_volume_scale = nullable_metric(field(metrics, "minimum_volume_scale"), true);
    scene.maximum_volume_scale = nullable_metric(field(metrics, "maximum_volume_scale"), true);
    // The writer accumulates scales before dividing. Its recorded mean can lie
    // a few rounding units outside the exact extrema for repeated equal scales.
    scene.mean_volume_scale = nullable_metric(field(metrics, "mean_volume_scale"), false);
    const double mean_tolerance =
        2.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(scene.object_count);
    if (scene.mean_volume_scale &&
        (*scene.mean_volume_scale <= 0.0 || *scene.mean_volume_scale > 1.0 + mean_tolerance)) {
        invalid_summary("mean volume scale is outside its numeric domain");
    }
    if (scene.minimum_volume_scale && scene.maximum_volume_scale && scene.mean_volume_scale &&
        (*scene.minimum_volume_scale > *scene.mean_volume_scale + mean_tolerance ||
         *scene.mean_volume_scale > *scene.maximum_volume_scale + mean_tolerance)) {
        invalid_summary("volume-scale metrics are inconsistent");
    }
    if (scene.success && (!scene.packing_fraction || !scene.minimum_volume_scale || !scene.maximum_volume_scale ||
                          !scene.mean_volume_scale || *scene.minimum_volume_scale != scene.target_volume_scale ||
                          *scene.maximum_volume_scale != scene.target_volume_scale)) {
        invalid_summary("successful packing metrics do not meet the target");
    }
}

[[nodiscard]] LoadedRunScene load_scene(const std::filesystem::path& summary_path, const RunSceneLimits& limits,
                                        const std::function<bool()>& cancellation)
{
    validate_limits(limits);
    check_cancelled(cancellation);
    LoadedRunScene scene;
    scene.summary_path = canonical_file(summary_path);
    const Json document = read_summary(scene.summary_path, limits, cancellation);
    if (unsigned_value(field(document, "schema_version"), std::numeric_limits<std::uint64_t>::max()) != 1) {
        invalid_summary("unsupported schema version");
    }
    scene.command = string_value(field(document, "command"), 16);
    const bool initialization = scene.command == "initialize";
    if (!initialization && scene.command != "pack") {
        invalid_summary("unsupported command");
    }
    const auto& outcome = field(document, "outcome");
    scene.status = string_value(field(outcome, "category"), 32);
    scene.success = scene.status == "success";
    if (!known_status(scene.status) || (initialization && !scene.success)) {
        invalid_summary("unsupported outcome category");
    }
    if (!initialization || outcome.contains("diagnostic")) {
        scene.diagnostic = string_value(field(outcome, "diagnostic"), 2048);
    }
    const auto& config = field(document, "config");
    scene.object_count = unsigned_value(field(config, "object_count"), limits.max_object_count);
    if (scene.object_count == 0) {
        invalid_summary("object count must be positive");
    }
    scene.seed =
        static_cast<std::uint32_t>(unsigned_value(field(config, "seed"), std::numeric_limits<std::uint32_t>::max()));
    scene.initial_volume_scale = volume_scale(field(config, "initial_volume_scale"));
    scene.target_volume_scale = scene.initial_volume_scale;
    if (!initialization) {
        read_pack_metadata(document, config, limits, scene);
    }
    const auto& warnings = field(document, "warnings");
    if (!warnings.is_array()) {
        invalid_summary("warnings must be an array");
    }
    if (warnings.size() > 256) {
        throw Error(ErrorCategory::resource_limit, "run-summary warning count exceeds the display limit");
    }
    for (const auto& warning : warnings) {
        scene.warnings.push_back(string_value(warning, 1024));
    }
    const auto& outputs = field(document, "outputs");
    const auto& individual = field(outputs, "individual_object_stls");
    if (!individual.is_array() || individual.size() > scene.object_count) {
        invalid_summary("invalid individual-artifact list");
    }
    for (const auto& path : individual) {
        static_cast<void>(string_value(path, static_cast<std::size_t>(limits.max_string_bytes)));
    }
    require_artifact_name(field(outputs, "run_summary_json"), "run-summary.json", initialization, limits);
    const char* objects_key = initialization ? "initialized_objects_stl" : "packed_objects_stl";
    const char* objects_name = initialization ? "initialized-objects.stl" : "packed-objects.stl";
    if (!scene.success) {
        if (!field(outputs, objects_key).is_null() || !field(outputs, "container_stl").is_null() ||
            !field(outputs, "placements_json").is_null() || !individual.empty()) {
            invalid_summary("unsuccessful run names success artifacts");
        }
        return scene;
    }
    require_artifact_name(field(outputs, objects_key), objects_name, initialization, limits);
    require_artifact_name(field(outputs, "container_stl"), "container.stl", initialization, limits);
    require_artifact_name(field(outputs, "placements_json"), "placements.json", initialization, limits);
    MeshLimits remaining = limits.mesh_limits;
    const auto directory = scene.summary_path.parent_path();
    scene.objects = load_artifact(directory, objects_name, remaining, cancellation);
    scene.container = load_artifact(directory, "container.stl", remaining, cancellation);
    return scene;
}

}  // namespace

LoadedRunScene load_run_scene(const std::filesystem::path& summary_path, const RunSceneLimits& limits,
                              const std::function<bool()>& cancellation_requested)
{
    try {
        return load_scene(summary_path, limits, cancellation_requested);
    }
    catch (const Json::exception&) {
        invalid_summary("malformed JSON or invalid field type");
    }
    catch (const std::bad_alloc&) {
        throw Error(ErrorCategory::resource_limit, "saved-result loading exhausted memory");
    }
    catch (const std::filesystem::filesystem_error&) {
        invalid_summary("artifact path cannot be represented");
    }
}

}  // namespace irop
