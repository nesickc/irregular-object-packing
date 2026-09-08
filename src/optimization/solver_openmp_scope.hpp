#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "irop/error.hpp"
#include "irop/optimization/local_solver.hpp"

namespace irop::detail {

struct SolverThreadEnvironmentValue {
    bool present = false;
    // A present value without text exceeded the bound, was not printable ASCII,
    // or could not be read. This is not evidence that an MKL override is absent.
    std::optional<std::string> value;
};

// Controls only the current OpenMP task through the already-loaded pinned Intel
// runtime. MKL-specific environment settings can override this task value, so
// scoped_threads() is not a claim about the effective MKL team size. Scope and
// destruction must remain on the same thread; no process environment is changed.
class SolverOpenmpScope final {
public:
    static constexpr std::uint32_t maximum_requested_threads = maximum_local_solve_openmp_threads;

    explicit SolverOpenmpScope(const std::uint32_t requested_threads)
    {
        if (requested_threads > maximum_requested_threads) {
            throw Error(ErrorCategory::invalid_configuration,
                        "solver OpenMP thread request must be between zero and 256");
        }

        // Capture bounded metadata before any task mutation, including allocations.
        mkl_num_threads_ = read_environment(L"MKL_NUM_THREADS");
        mkl_domain_num_threads_ = read_environment(L"MKL_DOMAIN_NUM_THREADS");
        const HMODULE runtime = GetModuleHandleW(L"libiomp5md.dll");
        if (runtime == nullptr) {
            throw Error(ErrorCategory::dependency_failure, "the pinned solver OpenMP runtime is not loaded");
        }
        const FARPROC get_address = GetProcAddress(runtime, "omp_get_max_threads");
        const FARPROC set_address = GetProcAddress(runtime, "omp_set_num_threads");
        if (get_address == nullptr || set_address == nullptr) {
            throw Error(ErrorCategory::dependency_failure, "the solver OpenMP runtime lacks task thread controls");
        }
        const auto get_threads = std::bit_cast<GetThreads>(get_address);
        set_threads_ = std::bit_cast<SetThreads>(set_address);
        before_threads_ = get_threads();
        if (before_threads_ <= 0) {
            throw Error(ErrorCategory::dependency_failure, "the solver OpenMP runtime returned an invalid task value");
        }
        scoped_threads_ = before_threads_;
        if (requested_threads != 0) {
            set_threads_(static_cast<int>(requested_threads));
            scoped_threads_ = get_threads();
            if (scoped_threads_ != static_cast<int>(requested_threads)) {
                set_threads_(before_threads_);
                throw Error(ErrorCategory::dependency_failure, "the solver OpenMP task thread request was not applied");
            }
            restore_ = true;
        }
    }

    ~SolverOpenmpScope() noexcept
    {
        if (restore_) {
            set_threads_(before_threads_);
        }
    }

    SolverOpenmpScope(const SolverOpenmpScope&) = delete;
    SolverOpenmpScope& operator=(const SolverOpenmpScope&) = delete;
    SolverOpenmpScope(SolverOpenmpScope&&) = delete;
    SolverOpenmpScope& operator=(SolverOpenmpScope&&) = delete;

    [[nodiscard]] int before_threads() const noexcept { return before_threads_; }
    [[nodiscard]] int scoped_threads() const noexcept { return scoped_threads_; }
    [[nodiscard]] const SolverThreadEnvironmentValue& mkl_num_threads() const noexcept { return mkl_num_threads_; }
    [[nodiscard]] const SolverThreadEnvironmentValue& mkl_domain_num_threads() const noexcept
    {
        return mkl_domain_num_threads_;
    }

private:
    using GetThreads = int(__cdecl*)();
    using SetThreads = void(__cdecl*)(int);

    [[nodiscard]] static SolverThreadEnvironmentValue read_environment(const wchar_t* name)
    {
        std::array<wchar_t, 257> buffer {};
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_ENVVAR_NOT_FOUND) {
                return {};
            }
            if (error == ERROR_SUCCESS) {
                return { .present = true, .value = std::string {} };
            }
            return { .present = true, .value = std::nullopt };
        }
        if (length >= buffer.size()) {
            return { .present = true, .value = std::nullopt };
        }
        std::string value;
        value.reserve(length);
        for (DWORD index = 0; index < length; ++index) {
            if (buffer[index] < L' ' || buffer[index] > L'~') {
                return { .present = true, .value = std::nullopt };
            }
            value.push_back(static_cast<char>(buffer[index]));
        }
        return { .present = true, .value = std::move(value) };
    }

    SetThreads set_threads_ = nullptr;
    int before_threads_ = 0;
    int scoped_threads_ = 0;
    bool restore_ = false;
    SolverThreadEnvironmentValue mkl_num_threads_;
    SolverThreadEnvironmentValue mkl_domain_num_threads_;
};

}  // namespace irop::detail
