#pragma once

#include <intrin.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// Psapi requires Windows declarations before it is included.
#include <Psapi.h>

#include "irop/model/triangle_mesh.hpp"

namespace irop::benchmark {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

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
