#include "job.hpp"

#include <utility>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"

namespace irop::studio {
namespace {

LoadedRunScene preview(const JobRequest& request, const std::function<bool()>& cancelled)
{
    const auto check_cancelled = [&] {
        if (cancelled()) {
            throw Error(ErrorCategory::cancelled, "Preview cancelled.");
        }
    };
    check_cancelled();
    RunSceneLimits limits;
    LoadedStl loaded_object = read_stl(request.object_path, limits.mesh_limits);
    TriangleMesh object = std::move(loaded_object.mesh);
    check_cancelled();
    static_cast<void>(ClosedMeshQuery(object));
    MeshLimits remaining = limits.mesh_limits;
    if (loaded_object.input_bytes >= remaining.max_input_bytes || object.vertices.size() >= remaining.max_vertices ||
        object.triangles.size() >= remaining.max_triangles) {
        throw Error(ErrorCategory::resource_limit, "Input preview exceeds the combined display mesh limit.");
    }
    remaining.max_input_bytes -= loaded_object.input_bytes;
    remaining.max_vertices -= object.vertices.size();
    remaining.max_triangles -= object.triangles.size();
    TriangleMesh container = read_stl(request.container_path, remaining).mesh;
    check_cancelled();
    static_cast<void>(ClosedMeshQuery(container));
    object = center_mesh_at_vertex_centroid(object).mesh;
    object = transform_mesh(object, Transform { .volume_scale = request.options.initialization.initial_volume_scale });
    check_cancelled();
    LoadedRunScene scene;
    scene.command = "preview";
    scene.status = "input_preview";
    scene.diagnostic = "Source object at its initial volume scale. This is an input preview, not a packed result.";
    scene.object_count = 1;
    scene.initial_volume_scale = request.options.initialization.initial_volume_scale;
    scene.objects = std::move(object);
    scene.container = std::move(container);
    return scene;
}

}  // namespace

Job::~Job()
{
    cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Job::start(JobRequest request)
{
    if (active_) {
        throw Error(ErrorCategory::invalid_configuration, "A job is already running.");
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        const std::lock_guard lock(mutex_);
        progress_.reset();
        completion_.reset();
    }
    worker_ = std::jthread([this, request = std::move(request)](const std::stop_token token) mutable {
        JobCompletion completed;
        try {
            const auto cancelled = [token] {
                return token.stop_requested();
            };
            if (request.kind == JobKind::preview) {
                completed.scene = preview(request, cancelled);
            }
            else if (request.kind == JobKind::open) {
                completed.scene = load_run_scene(request.summary_path, {}, cancelled);
            }
            else {
                request.options.callbacks.cancellation_requested = cancelled;
                request.options.callbacks.progress = [this](const PackingProgress& update) {
                    const std::lock_guard lock(mutex_);
                    progress_ = update;
                };
                const PackSceneResult packed =
                    pack_scene(request.object_path, request.container_path, request.output_directory, request.options);
                // Publication determines the terminal outcome. A stop arriving
                // after commit must not erase a successfully published result.
                try {
                    completed.scene = load_run_scene(packed.run_summary_path);
                }
                catch (const std::exception& error) {
                    LoadedRunScene recorded;
                    recorded.command = "pack";
                    recorded.status = to_string(packed.packing.status);
                    recorded.success = packed.packing.succeeded();
                    recorded.object_count = packed.packing.state.config.object_count;
                    recorded.seed = packed.packing.state.config.seed;
                    recorded.summary_path = packed.run_summary_path;
                    if (packed.packing.final_validation_performed) {
                        recorded.recorded_physical_validity = packed.packing.final_validation.physical_scene_valid();
                    }
                    recorded.diagnostic = std::string("Saved result could not be displayed: ") + error.what();
                    completed.scene = std::move(recorded);
                }
            }
        }
        catch (const Error& error) {
            completed.cancelled = error.category() == ErrorCategory::cancelled;
            completed.diagnostic = error.what();
        }
        catch (const std::exception& error) {
            completed.diagnostic = error.what();
        }
        catch (...) {
            completed.diagnostic = "Unexpected background job failure.";
        }
        const std::lock_guard lock(mutex_);
        completion_ = std::move(completed);
    });
    active_ = true;
}

void Job::cancel() noexcept { worker_.request_stop(); }

bool Job::active() const noexcept { return active_; }

std::optional<PackingProgress> Job::progress() const
{
    const std::lock_guard lock(mutex_);
    return progress_;
}

std::optional<JobCompletion> Job::take_completion()
{
    std::optional<JobCompletion> result;
    {
        const std::lock_guard lock(mutex_);
        if (!completion_) {
            return {};
        }
        result = std::move(completion_);
        completion_.reset();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    active_ = false;
    return result;
}

}  // namespace irop::studio
