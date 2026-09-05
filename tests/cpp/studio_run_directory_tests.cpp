#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Security descriptor declarations need the base Win32 declarations first.
#include <sddl.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "run_directory.hpp"
#include "support/test_support.hpp"

namespace {

using irop::studio::RunDirectories;
using irop::studio::RunReservation;

class DenyNewFiles final {
public:
    explicit DenyNewFiles(std::filesystem::path parent) : parent_(std::move(parent))
    {
        DWORD needed = 0;
        GetFileSecurityW(parent_.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
        REQUIRE(needed > 0);
        original_.resize(needed);
        REQUIRE(GetFileSecurityW(parent_.c_str(), DACL_SECURITY_INFORMATION, original_.data(), needed, &needed));
        PSECURITY_DESCRIPTOR denied = nullptr;
        REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;0x00000006;;;WD)(A;;FA;;;WD)",
                                                                     SDDL_REVISION_1, &denied, nullptr));
        const bool installed = SetFileSecurityW(parent_.c_str(), DACL_SECURITY_INFORMATION, denied) != FALSE;
        LocalFree(denied);
        REQUIRE(installed);
    }
    ~DenyNewFiles() { SetFileSecurityW(parent_.c_str(), DACL_SECURITY_INFORMATION, original_.data()); }
    DenyNewFiles(const DenyNewFiles&) = delete;
    DenyNewFiles& operator=(const DenyNewFiles&) = delete;

private:
    std::filesystem::path parent_;
    std::vector<unsigned char> original_;
};

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.good());
    output << text;
    output.close();
    REQUIRE(output.good());
}

TEST_CASE("Studio remembers a Unicode Runs folder and advances unpublished attempts across restart",
          "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    const auto settings = temporary.path() / "settings";
    const auto parent = temporary.path() / std::filesystem::path(u8"запуски-tętra-网");
    RunDirectories directories(settings);
    CHECK(directories.parent() == settings / "runs");
    CHECK(directories.settings_warning().empty());
    directories.select_parent(parent);
    CHECK(directories.next_directory() == parent / "run-000001");
    CHECK_FALSE(std::filesystem::exists(parent));
    {
        const auto first = directories.reserve();
        CHECK(first.output_directory() == parent / "run-000001");
        CHECK_FALSE(std::filesystem::exists(first.output_directory()));
        CHECK(std::filesystem::exists(parent / ".irop-reserve-run-000001"));
        CHECK(directories.next_directory() == parent / "run-000002");
    }
    // Failure/cancellation before publication leaves no final directory. The
    // normal reservation is removed, while its durable number remains consumed.
    CHECK_FALSE(std::filesystem::exists(parent / ".irop-reserve-run-000001"));
    CHECK_FALSE(std::filesystem::exists(parent / "run-000001"));
    RunDirectories restarted(settings);
    CHECK(restarted.parent() == parent);
    CHECK(restarted.next_directory() == parent / "run-000002");
    const auto second = restarted.reserve();
    CHECK(second.output_directory() == parent / "run-000002");
    CHECK_FALSE(std::filesystem::exists(second.output_directory()));
}

TEST_CASE("Studio skips occupied and abandoned names without changing their content", "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    RunDirectories directories(temporary.path() / "settings");
    const auto parent = temporary.path() / "runs";
    directories.select_parent(parent);
    std::filesystem::create_directories(parent / "run-000002");
    write_text(parent / "run-000003", "existing result");
    write_text(parent / ".irop-reserve-run-000004", "abandoned reservation");
    {
        const auto reservation = directories.reserve();
        CHECK(reservation.output_directory() == parent / "run-000005");
    }
    CHECK(std::filesystem::file_size(parent / "run-000003") == 15);
    CHECK(std::filesystem::file_size(parent / ".irop-reserve-run-000004") == 21);
    CHECK(std::filesystem::is_directory(parent / "run-000002"));
    CHECK(directories.next_directory() == parent / "run-000006");
}

TEST_CASE("Concurrent Studio instances reserve distinct destinations", "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    const auto settings = temporary.path() / "settings";
    RunDirectories initial(settings);
    initial.select_parent(temporary.path() / "runs");
    std::vector<std::future<RunReservation>> workers;
    for (unsigned index = 0; index < 8; ++index) {
        workers.push_back(std::async(std::launch::async, [&settings] {
            return RunDirectories(settings).reserve();
        }));
    }
    std::vector<RunReservation> reservations;
    std::set<std::filesystem::path> outputs;
    for (auto& worker : workers) {
        auto reservation = worker.get();
        CHECK_FALSE(std::filesystem::exists(reservation.output_directory()));
        outputs.insert(reservation.output_directory());
        reservations.push_back(std::move(reservation));
    }
    CHECK(outputs.size() == 8);
    CHECK(initial.next_directory().filename() == "run-000009");
    reservations.clear();
    CHECK(RunDirectories(settings).next_directory().filename() == "run-000009");
}

TEST_CASE("Studio rejects unusable parents and unsafe sequence metadata before allocating a run",
          "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    RunDirectories directories(temporary.path() / "settings");
    const auto parent = temporary.path() / "runs";
    write_text(parent, "occupied parent");
    CHECK_THROWS_AS(directories.select_parent(parent), irop::Error);
    REQUIRE(std::filesystem::remove(parent));
    directories.select_parent(parent);
    REQUIRE(std::filesystem::create_directory(parent));
    SECTION("metadata directory") { REQUIRE(std::filesystem::create_directory(parent / ".irop-run-sequence")); }
    SECTION("torn sequence record") { write_text(parent / ".irop-run-sequence", "0000000"); }
    SECTION("oversized sequence") { write_text(parent / ".irop-run-sequence", std::string(1'000'010, '0')); }
    SECTION("exhausted numbering") { REQUIRE(std::filesystem::create_directory(parent / "run-999999999")); }
    SECTION("overflowing occupied name") { write_text(parent / "run-999999999999999999999999", "occupied"); }
    SECTION("metadata hard link")
    {
        const auto outside = temporary.path() / "outside.txt";
        write_text(outside, "000000001\n");
        std::filesystem::create_hard_link(outside, parent / ".irop-run-sequence");
    }
    CHECK_THROWS_AS(directories.reserve(), irop::Error);
    CHECK_FALSE(std::filesystem::exists(parent / "run-000001"));
    CHECK_FALSE(std::filesystem::exists(parent / ".irop-reserve-run-000001"));
}

TEST_CASE("Studio rejects a Runs folder without create permission before publishing anything",
          "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    RunDirectories directories(temporary.path() / "settings");
    const auto parent = temporary.path() / "runs";
    REQUIRE(std::filesystem::create_directory(parent));
    directories.select_parent(parent);
    {
        const DenyNewFiles denied(parent);
        CHECK_THROWS_AS(directories.reserve(), irop::Error);
        CHECK(std::filesystem::is_empty(parent));
    }
    const auto allowed = directories.reserve();
    CHECK(allowed.output_directory().filename() == "run-000001");
    CHECK_FALSE(std::filesystem::exists(allowed.output_directory()));
}

TEST_CASE("Studio ignores malformed bounded user settings without following links", "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    const auto settings = temporary.path() / "settings";
    REQUIRE(std::filesystem::create_directory(settings));
    SECTION("oversized") { write_text(settings / "runs-folder.txt", std::string(65538, 'x')); }
    SECTION("malformed") { write_text(settings / "runs-folder.txt", "bad settings"); }
    SECTION("directory") { REQUIRE(std::filesystem::create_directory(settings / "runs-folder.txt")); }
    RunDirectories directories(settings);
    CHECK(directories.parent() == settings / "runs");
    CHECK_FALSE(directories.settings_warning().empty());
    CHECK(directories.next_directory() == settings / "runs" / "run-000001");
    const auto reserved = directories.reserve();
    CHECK(reserved.output_directory() == settings / "runs" / "run-000001");
    CHECK_FALSE(std::filesystem::exists(reserved.output_directory()));
    CHECK_FALSE(directories.settings_warning().empty());
}

TEST_CASE("Studio keeps a usable session Runs folder when preferences cannot be saved", "[studio][run-directory]")
{
    irop::test::TempDirectory temporary;
    const auto settings = temporary.path() / "settings";
    const auto parent = temporary.path() / "chosen-runs";
    const auto metadata = settings / "runs-folder.txt";
    const auto outside = temporary.path() / "outside.txt";
    REQUIRE(std::filesystem::create_directory(settings));
    std::optional<DenyNewFiles> denied;
    SECTION("unsafe metadata directory") { REQUIRE(std::filesystem::create_directory(metadata)); }
    SECTION("unsafe metadata hard link")
    {
        write_text(outside, "unsafe settings");
        std::filesystem::create_hard_link(outside, metadata);
    }
    SECTION("settings directory denies creation") { denied.emplace(settings); }
    RunDirectories directories(settings);
    REQUIRE_NOTHROW(directories.select_parent(parent));
    CHECK(directories.parent() == parent);
    CHECK_FALSE(directories.settings_warning().empty());
    {
        const auto first = directories.reserve();
        CHECK(first.output_directory() == parent / "run-000001");
        CHECK_FALSE(std::filesystem::exists(first.output_directory()));
    }
    const auto second = directories.reserve();
    CHECK(second.output_directory() == parent / "run-000002");
    if (std::filesystem::exists(outside)) {
        CHECK(std::filesystem::equivalent(outside, metadata));
        CHECK(std::filesystem::file_size(outside) == 15);
        CHECK(std::filesystem::hard_link_count(outside) == 2);
    }
    else if (!denied) {
        CHECK(std::filesystem::is_directory(metadata));
    }
    else {
        CHECK_FALSE(std::filesystem::exists(metadata));
    }
}

}  // namespace
