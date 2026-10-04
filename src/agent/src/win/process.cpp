#include "lsw/agent/win/process.hpp"

#include <algorithm>

namespace lsw::agent::win {

namespace {

// The attribute list restricting inheritance to ``handles``. Owns its
// buffer; the handles themselves are borrowed and must outlive the
// CreateProcess call.
class HandleList {
public:
    explicit HandleList(std::vector<HANDLE> handles) : handles_(std::move(handles)) {
        SIZE_T bytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        buffer_.resize(bytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());
        if (::InitializeProcThreadAttributeList(list, 1, 0, &bytes) == FALSE) {
            return;
        }
        initialised_ = true;
        ok_ = ::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          handles_.data(), handles_.size() * sizeof(HANDLE),
                                          nullptr, nullptr) != FALSE;
    }

    ~HandleList() {
        if (initialised_) {
            ::DeleteProcThreadAttributeList(get());
        }
    }

    HandleList(const HandleList&) = delete;
    HandleList& operator=(const HandleList&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept {
        return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());
    }

private:
    std::vector<HANDLE> handles_;
    std::vector<unsigned char> buffer_;
    bool initialised_ = false;
    bool ok_ = false;
};

}  // namespace

std::optional<SpawnedProcess> Process::spawn(const SpawnOptions& options, DWORD* error_out) {
    STARTUPINFOEXW startup_ex{};
    STARTUPINFOW& startup = startup_ex.StartupInfo;
    startup.cb = sizeof(startup);
    DWORD flags = options.creation_flags;

    std::vector<HANDLE> std_handles;
    if (options.std_input != nullptr || options.std_output != nullptr ||
        options.std_error != nullptr) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = options.std_input;
        startup.hStdOutput = options.std_output;
        startup.hStdError = options.std_error;
        for (HANDLE h : {options.std_input, options.std_output, options.std_error}) {
            // The list may not name a handle twice (stdout and stderr are
            // usually the same pipe) or name a null one.
            if (h != nullptr && h != INVALID_HANDLE_VALUE &&
                std::find(std_handles.begin(), std_handles.end(), h) == std_handles.end()) {
                std_handles.push_back(h);
            }
        }
    }
    if (options.hide_window) {
        startup.dwFlags |= STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }

    // lpDesktop is an LPWSTR; give it a buffer of its own.
    std::vector<wchar_t> desktop(options.desktop.begin(), options.desktop.end());
    desktop.push_back(L'\0');
    if (!options.desktop.empty()) {
        startup.lpDesktop = desktop.data();
    }

    std::optional<HandleList> handle_list;
    if (options.inherit_handles && !std_handles.empty()) {
        handle_list.emplace(std_handles);
        if (!handle_list->ok()) {
            if (error_out != nullptr) {
                *error_out = ::GetLastError();
            }
            return std::nullopt;
        }
        startup_ex.lpAttributeList = handle_list->get();
        startup.cb = sizeof(startup_ex);
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    if (!options.environment.empty()) {
        flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    // CreateProcess*W writes through the command-line buffer, so it must
    // be a mutable copy.
    std::vector<wchar_t> command(options.command_line.begin(), options.command_line.end());
    command.push_back(L'\0');

    const wchar_t* application =
        options.application.empty() ? nullptr : options.application.c_str();
    // Only read, whatever the parameter's constness says.
    void* environment =
        options.environment.empty() ? nullptr : const_cast<wchar_t*>(options.environment.data());
    const wchar_t* working_dir =
        options.working_dir.empty() ? nullptr : options.working_dir.c_str();
    const BOOL inherit = options.inherit_handles ? TRUE : FALSE;

    PROCESS_INFORMATION spawned{};
    const BOOL ok =
        options.token != nullptr
            ? ::CreateProcessAsUserW(options.token, application, command.data(), nullptr, nullptr,
                                     inherit, flags, environment, working_dir, &startup, &spawned)
            : ::CreateProcessW(application, command.data(), nullptr, nullptr, inherit, flags,
                               environment, working_dir, &startup, &spawned);
    if (ok == 0) {
        if (error_out != nullptr) {
            *error_out = ::GetLastError();
        }
        return std::nullopt;
    }
    if (error_out != nullptr) {
        *error_out = ERROR_SUCCESS;
    }
    SpawnedProcess out;
    out.process.reset(spawned.hProcess);
    out.thread.reset(spawned.hThread);
    out.pid = spawned.dwProcessId;
    return out;
}

}  // namespace lsw::agent::win
