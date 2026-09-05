#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "run_directory.hpp"

#include <windows.h>

// Windows shell declarations require the base Win32 declarations first.
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>

#include "irop/error.hpp"

namespace irop::studio {
namespace {

constexpr std::uint64_t maximum_number = 999999999;
constexpr std::size_t maximum_entries = 100000;
constexpr DWORD maximum_settings_bytes = 65536;
constexpr wchar_t sequence_name[] = L".irop-run-sequence";
constexpr wchar_t settings_name[] = L"runs-folder.txt";
constexpr std::wstring_view settings_header = L"IROP-STUDIO-1\n";

class Handle final {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle()
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(value_, INVALID_HANDLE_VALUE); }

private:
    HANDLE value_;
};

[[noreturn]] void fail(const char* message) { throw Error(ErrorCategory::output_io, message); }

[[nodiscard]] bool exists_without_following(const std::filesystem::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return false;
    }
    fail("Cannot inspect the Runs folder or destination. Check its access permissions.");
}

void validate_file(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION information {};
    if (!GetFileInformationByHandle(handle, &information) || GetFileType(handle) != FILE_TYPE_DISK ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1) {
        fail("Studio metadata must be a regular file without links.");
    }
}

[[nodiscard]] std::filesystem::path canonical_parent(const std::filesystem::path& input, bool create)
{
    if (input.empty() || input.native().size() > 32700 || !input.is_absolute()) {
        fail("Choose an absolute Runs folder path within the Windows path limit.");
    }
    std::error_code error;
    if (create) {
        std::filesystem::create_directories(input, error);
        if (error) {
            fail("Cannot create the Runs folder. Choose a writable folder.");
        }
    }
    const auto result = std::filesystem::weakly_canonical(input, error);
    if (error || (exists_without_following(result) && !std::filesystem::is_directory(result, error)) || error) {
        fail("The Runs folder must be an accessible directory.");
    }
    return result;
}

[[nodiscard]] std::wstring run_name(std::uint64_t number)
{
    auto digits = std::to_wstring(number);
    if (digits.size() < 6) {
        digits.insert(0, 6 - digits.size(), L'0');
    }
    return L"run-" + digits;
}

[[nodiscard]] std::uint64_t numbered_entry(std::wstring_view name)
{
    constexpr std::wstring_view reservation_prefix = L".irop-reserve-";
    if (name.starts_with(reservation_prefix)) {
        name.remove_prefix(reservation_prefix.size());
    }
    if (!name.starts_with(L"run-")) {
        return 0;
    }
    name.remove_prefix(4);
    if (name.empty()) {
        return 0;
    }
    std::uint64_t value = 0;
    for (const wchar_t digit : name) {
        if (digit < L'0' || digit > L'9') {
            return 0;
        }
        if (value > (maximum_number - static_cast<std::uint64_t>(digit - L'0')) / 10) {
            fail("Runs folder numbering exceeds its supported limit. Choose another Runs folder.");
        }
        value = value * 10 + static_cast<std::uint64_t>(digit - L'0');
    }
    return value;
}

[[nodiscard]] std::uint64_t scanned_number(const std::filesystem::path& parent)
{
    if (!exists_without_following(parent)) {
        return 0;
    }
    std::error_code error;
    std::filesystem::directory_iterator iterator(parent, error);
    if (error) {
        fail("Cannot list the Runs folder. Check its access permissions.");
    }
    std::uint64_t maximum = 0;
    std::size_t count = 0;
    for (const std::filesystem::directory_iterator end; iterator != end; iterator.increment(error)) {
        if (error || ++count > maximum_entries) {
            fail("Runs folder cannot be scanned within its 100,000-entry limit. Choose another Runs folder.");
        }
        maximum = std::max(maximum, numbered_entry(iterator->path().filename().native()));
    }
    if (error) {
        fail("Cannot finish listing the Runs folder.");
    }
    return maximum;
}

[[nodiscard]] std::uint64_t read_sequence(HANDLE handle)
{
    validate_file(handle);
    LARGE_INTEGER size {};
    // An append-only, bounded ledger keeps previously flushed attempts intact
    // if a process stops during its next update. A partial last record fails
    // closed rather than reusing a possibly consumed run number.
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || size.QuadPart > 1'000'000 || size.QuadPart % 10 != 0) {
        fail("Studio run sequence is invalid or full. Choose another Runs folder.");
    }
    if (size.QuadPart == 0) {
        return 0;
    }
    LARGE_INTEGER position {};
    position.QuadPart = size.QuadPart - 10;
    std::array<char, 10> bytes {};
    DWORD count = 0;
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
        !ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || count != bytes.size()) {
        fail("Cannot read the Studio run sequence.");
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(bytes.data(), bytes.data() + 9, value);
    if (parsed.ec != std::errc {} || parsed.ptr != bytes.data() + 9 || bytes[9] != '\n' || value > maximum_number) {
        fail("Studio run sequence is invalid. Choose another Runs folder.");
    }
    return value;
}
[[nodiscard]] HANDLE open_sequence(const std::filesystem::path& path, bool write)
{
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        HANDLE result = CreateFileW(path.c_str(), GENERIC_READ | (write ? GENERIC_WRITE : 0), 0, nullptr,
                                    write ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (result != INVALID_HANDLE_VALUE) {
            return result;
        }
        const DWORD error = GetLastError();
        if (!write && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
            return INVALID_HANDLE_VALUE;
        }
        if (error != ERROR_SHARING_VIOLATION) {
            fail("Cannot access the Studio run sequence. Choose a writable Runs folder without metadata links.");
        }
        Sleep(5);
    }
    fail("Another Studio instance is updating this Runs folder. Try Run packing again.");
}

void write_sequence(HANDLE handle, std::uint64_t number)
{
    std::string bytes = std::to_string(number);
    bytes.insert(0, 9 - bytes.size(), '0');
    bytes.push_back('\n');
    LARGE_INTEGER size {};
    LARGE_INTEGER end {};
    DWORD written = 0;
    if (!GetFileSizeEx(handle, &size) || size.QuadPart >= 1'000'000 ||
        !SetFilePointerEx(handle, end, nullptr, FILE_END) ||
        !WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size() || !FlushFileBuffers(handle)) {
        fail("Cannot persist the next run number. Packing was not started; choose another Runs folder.");
    }
}
[[nodiscard]] std::filesystem::path known_application_directory()
{
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value))) {
        fail("Windows could not locate a writable user application-data folder.");
    }
    const std::filesystem::path result = std::filesystem::path(value) / L"IROP";
    CoTaskMemFree(value);
    return result;
}

[[nodiscard]] std::filesystem::path read_settings(const std::filesystem::path& path)
{
    const Handle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        fail("Could not read remembered Runs folder; using the default.");
    }
    validate_file(handle.get());
    LARGE_INTEGER size {};
    if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart <= 0 || size.QuadPart > maximum_settings_bytes ||
        size.QuadPart % sizeof(wchar_t) != 0) {
        fail("Remembered Runs folder settings are invalid or oversized; using the default.");
    }
    std::wstring value(static_cast<std::size_t>(size.QuadPart) / sizeof(wchar_t), L'\0');
    DWORD count = 0;
    if (!ReadFile(handle.get(), value.data(), static_cast<DWORD>(size.QuadPart), &count, nullptr) ||
        count != static_cast<DWORD>(size.QuadPart) || !value.starts_with(settings_header)) {
        fail("Remembered Runs folder settings are invalid; using the default.");
    }
    value.erase(0, settings_header.size());
    if (value.find(L'\0') != std::wstring::npos) {
        fail("Remembered Runs folder contains invalid text; using the default.");
    }
    return canonical_parent(value, false);
}

void save_settings(const std::filesystem::path& directory, const std::filesystem::path& parent)
{
    const auto destination = canonical_parent(directory, true) / settings_name;
    const std::wstring value = std::wstring(settings_header) + parent.native();
    if (value.size() > maximum_settings_bytes / sizeof(wchar_t)) {
        fail("Runs folder path is too long to remember.");
    }
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        fail("Remembered Runs folder settings cannot replace a directory or link.");
    }
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        const Handle existing(CreateFileW(destination.c_str(), FILE_READ_ATTRIBUTES,
                                          FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (existing.get() == INVALID_HANDLE_VALUE) {
            fail("Cannot safely inspect the remembered Runs folder settings.");
        }
        validate_file(existing.get());
    }
    // Each writer owns a unique temporary file; replacement makes a saved path
    // all-or-nothing. Concurrent selection is intentionally last-writer-wins.
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto temporary = destination.native() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(GetCurrentThreadId()) + L"-" + std::to_wstring(attempt);
        Handle handle(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) {
                continue;
            }
            fail("Cannot remember the Runs folder. Check the Studio settings folder permissions.");
        }
        DWORD written = 0;
        const auto bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        const bool saved = WriteFile(handle.get(), value.data(), bytes, &written, nullptr) && written == bytes &&
                           FlushFileBuffers(handle.get());
        CloseHandle(handle.release());
        if (!saved ||
            !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            fail("Cannot save the Runs folder preference.");
        }
        return;
    }
    fail("Studio settings have too many occupied temporary names.");
}

}  // namespace

struct RunReservation::Impl {
    std::filesystem::path output;
    Handle parent;
    Handle reservation;
    Impl(std::filesystem::path destination, HANDLE parent_handle, HANDLE handle) :
        output(std::move(destination)),
        parent(parent_handle),
        reservation(handle)
    {
    }
};

RunReservation::~RunReservation() = default;
RunReservation::RunReservation(RunReservation&&) noexcept = default;
RunReservation& RunReservation::operator=(RunReservation&&) noexcept = default;
RunReservation::RunReservation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
const std::filesystem::path& RunReservation::output_directory() const noexcept { return impl_->output; }

RunDirectories::RunDirectories(std::filesystem::path settings_directory)
{
    if (settings_directory.empty()) {
        const auto application = known_application_directory();
        settings_directory_ = application / L"Studio";
        parent_ = application / L"Runs";
    }
    else {
        settings_directory_ = canonical_parent(settings_directory, false);
        parent_ = settings_directory_ / L"runs";
    }
    try {
        const auto saved = read_settings(settings_directory_ / settings_name);
        if (!saved.empty()) {
            parent_ = saved;
        }
    }
    catch (const Error& error) {
        settings_warning_ = error.what();
    }
}

const std::filesystem::path& RunDirectories::parent() const noexcept { return parent_; }
const std::string& RunDirectories::settings_warning() const noexcept { return settings_warning_; }

void RunDirectories::select_parent(const std::filesystem::path& parent)
{
    // Selecting a usable session destination is independent of remembering it.
    // Broken or denied preference storage must not prevent a writable run.
    parent_ = canonical_parent(parent, false);
    settings_warning_.clear();
    try {
        save_settings(settings_directory_, parent_);
    }
    catch (const Error& error) {
        settings_warning_ =
            std::string("The Runs folder is selected for this session, but could not be remembered: ") + error.what();
    }
}

std::filesystem::path RunDirectories::next_directory() const
{
    const auto parent = canonical_parent(parent_, false);
    const Handle sequence(open_sequence(parent / sequence_name, false));
    const auto recorded = sequence.get() == INVALID_HANDLE_VALUE ? 0 : read_sequence(sequence.get());
    const auto maximum = std::max(recorded, scanned_number(parent));
    if (maximum >= maximum_number) {
        fail("Run numbers are exhausted. Choose another Runs folder.");
    }
    return parent / run_name(maximum + 1);
}

RunReservation RunDirectories::reserve() const
{
    const auto parent = canonical_parent(parent_, true);
    // Deny delete sharing on the canonical parent through the attempt, so its
    // pathname cannot be swapped for a different directory after allocation.
    Handle parent_handle(CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                     nullptr));
    BY_HANDLE_FILE_INFORMATION information {};
    if (parent_handle.get() == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(parent_handle.get(), &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        fail("Cannot safely hold the selected Runs folder. Choose an accessible directory.");
    }
    const Handle sequence(open_sequence(parent / sequence_name, true));
    std::uint64_t number = std::max(read_sequence(sequence.get()), scanned_number(parent));
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (number >= maximum_number) {
            fail("Run numbers are exhausted. Choose another Runs folder.");
        }
        ++number;
        const auto destination = parent / run_name(number);
        const auto reservation = parent / (L".irop-reserve-" + run_name(number));
        Handle handle(CreateFileW(reservation.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT,
                                  nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) {
                continue;
            }
            fail("Cannot reserve a new output in the Runs folder. Choose a writable folder.");
        }
        if (exists_without_following(destination)) {
            continue;
        }
        write_sequence(sequence.get(), number);
        auto owned = std::make_unique<RunReservation::Impl>(destination, parent_handle.get(), handle.get());
        static_cast<void>(parent_handle.release());
        static_cast<void>(handle.release());
        return RunReservation(std::move(owned));
    }
    fail("Could not reserve a fresh run destination after 100 attempts. Try another Runs folder.");
}

}  // namespace irop::studio
