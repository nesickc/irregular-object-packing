#pragma once

#include <intrin.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// Psapi requires Windows declarations before it is included.
#include <Psapi.h>
#include <bcrypt.h>

#include "irop/model/triangle_mesh.hpp"

namespace irop::benchmark {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

[[nodiscard]] inline std::filesystem::path path_from_utf8(const std::string_view value)
{
    std::u8string bytes;
    bytes.reserve(value.size());
    for (const char character : value) {
        bytes.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path(bytes);
}

[[nodiscard]] inline std::string path_utf8(const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    return { bytes.begin(), bytes.end() };
}

inline void write_report(const std::filesystem::path& path, const std::string& contents)
{
    constexpr std::size_t maximum_report_bytes = 32ULL * 1024ULL * 1024ULL;
    if (contents.size() > maximum_report_bytes) {
        throw std::runtime_error("benchmark report exceeds its bounded output size");
    }
    struct File {
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~File()
        {
            if (handle != INVALID_HANDLE_VALUE) {
                static_cast<void>(CloseHandle(handle));
            }
        }
    } file;
    file.handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to exclusively create the new benchmark report");
    }
    DWORD written = 0;
    if (WriteFile(file.handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) == 0 ||
        written != contents.size()) {
        throw std::runtime_error("failed to write benchmark report");
    }
}

// Use the platform SHA256 implementation and stream bounded inputs. Hashing is
// measured separately from the packing service and warms filesystem caches.
[[nodiscard]] inline Json input_metadata(const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    const auto resolved = std::filesystem::canonical(path);
    const auto bytes = std::filesystem::file_size(resolved);
    if (bytes > maximum_bytes) {
        throw std::invalid_argument("benchmark input exceeds the mesh byte limit before hashing");
    }
    struct Hash {
        BCRYPT_HASH_HANDLE handle = nullptr;
        ~Hash()
        {
            if (handle != nullptr) {
                static_cast<void>(BCryptDestroyHash(handle));
            }
        }
    } hash;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash.handle, nullptr, 0, nullptr, 0, 0) != 0) {
        throw std::runtime_error("failed to create the benchmark SHA256 hash");
    }
    std::ifstream input(resolved, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open benchmark input for hashing");
    }
    std::array<unsigned char, 64 * 1024> buffer {};
    std::uint64_t consumed = 0;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::uint64_t>(input.gcount());
        if (count > maximum_bytes - consumed) {
            throw std::invalid_argument("benchmark input grew beyond the mesh byte limit during hashing");
        }
        consumed += count;
        if (BCryptHashData(hash.handle, buffer.data(), static_cast<ULONG>(count), 0) != 0) {
            throw std::runtime_error("failed to hash benchmark input");
        }
    }
    if (!input.eof() || consumed != bytes) {
        throw std::runtime_error("benchmark input changed size or could not be fully read during hashing");
    }
    std::array<unsigned char, 32> digest {};
    if (BCryptFinishHash(hash.handle, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        throw std::runtime_error("failed to finish the benchmark SHA256 hash");
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 0x0fU]);
    }
    return {
        { "resolved_path", path_utf8(resolved) },
        { "size_bytes",    bytes               },
        { "sha256",        encoded             }
    };
}

[[nodiscard]] inline double process_cpu_milliseconds()
{
    FILETIME creation {}, exit {}, kernel {}, user {};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
        throw std::runtime_error("GetProcessTimes failed");
    }
    const auto ticks = [](const FILETIME& value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10'000.0;
}

struct Timer {
    Clock::time_point started = Clock::now();
    double cpu_started = process_cpu_milliseconds();

    [[nodiscard]] Json elapsed() const
    {
        return {
            { "wall_ms", std::chrono::duration<double, std::milli>(Clock::now() - started).count() },
            { "cpu_ms", process_cpu_milliseconds() - cpu_started },
        };
    }
};

[[nodiscard]] inline Json memory_measurements()
{
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) == 0) {
        throw std::runtime_error("GetProcessMemoryInfo failed");
    }
    return {
        { "scope",                  "process lifetime including startup and dependency loading; one case per process" },
        { "peak_working_set_bytes", counters.PeakWorkingSetSize                                                       },
        { "peak_commit_bytes",      counters.PeakPagefileUsage                                                        },
        { "current_private_bytes",  counters.PrivateUsage                                                             },
    };
}

[[nodiscard]] inline Json machine_metadata()
{
    SYSTEM_INFO information {};
    GetNativeSystemInfo(&information);
    MEMORYSTATUSEX memory {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) == 0) {
        throw std::runtime_error("GlobalMemoryStatusEx failed");
    }
    char processor_name[49] {};
    int registers[4] {};
    __cpuid(registers, static_cast<int>(0x80000000U));
    if (static_cast<unsigned int>(registers[0]) >= 0x80000004U) {
        for (unsigned int index = 0; index < 3; ++index) {
            __cpuid(registers, static_cast<int>(0x80000002U + index));
            std::memcpy(processor_name + index * 16, registers, 16);
        }
    }
    return {
        { "os",                     "Windows"                                     },
        { "cpu_model",              processor_name                                },
        { "processor_architecture", information.wProcessorArchitecture            },
        { "logical_processors",     GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) },
        { "physical_memory_bytes",  memory.ullTotalPhys                           },
        { "page_size_bytes",        information.dwPageSize                        },
        { "pointer_bits",           sizeof(void*) * 8                             },
    };
}

[[nodiscard]] inline TriangleMesh box(const double x, const double y, const double z)
{
    return {
        .vertices = {
            { -x, -y, -z }, { x, -y, -z }, { x, y, -z }, { -x, y, -z },
            { -x, -y, z }, { x, -y, z }, { x, y, z }, { -x, y, z },
        },
        .triangles = {
            { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 },
            { 0, 1, 5 }, { 0, 5, 4 }, { 3, 7, 6 }, { 3, 6, 2 },
            { 0, 4, 7 }, { 0, 7, 3 }, { 1, 2, 6 }, { 1, 6, 5 },
        },
    };
}

[[nodiscard]] inline TriangleMesh tetrahedron()
{
    return {
        .vertices = { { -0.25, -0.25, -0.25 }, { 0.75, -0.25, -0.25 }, { -0.25, 0.75, -0.25 }, { -0.25, -0.25, 0.75 } },
        .triangles = { { 0, 2, 1 },             { 0, 1, 3 },            { 0, 3, 2 },            { 1, 2, 3 }            },
    };
}

[[nodiscard]] inline TriangleMesh cylinder(const double radius, const double half_height, const std::size_t segments)
{
    if (segments < 3 || segments > 512) {
        throw std::invalid_argument("benchmark cylinder requires 3 through 512 segments");
    }
    TriangleMesh result;
    result.vertices.reserve(2 * segments);
    constexpr double two_pi = 6.28318530717958647692;
    for (const double z : { -half_height, half_height }) {
        for (std::size_t index = 0; index < segments; ++index) {
            const double angle = two_pi * static_cast<double>(index) / static_cast<double>(segments);
            result.vertices.push_back({ radius * std::cos(angle), radius * std::sin(angle), z });
        }
    }
    const auto top = static_cast<MeshIndex>(segments);
    for (MeshIndex index = 0; index < top; ++index) {
        const MeshIndex next = (index + 1) % top;
        result.triangles.push_back({ index, next, top + next });
        result.triangles.push_back({ index, top + next, top + index });
    }
    for (MeshIndex index = 1; index + 1 < top; ++index) {
        result.triangles.push_back({ 0, index + 1, index });
        result.triangles.push_back({ top, top + index, top + index + 1 });
    }
    return result;
}

[[nodiscard]] inline Json mesh_metadata(const TriangleMesh& mesh)
{
    return {
        { "vertices",  mesh.vertices.size()  },
        { "triangles", mesh.triangles.size() }
    };
}

}  // namespace irop::benchmark
