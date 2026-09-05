#include <windows.h>

// Windows extension headers require the base Win32 declarations first.
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "irop/error.hpp"
#include "job.hpp"
#include "run_directory.hpp"
#include "viewport.hpp"

namespace {

using irop::studio::JobKind;
constexpr wchar_t window_class[] = L"IropStudioWindow";
constexpr UINT_PTR poll_timer = 1;
constexpr COLORREF ink = RGB(26, 43, 60);
constexpr COLORREF muted = RGB(91, 107, 122);
enum ControlId {
    object_path = 101,
    container_path,
    output_path,
    object_count,
    seed,
    initial_scale,
    final_scale,
    steps,
    seconds,
    adaptive,
    fallback,
    capture_diagnostics,
    next_output,
    result_folder,
    result_location,
    browse_object,
    browse_container,
    browse_output,
    preview_button,
    run_button,
    cancel_button,
    open_button,
    fit_button,
    iso_button,
    x_button,
    y_button,
    z_button,
    container_visible,
    wireframe,
    progress_bar,
    status_title,
    details_text
};

std::wstring wide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Text exceeds the Windows display limit.");
    }
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count == 0) {
        return L"Text contains invalid UTF-8.";
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        count);
    return result;
}

std::wstring text_of(HWND control)
{
    const int count = GetWindowTextLengthW(control);
    if (count > 32767) {
        throw std::runtime_error("Field exceeds the supported length.");
    }
    std::wstring result(static_cast<std::size_t>(count) + 1, L'\0');
    GetWindowTextW(control, result.data(), count + 1);
    result.resize(static_cast<std::size_t>(count));
    return result;
}

template <class Number>
Number number_of(HWND control, const char* label)
{
    const std::wstring value = text_of(control);
    std::string ascii;
    for (const wchar_t character : value) {
        if (character > 127) {
            throw std::runtime_error(std::string(label) + " must be a number using a decimal point.");
        }
        ascii.push_back(static_cast<char>(character));
    }
    Number result {};
    const auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), result);
    if (parsed.ec != std::errc {} || parsed.ptr != ascii.data() + ascii.size()) {
        throw std::runtime_error(std::string("Enter a valid ") + label + ".");
    }
    return result;
}

struct DialogRelease {
    void operator()(IFileOpenDialog* value) const noexcept
    {
        if (value) {
            value->Release();
        }
    }
};
struct ItemRelease {
    void operator()(IShellItem* value) const noexcept
    {
        if (value) {
            value->Release();
        }
    }
};

std::optional<std::filesystem::path> choose_path(HWND owner, bool folder, bool summary)
{
    IFileOpenDialog* raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw)))) {
        throw std::runtime_error("Windows could not open the file picker.");
    }
    const std::unique_ptr<IFileOpenDialog, DialogRelease> dialog(raw);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dialog->SetTitle(folder ? L"Choose the Runs folder" : summary ? L"Open run-summary.json" : L"Choose an STL mesh");
    const COMDLG_FILTERSPEC filter { summary ? L"Run summary (JSON)" : L"Triangle mesh (STL)",
                                     summary ? L"*.json" : L"*.stl" };
    if (!folder) {
        dialog->SetFileTypes(1, &filter);
    }
    const HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    if (FAILED(shown)) {
        throw std::runtime_error("The Windows file picker failed.");
    }
    IShellItem* raw_item = nullptr;
    if (FAILED(dialog->GetResult(&raw_item))) {
        throw std::runtime_error("No file was selected.");
    }
    const std::unique_ptr<IShellItem, ItemRelease> item(raw_item);
    PWSTR selected = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
        throw std::runtime_error("Select a local filesystem path.");
    }
    const std::filesystem::path result(selected);
    CoTaskMemFree(selected);
    return result;
}

struct Startup {
    std::filesystem::path object;
    std::filesystem::path container;
    std::filesystem::path summary;
    std::filesystem::path smoke_directory;
    std::filesystem::path settings_directory;
    std::wstring smoke_action = L"open";
};

Startup parse_startup()
{
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        throw std::runtime_error("Cannot read application arguments.");
    }
    const auto release = [](wchar_t** value) {
        LocalFree(value);
    };
    const std::unique_ptr<wchar_t*, decltype(release)> owned(arguments, release);
    Startup result;
    for (int index = 1; index < count; ++index) {
        const std::wstring flag(arguments[index]);
        if (index + 1 >= count) {
            throw std::runtime_error("Expected a value after the Studio option.");
        }
        const wchar_t* value = arguments[++index];
        if (flag == L"--open") {
            result.summary = value;
        }
        else if (flag == L"--object") {
            result.object = value;
        }
        else if (flag == L"--container") {
            result.container = value;
        }
        else if (flag == L"--smoke-test") {
            result.smoke_directory = value;
        }
        else if (flag == L"--settings-dir") {
            result.settings_directory = value;
        }
        else if (flag == L"--smoke-action") {
            result.smoke_action = value;
        }
        else {
            throw std::runtime_error("Studio options: --open SUMMARY, --object STL, --container STL.");
        }
    }
    if (!result.smoke_directory.empty() && result.settings_directory.empty()) {
        result.settings_directory = std::filesystem::absolute(result.smoke_directory).native() + L"-settings";
    }
    return result;
}

class Studio final {
public:
    explicit Studio(Startup startup) : startup_(std::move(startup)), run_directories_(startup_.settings_directory) {}
    ~Studio()
    {
        viewport_.reset();
        if (font_) {
            DeleteObject(font_);
        }
        if (heading_font_) {
            DeleteObject(heading_font_);
        }
        if (title_font_) {
            DeleteObject(title_font_);
        }
    }
    bool create(HINSTANCE instance, int show)
    {
        WNDCLASSEXW type {};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = window_proc;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        type.lpszClassName = window_class;
        type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if (!RegisterClassExW(&type)) {
            throw std::runtime_error("Cannot register Studio window.");
        }
        hwnd_ = CreateWindowExW(0, window_class, L"IROP Studio | Irregular object packing",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1240, 890, nullptr,
                                nullptr, instance, this);
        if (!hwnd_) {
            return false;
        }
        ShowWindow(hwnd_, show);
        if (!startup_.smoke_directory.empty()) {
            // Build tools can supply SW_HIDE for the first ShowWindow call.
            // The desktop smoke needs the real window rendered for capture,
            // while leaving the user's current foreground window active.
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        }
        UpdateWindow(hwnd_);
        PostMessageW(hwnd_, WM_APP + 1, 0, 0);
        return true;
    }
    HWND window() const noexcept { return hwnd_; }

private:
    Startup startup_;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT heading_font_ = nullptr;
    HFONT title_font_ = nullptr;
    UINT dpi_ = 96;
    std::unique_ptr<irop::studio::Viewport> viewport_;
    irop::studio::RunDirectories run_directories_;
    std::optional<irop::studio::RunReservation> run_reservation_;
    irop::studio::Job job_;
    std::filesystem::path displayed_result_folder_;
    std::vector<HWND> settings_;
    std::wstring status_ = L"Ready to explore";
    std::wstring details_ =
        L"Choose an object and a container, then preview or pack.\r\nOr open a saved run to inspect its geometry and "
        L"outcome.";
    bool closing_ = false;
    bool smoke_started_ = false;
    bool smoke_finishing_ = false;
    unsigned smoke_ticks_ = 0;
    unsigned smoke_completions_ = 0;
    JobKind current_kind_ = JobKind::preview;

    int px(int value) const noexcept { return MulDiv(value, static_cast<int>(dpi_), 96); }
    HWND control(int id) const noexcept { return GetDlgItem(hwnd_, id); }
    void set(int id, const std::wstring& text)
    {
        SetWindowTextW(control(id), text.c_str());
        if (id >= object_path && id <= output_path) {
            const int length = GetWindowTextLengthW(control(id));
            SendMessageW(control(id), EM_SETSEL, static_cast<WPARAM>(length), length);
            SendMessageW(control(id), EM_SCROLLCARET, 0, 0);
        }
    }
    HWND add(const wchar_t* kind, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height,
             bool setting = false)
    {
        HWND child =
            CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text,
                            WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(width), px(height), hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!child) {
            throw std::runtime_error("Cannot create a Studio control.");
        }
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (setting) {
            settings_.push_back(child);
        }
        return child;
    }
    void label(const wchar_t* text, int x, int y, int width = 290)
    {
        add(L"STATIC", text, SS_LEFT, -1, x, y, width, 22);
    }
    void section(const wchar_t* text, int y)
    {
        HWND child = add(L"STATIC", text, SS_LEFT, -1, 24, y, 296, 25);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font_), TRUE);
    }
    void edit(int id, const wchar_t* value, int x, int y, int width = 278)
    {
        HWND child = add(L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL, id, x, y, width, 29, true);
        SendMessageW(child, EM_SETLIMITTEXT, id <= output_path ? 32767 : 48, 0);
        if (id <= output_path) {
            const int length = GetWindowTextLengthW(child);
            SendMessageW(child, EM_SETSEL, static_cast<WPARAM>(length), length);
            SendMessageW(child, EM_SCROLLCARET, 0, 0);
        }
    }
    void button(int id, const wchar_t* value, int x, int y, int width, bool setting = false)
    {
        add(L"BUTTON", value, WS_TABSTOP | BS_PUSHBUTTON, id, x, y, width, 30, setting);
    }
    void initialize()
    {
        dpi_ = GetDpiForWindow(hwnd_);
        font_ = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading_font_ =
            CreateFontW(-px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        title_font_ =
            CreateFontW(-px(26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        section(L"01   MODELS", 96);
        label(L"Object mesh", 24, 130);
        edit(object_path, startup_.object.c_str(), 24, 153, 238);
        button(browse_object, L"...", 270, 153, 42, true);
        label(L"Container mesh", 24, 192);
        edit(container_path, startup_.container.c_str(), 24, 215, 238);
        button(browse_container, L"...", 270, 215, 42, true);
        button(preview_button, L"Preview inputs", 24, 260, 288, true);
        section(L"02   NEXT RUN", 318);
        label(L"Copies", 24, 351, 132);
        label(L"Random seed", 178, 351, 134);
        edit(object_count, L"10", 24, 374, 132);
        edit(seed, L"1918", 178, 374, 134);
        label(L"Initial volume scale", 24, 416, 145);
        label(L"Target volume scale", 178, 416, 150);
        edit(initial_scale, L"0.1", 24, 439, 132);
        edit(final_scale, L"1.0", 178, 439, 134);
        label(L"Scale steps", 24, 481, 132);
        label(L"Time limit (seconds)", 178, 481, 150);
        edit(steps, L"9", 24, 504, 132);
        edit(seconds, L"300", 178, 504, 134);
        add(L"BUTTON", L"Adaptive mesh sampling", WS_TABSTOP | BS_AUTOCHECKBOX, adaptive, 24, 548, 288, 25, true);
        add(L"BUTTON", L"Structured initialization fallback", WS_TABSTOP | BS_AUTOCHECKBOX, fallback, 24, 577, 288, 25,
            true);
        SendMessageW(control(adaptive), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(control(fallback), BM_SETCHECK, BST_CHECKED, 0);
        add(L"BUTTON", L"Capture solver diagnostics", WS_TABSTOP | BS_AUTOCHECKBOX, capture_diagnostics, 24, 605, 288,
            25, true);
        section(L"03   OUTPUT", 635);
        label(L"Runs folder", 24, 663);
        edit(output_path, run_directories_.parent().c_str(), 24, 686, 238);
        SendMessageW(control(output_path), EM_SETREADONLY, TRUE, 0);
        button(browse_output, L"...", 270, 686, 42, true);
        add(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, next_output, 24, 719, 288, 22);
        button(run_button, L"Run packing", 24, 751, 186, true);
        button(cancel_button, L"Cancel", 220, 751, 92);
        button(result_folder, L"Open result folder", 24, 791, 288);
        EnableWindow(control(result_folder), FALSE);
        refresh_next_output();
        EnableWindow(control(cancel_button), FALSE);
        button(open_button, L"Open saved run...", 350, 96, 160, true);
        button(fit_button, L"Fit", 526, 96, 52);
        button(iso_button, L"3D", 584, 96, 48);
        button(x_button, L"X", 638, 96, 38);
        button(y_button, L"Y", 682, 96, 38);
        button(z_button, L"Z", 726, 96, 38);
        add(L"BUTTON", L"Container", WS_TABSTOP | BS_AUTOCHECKBOX, container_visible, 784, 98, 100, 25);
        add(L"BUTTON", L"Wireframe", WS_TABSTOP | BS_AUTOCHECKBOX, wireframe, 891, 98, 108, 25);
        SendMessageW(control(container_visible), BM_SETCHECK, BST_CHECKED, 0);
        add(PROGRESS_CLASSW, L"", PBS_MARQUEE, progress_bar, 350, 670, 790, 4);
        HWND status = add(L"STATIC", status_.c_str(), SS_LEFT, status_title, 350, 688, 790, 26);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font_), TRUE);
        add(L"EDIT", details_.c_str(), ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, details_text, 350, 720,
            790, 78);
        add(L"EDIT", L"No saved result selected.", ES_READONLY | ES_AUTOHSCROLL, result_location, 350, 806, 790, 26);
        viewport_ = std::make_unique<irop::studio::Viewport>(hwnd_);
        layout();
        SetTimer(hwnd_, poll_timer, 100, nullptr);
        if (!run_directories_.settings_warning().empty()) {
            show_status(L"Runs folder preference unavailable", wide(run_directories_.settings_warning()));
        }
    }
    void refresh_next_output()
    {
        try {
            set(next_output, L"Next: " + run_directories_.next_directory().filename().native() +
                                 (run_directories_.settings_warning().empty() ? L"" : L" | Settings warning"));
        }
        catch (const std::exception& error) {
            set(next_output, wide(error.what()));
        }
    }
    void layout()
    {
        if (!viewport_) {
            return;
        }
        RECT area {};
        GetClientRect(hwnd_, &area);
        const int width = std::max(1, static_cast<int>(area.right) - px(374));
        const int footer = static_cast<int>(area.bottom) - px(174);
        viewport_->resize(px(350), px(142), width, std::max(px(180), footer - px(166)));
        MoveWindow(control(progress_bar), px(350), footer, width, px(4), TRUE);
        MoveWindow(control(status_title), px(350), footer + px(15), width, px(26), TRUE);
        MoveWindow(control(details_text), px(350), footer + px(46), width, px(76), TRUE);
        MoveWindow(control(result_location), px(350), footer + px(132), width, px(26), TRUE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    void show_status(const std::wstring& title, const std::wstring& details)
    {
        status_ = title;
        details_ = details;
        set(status_title, status_);
        set(details_text, details_);
    }
    void busy(bool value)
    {
        for (HWND child : settings_) {
            EnableWindow(child, !value);
        }
        EnableWindow(control(cancel_button), value && !closing_);
        SendMessageW(control(progress_bar), PBM_SETMARQUEE, value, 35);
    }
    irop::studio::JobRequest request(JobKind kind)
    {
        irop::studio::JobRequest result;
        result.kind = kind;
        result.object_path = text_of(control(object_path));
        result.container_path = text_of(control(container_path));
        auto& options = result.options;
        options.initialization.object_count = number_of<std::uint64_t>(control(object_count), "copy count");
        options.initialization.seed = number_of<std::uint32_t>(control(seed), "random seed");
        options.initialization.initial_volume_scale = number_of<double>(control(initial_scale), "initial volume scale");
        options.initialization.enable_structured_fallback =
            SendMessageW(control(fallback), BM_GETCHECK, 0, 0) == BST_CHECKED;
        options.algorithm.final_volume_scale = number_of<double>(control(final_scale), "target volume scale");
        options.algorithm.scale_step_count = number_of<std::uint64_t>(control(steps), "scale step count");
        options.algorithm.adaptive_sampling = SendMessageW(control(adaptive), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (SendMessageW(control(capture_diagnostics), BM_GETCHECK, 0, 0) == BST_CHECKED) {
            options.algorithm.diagnostics.capture_failed_local_problem = true;
            options.algorithm.diagnostics.max_trace_records_per_solve = 128;
            options.algorithm.diagnostics.max_local_solve_records = 64;
        }
        const auto duration = number_of<std::uint64_t>(control(seconds), "time limit");
        if (duration == 0 || duration > static_cast<std::uint64_t>(std::chrono::milliseconds::max().count()) / 1000) {
            throw std::runtime_error("Time limit must be positive and representable in milliseconds.");
        }
        options.limits.max_elapsed_time =
            std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(duration * 1000));
        irop::validate_packing_config(options.initialization);
        irop::validate_packing_algorithm_config(options.initialization.initial_volume_scale, options.algorithm,
                                                options.limits);
        if (result.object_path.empty() || result.container_path.empty()) {
            throw std::runtime_error("Choose both an object mesh and a container mesh.");
        }

        return result;
    }
    void start(irop::studio::JobRequest selected)
    {
        if (selected.kind == JobKind::pack) {
            auto reservation = run_directories_.reserve();
            selected.output_directory = reservation.output_directory();
            run_reservation_ = std::move(reservation);
            refresh_next_output();
        }
        current_kind_ = selected.kind;
        try {
            job_.start(std::move(selected));
        }
        catch (...) {
            run_reservation_.reset();
            throw;
        }
        viewport_->clear();
        busy(true);
        show_status(current_kind_ == JobKind::pack ? L"Packing in progress" : L"Loading geometry",
                    current_kind_ == JobKind::pack
                        ? L"Preparing meshes and initial placements. You can move the camera or cancel the run."
                        : L"Reading the selected files and checking display limits...");
    }
    void open(const std::filesystem::path& path)
    {
        irop::studio::JobRequest selected;
        selected.kind = JobKind::open;
        selected.summary_path = std::filesystem::is_directory(path) ? path / L"run-summary.json" : path;
        start(std::move(selected));
    }
    void command(int id)
    {
        if (id == cancel_button) {
            job_.cancel();
            EnableWindow(control(cancel_button), FALSE);
            show_status(L"Cancellation requested",
                        L"Waiting for the current geometry or solver call to return safely...");
        }
        else if (id == fit_button) {
            viewport_->fit();
        }
        else if (id == iso_button) {
            viewport_->set_view(-1);
        }
        else if (id == x_button || id == y_button || id == z_button) {
            viewport_->set_view(id - x_button);
        }
        else if (id == container_visible) {
            viewport_->set_container_visible(SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (id == wireframe) {
            viewport_->set_objects_wireframe(SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (id == result_folder && !displayed_result_folder_.empty()) {
            const auto opened = reinterpret_cast<INT_PTR>(
                ShellExecuteW(hwnd_, L"explore", displayed_result_folder_.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (opened <= 32) {
                throw std::runtime_error("Windows could not open the result folder.");
            }
        }
        else if (!job_.active()) {
            if (id == browse_object || id == browse_container || id == browse_output || id == open_button) {
                const auto chosen = choose_path(hwnd_, id == browse_output, id == open_button);
                if (chosen) {
                    if (id == open_button) {
                        open(*chosen);
                    }
                    else if (id == browse_output) {
                        run_directories_.select_parent(*chosen);
                        set(output_path, run_directories_.parent().native());
                        refresh_next_output();
                        if (!run_directories_.settings_warning().empty()) {
                            show_status(L"Runs folder selected for this session",
                                        wide(run_directories_.settings_warning()));
                        }
                    }
                    else {
                        set(id == browse_object ? object_path : container_path, chosen->native());
                    }
                }
            }
            else if (id == preview_button) {
                start(request(JobKind::preview));
            }
            else if (id == run_button) {
                start(request(JobKind::pack));
            }
        }
    }
    void display(const irop::LoadedRunScene& scene)
    {
        if (!scene.summary_path.empty()) {
            displayed_result_folder_ = scene.summary_path.parent_path();
            set(result_location, L"Result: " + scene.summary_path.native());
            const int length = GetWindowTextLengthW(control(result_location));
            SendMessageW(control(result_location), EM_SETSEL, static_cast<WPARAM>(length), length);
            SendMessageW(control(result_location), EM_SCROLLCARET, 0, 0);
            EnableWindow(control(result_folder), TRUE);
        }
        if (scene.objects && scene.container) {
            viewport_->show_scene(*scene.objects, *scene.container);
        }
        else {
            viewport_->clear();
        }
        std::wostringstream details;
        if (scene.command == "preview") {
            details << wide(scene.diagnostic);
        }
        else {
            details << scene.object_count << (scene.object_count == 1 ? L" object  |  seed " : L" objects  |  seed ")
                    << scene.seed;
            if (scene.packing_fraction) {
                details.precision(4);
                details << L"  |  volume fraction " << *scene.packing_fraction * 100.0 << L"%";
            }
            details << L"\r\nRecorded physical validation: "
                    << (scene.recorded_physical_validity ? (*scene.recorded_physical_validity ? L"passed" : L"failed")
                                                         : L"not recorded");
            if (!scene.diagnostic.empty()) {
                details << L"\r\n" << wide(scene.diagnostic);
            }
            for (const auto& warning : scene.warnings) {
                details << L"\r\n" << wide(warning);
            }
        }
        show_status(scene.command == "preview" ? L"Input preview"
                    : scene.success
                        ? (scene.command == "initialize" ? L"Initialized scene | Success" : L"Packing result | Success")
                        : L"Run ended | " + wide(scene.status),
                    details.str());
    }
    void capture_client(const std::filesystem::path& path)
    {
        if (!IsWindowVisible(hwnd_)) {
            throw std::runtime_error("The smoke window is not visible for native capture.");
        }
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        RECT area {};
        GetClientRect(hwnd_, &area);
        const int width = static_cast<int>(area.right), height = static_cast<int>(area.bottom);
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            throw std::runtime_error("Smoke window capture exceeds its pixel bound.");
        }
        HDC screen = GetDC(hwnd_);
        HDC memory = CreateCompatibleDC(screen);
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!screen || !memory || !bitmap || !pixels) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            if (memory) {
                DeleteDC(memory);
            }
            if (screen) {
                ReleaseDC(hwnd_, screen);
            }
            throw std::runtime_error("Cannot allocate smoke window capture.");
        }
        HGDIOBJ previous = SelectObject(memory, bitmap);
        const auto release = [&]() noexcept {
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);
            ReleaseDC(hwnd_, screen);
        };
        try {
            // WM_PRINTCLIENT shares the actual WM_PAINT drawing routine below;
            // standard child controls supply their own native print handlers.
            const BOOL captured = PrintWindow(hwnd_, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
            const DWORD bytes = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4;
            const auto* colors = static_cast<const DWORD*>(pixels);
            const bool has_color = std::any_of(colors, colors + bytes / 4, [](const DWORD color) {
                return (color & 0x00FFFFFFU) != 0;
            });
            if (!captured || !has_color) {
                throw std::runtime_error("Native window capture returned no visible pixels.");
            }
            BITMAPFILEHEADER header {};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + bytes;
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(&header), sizeof(header));
            output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
            output.write(static_cast<const char*>(pixels), bytes);
            output.close();
            if (!output) {
                throw std::runtime_error("Cannot write smoke window capture.");
            }
        }
        catch (...) {
            release();
            throw;
        }
        release();
    }
    void finish_smoke(const irop::studio::JobCompletion& completion)
    {
        if (startup_.smoke_directory.empty()) {
            return;
        }
        ++smoke_completions_;
        if (smoke_completions_ == 1 &&
            (startup_.smoke_action == L"rerun" || startup_.smoke_action == L"cancel-rerun" ||
             startup_.smoke_action == L"failure-rerun" || startup_.smoke_action == L"settings-fallback")) {
            if (startup_.smoke_action == L"settings-fallback" &&
                (run_directories_.settings_warning().empty() || !completion.scene || !completion.scene->success)) {
                throw std::runtime_error("Packing did not continue with a visible preference warning.");
            }
            if (startup_.smoke_action == L"rerun" && (!completion.scene || !completion.scene->success)) {
                throw std::runtime_error("The first repeated packing run did not succeed.");
            }
            if (startup_.smoke_action == L"cancel-rerun" && !completion.cancelled &&
                (!completion.scene || completion.scene->status != "cancelled")) {
                throw std::runtime_error("The first repeated packing run did not preserve cancellation.");
            }
            if (startup_.smoke_action == L"failure-rerun" && (completion.scene || completion.diagnostic.empty())) {
                throw std::runtime_error("The first repeated packing run did not report its input failure.");
            }
            if (startup_.smoke_action == L"failure-rerun") {
                set(object_path, startup_.object.native());
            }
            if (startup_.smoke_action == L"cancel-rerun") {
                set(object_count, L"1");
            }
            command(run_button);
            return;
        }
        if (completion.scene && completion.scene->objects) {
            const HWND rendering_child = FindWindowExW(hwnd_, nullptr, L"vtkOpenGL", nullptr);
            if (!rendering_child) {
                throw std::runtime_error("The native VTK rendering child was not created.");
            }
            constexpr wchar_t exit_keys[] = { L'q', L'Q', L'e', L'E' };
            for (const wchar_t key : exit_keys) {
                SendMessageW(rendering_child, WM_CHAR, static_cast<WPARAM>(key), 1);
                MSG unexpected {};
                if (!IsWindow(hwnd_) || !viewport_->interaction_active() ||
                    PeekMessageW(&unexpected, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE)) {
                    throw std::runtime_error("A viewport shortcut bypassed the application's close policy.");
                }
            }
            viewport_->save_snapshot(startup_.smoke_directory / L"viewport.png");
            RECT viewport_area {};
            GetClientRect(rendering_child, &viewport_area);
            const auto mouse_x = static_cast<WORD>(viewport_area.right / 2);
            const auto mouse_y = static_cast<WORD>(viewport_area.bottom / 2);
            const irop::Point3 before_orbit = viewport_->camera_position();
            SendMessageW(rendering_child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(mouse_x, mouse_y));
            SendMessageW(rendering_child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(mouse_x + 70, mouse_y + 35));
            SendMessageW(rendering_child, WM_LBUTTONUP, 0, MAKELPARAM(mouse_x + 70, mouse_y + 35));
            const irop::Point3 after_orbit = viewport_->camera_position();
            if (before_orbit.x == after_orbit.x && before_orbit.y == after_orbit.y && before_orbit.z == after_orbit.z) {
                throw std::runtime_error("Native mouse drag did not orbit the camera.");
            }
            viewport_->save_snapshot(startup_.smoke_directory / L"orbit.png");
            command(x_button);
            command(y_button);
            command(z_button);
            SendMessageW(control(wireframe), BM_SETCHECK, BST_CHECKED, 0);
            command(wireframe);
            SendMessageW(control(container_visible), BM_SETCHECK, BST_UNCHECKED, 0);
            command(container_visible);
            viewport_->save_snapshot(startup_.smoke_directory / L"wireframe.png");
            SendMessageW(control(wireframe), BM_SETCHECK, BST_UNCHECKED, 0);
            command(wireframe);
            SendMessageW(control(container_visible), BM_SETCHECK, BST_CHECKED, 0);
            command(container_visible);
            command(iso_button);
            command(fit_button);
            RECT frame {};
            GetWindowRect(hwnd_, &frame);
            SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left + px(20), frame.bottom - frame.top + px(20),
                         SWP_NOMOVE | SWP_NOZORDER);
            capture_client(startup_.smoke_directory / L"window.bmp");
        }
        std::ofstream report(startup_.smoke_directory / L"smoke-result.txt");
        report << (completion.scene ? completion.scene->status : completion.cancelled ? "cancelled" : "error") << '\n';
        if (completion.scene) {
            report << completion.scene->object_count << '\n';
        }
        report << completion.diagnostic << '\n';
        report.close();
        smoke_finishing_ = true;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    void poll()
    {
        if (auto completed = job_.take_completion()) {
            run_reservation_.reset();
            refresh_next_output();
            busy(false);
            if (completed->scene) {
                display(*completed->scene);
            }
            else {
                viewport_->clear();
                show_status(completed->cancelled ? L"Cancelled" : L"Unable to complete", wide(completed->diagnostic));
            }
            finish_smoke(*completed);
            if (closing_) {
                close();
            }
            return;
        }
        if (!startup_.smoke_directory.empty() && smoke_started_ && ++smoke_ticks_ > 1200) {
            job_.cancel();
            throw std::runtime_error("UI smoke test exceeded its cooperative deadline.");
        }
        if (job_.active() && !closing_ && IsWindowEnabled(control(cancel_button))) {
            if (auto progress = job_.progress()) {
                std::wostringstream text;
                if (progress->phase == irop::PackingProgressPhase::input_preparation) {
                    text << L"Reading and preparing the input meshes...";
                }
                else if (progress->phase == irop::PackingProgressPhase::initialization_started) {
                    text << L"Generating initial placements for " << progress->object_count << L" objects"
                         << L"\r\nRandom candidate attempt limit: "
                         << progress->initialization_attempt_limit.value_or(0)
                         << L". Geometry and fallback work have separate limits.";
                }
                else if (progress->phase == irop::PackingProgressPhase::finished) {
                    text << L"Writing the run result...";
                }
                else {
                    text << L"Scale step " << progress->scale_step + 1 << L" / " << progress->scale_step_count
                         << L"  |  iteration " << progress->iteration + 1 << L"\r\n"
                         << progress->objects_at_target << L" / " << progress->object_count
                         << L" objects at volume scale " << progress->target_volume_scale;
                    if (progress->object_id) {
                        text << L"\r\nSolving object " << *progress->object_id + 1 << L" / " << progress->object_count
                             << L" | local limit " << progress->local_iteration_limit << L" iterations, "
                             << std::chrono::duration<double>(progress->local_time_limit).count() << L" seconds"
                             << L" | engine budget "
                             << std::chrono::duration<double>(progress->engine_time_limit).count() << L" seconds";
                    }
                    if (progress->phase == irop::PackingProgressPhase::tetrahedralization_recovery) {
                        text << L"\r\nRecovering tetrahedralization...";
                    }
                }
                show_status(L"Packing in progress", text.str());
            }
        }
    }
    void begin()
    {
        if (!startup_.smoke_directory.empty()) {
            if (std::filesystem::exists(startup_.smoke_directory)) {
                throw std::runtime_error("Smoke output directory must be new.");
            }
            std::filesystem::create_directories(startup_.smoke_directory);
            smoke_started_ = true;
            if (startup_.smoke_action == L"open") {
                open(startup_.summary);
            }
            else {
                if (startup_.smoke_action != L"restart") {
                    const auto parent = std::filesystem::absolute(startup_.smoke_directory / L"runs");
                    run_directories_.select_parent(parent);
                    set(output_path, parent.native());
                    refresh_next_output();
                }
                if (startup_.smoke_action == L"failure-rerun") {
                    set(object_path, (startup_.smoke_directory / L"missing.stl").native());
                }
                // Keep cancellation exercises active long enough for the UI
                // click to precede commit; a tiny one-object run can finish
                // while the initial viewport clear repaints on a fast machine.
                const bool cancelling = startup_.smoke_action == L"cancel" ||
                                        startup_.smoke_action == L"close-active" ||
                                        startup_.smoke_action == L"cancel-rerun";
                set(object_count, cancelling ? L"100" : L"1");
                set(initial_scale, L"0.1");
                set(final_scale, L"0.2");
                set(steps, L"1");
                SendMessageW(control(adaptive), BM_SETCHECK, BST_UNCHECKED, 0);
                command(startup_.smoke_action == L"preview" ? preview_button : run_button);
                if (startup_.smoke_action == L"cancel" || startup_.smoke_action == L"close-active" ||
                    startup_.smoke_action == L"cancel-rerun") {
                    command(cancel_button);
                    if (startup_.smoke_action == L"close-active") {
                        close();
                    }
                }
            }
        }
        else if (!startup_.summary.empty()) {
            open(startup_.summary);
        }
    }
    void close()
    {
        if (job_.active()) {
            closing_ = true;
            job_.cancel();
            busy(true);
            show_status(L"Closing after cancellation",
                        L"Waiting for the active geometry or solver call to finish safely...");
            return;
        }
        KillTimer(hwnd_, poll_timer);
        viewport_.reset();
        DestroyWindow(hwnd_);
    }
    void draw_client(const HDC dc)
    {
        RECT client {};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ink);
        HGDIOBJ previous = SelectObject(dc, title_font_);
        TextOutW(dc, px(24), px(17), L"IROP Studio", 11);
        SelectObject(dc, font_);
        SetTextColor(dc, muted);
        constexpr wchar_t subtitle[] = L"IRREGULAR OBJECT PACKING";
        TextOutW(dc, px(24), px(54), subtitle, static_cast<int>(sizeof(subtitle) / sizeof(wchar_t) - 1));
        constexpr wchar_t hint[] = L"Drag to orbit   |   Shift + drag to pan   |   Scroll to zoom";
        TextOutW(dc, px(350), px(38), hint, static_cast<int>(sizeof(hint) / sizeof(wchar_t) - 1));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 230, 236));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        MoveToEx(dc, 0, px(80), nullptr);
        LineTo(dc, client.right, px(80));
        MoveToEx(dc, px(332), px(80), nullptr);
        LineTo(dc, px(332), client.bottom);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        SelectObject(dc, previous);
    }
    void paint() noexcept
    {
        PAINTSTRUCT paint_state {};
        HDC dc = BeginPaint(hwnd_, &paint_state);
        draw_client(dc);
        EndPaint(hwnd_, &paint_state);
    }
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM word, LPARAM data) noexcept
    {
        Studio* app = reinterpret_cast<Studio*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<Studio*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
            app->hwnd_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) {
            return DefWindowProcW(window, message, word, data);
        }
        try {
            switch (message) {
            case WM_CREATE:
                app->initialize();
                return 0;
            case WM_APP + 1:
                app->begin();
                return 0;
            case WM_COMMAND:
                if (HIWORD(word) == BN_CLICKED) {
                    app->command(LOWORD(word));
                }
                return 0;
            case WM_TIMER:
                if (word == poll_timer) {
                    app->poll();
                }
                return 0;
            case WM_SIZE:
                app->layout();
                return 0;
            case WM_GETMINMAXINFO: {
                auto* bounds = reinterpret_cast<MINMAXINFO*>(data);
                bounds->ptMinTrackSize = { app->px(1080), app->px(890) };
                return 0;
            }
            case WM_PAINT:
                app->paint();
                return 0;
            case WM_PRINTCLIENT:
                app->draw_client(reinterpret_cast<HDC>(word));
                return 0;
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(word), ink);
                SetBkColor(reinterpret_cast<HDC>(word), RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
            case WM_CLOSE:
                app->close();
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, word, data);
            }
        }
        catch (const std::exception& error) {
            try {
                app->show_status(L"Unable to complete", wide(error.what()));
                if (!app->startup_.smoke_directory.empty() && !app->smoke_finishing_) {
                    std::ofstream report(app->startup_.smoke_directory / L"smoke-error.txt");
                    report << error.what();
                    app->smoke_finishing_ = true;
                    app->close();
                }
            }
            catch (...) {
                MessageBoxW(window, L"Studio could not complete the operation.", L"IROP Studio", MB_OK | MB_ICONERROR);
            }
            if (message == WM_CREATE) {
                app->viewport_.reset();
            }
            return message == WM_CREATE ? -1 : 0;
        }
        catch (...) {
            MessageBoxW(window, L"Unexpected Studio failure.", L"IROP Studio", MB_OK | MB_ICONERROR);
            if (message == WM_CREATE) {
                app->viewport_.reset();
            }
            return message == WM_CREATE ? -1 : 0;
        }
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    try {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(com)) {
            throw std::runtime_error("Cannot initialize Windows file dialogs.");
        }
        INITCOMMONCONTROLSEX controls { sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&controls);
        Studio app(parse_startup());
        if (!app.create(instance, show)) {
            CoUninitialize();
            return 2;
        }
        MSG message {};
        BOOL fetched = 0;
        while ((fetched = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(app.window(), &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        CoUninitialize();
        return fetched == -1 ? 2 : static_cast<int>(message.wParam);
    }
    catch (const std::exception& error) {
        try {
            MessageBoxW(nullptr, wide(error.what()).c_str(), L"IROP Studio", MB_OK | MB_ICONERROR);
        }
        catch (...) {
            MessageBoxW(nullptr, L"Studio failed to start.", L"IROP Studio", MB_OK | MB_ICONERROR);
        }
        return 2;
    }
    catch (...) {
        MessageBoxW(nullptr, L"Studio failed to start.", L"IROP Studio", MB_OK | MB_ICONERROR);
        return 2;
    }
}
