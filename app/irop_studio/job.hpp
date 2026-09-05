#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "irop/io/run_scene.hpp"
#include "irop/packing/pack_scene.hpp"

namespace irop::studio {

enum class JobKind { preview, pack, open };

struct JobRequest {
    JobKind kind = JobKind::preview;
    std::filesystem::path object_path;
    std::filesystem::path container_path;
    std::filesystem::path output_directory;
    std::filesystem::path summary_path;
    PackOptions options;
};

struct JobCompletion {
    std::optional<LoadedRunScene> scene;
    std::string diagnostic;
    bool cancelled = false;
};

// Owns exactly one worker. The UI polls a latest-only progress value and moves
// completed geometry once; neither callbacks nor the worker access a window.
class Job final {
public:
    ~Job();
    void start(JobRequest request);
    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::optional<PackingProgress> progress() const;
    [[nodiscard]] std::optional<JobCompletion> take_completion();

private:
    std::jthread worker_;
    mutable std::mutex mutex_;
    std::optional<PackingProgress> progress_;
    std::optional<JobCompletion> completion_;
    bool active_ = false;  // UI-thread only, including start/take_completion.
};

}  // namespace irop::studio
