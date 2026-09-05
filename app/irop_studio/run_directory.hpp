#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace irop::studio {

// Owns only an exclusive sibling reservation, never the final artifact directory.
// The operating system removes that exact reservation when its handle closes.
class RunReservation final {
public:
    ~RunReservation();
    RunReservation(RunReservation&&) noexcept;
    RunReservation& operator=(RunReservation&&) noexcept;
    RunReservation(const RunReservation&) = delete;
    RunReservation& operator=(const RunReservation&) = delete;
    [[nodiscard]] const std::filesystem::path& output_directory() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit RunReservation(std::unique_ptr<Impl> impl);
    friend class RunDirectories;
};

// Windows-only application policy. Core/CLI exact-output semantics are unchanged.
// A supplied settings directory isolates all preferences/default output in tests.
class RunDirectories final {
public:
    explicit RunDirectories(std::filesystem::path settings_directory = {});
    [[nodiscard]] const std::filesystem::path& parent() const noexcept;
    [[nodiscard]] const std::string& settings_warning() const noexcept;
    void select_parent(const std::filesystem::path& parent);
    [[nodiscard]] std::filesystem::path next_directory() const;
    [[nodiscard]] RunReservation reserve() const;

private:
    std::filesystem::path settings_directory_;
    std::filesystem::path parent_;
    std::string settings_warning_;
};

}  // namespace irop::studio
