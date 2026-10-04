#include "lsw/agent/win/mirror_process.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/run_as.hpp"

#include <utility>
#include <vector>

namespace lsw::agent::win {

namespace {

// QEMU's user networking puts the host at the gateway address, with no
// forwarding rule needed for a connection the guest opens itself.
constexpr const char* kHostAddress = "10.0.2.2";

void set_error(std::string* into, std::string text) {
    if (into != nullptr) {
        *into = std::move(text);
    }
}

}  // namespace

unsigned long MirrorProcess::start(const std::string& exe, std::uint16_t port,
                                   const std::string& log_path, const RunAs& as,
                                   std::string* error_out) {
    if (exe.empty() || port == 0) {
        set_error(error_out, "start_window_mirror: an executable and a port are required");
        return 0;
    }

    const std::wstring path = util::TextEncoding::to_wide(exe);
    if (!FileSystem::exists(path)) {
        set_error(error_out, "start_window_mirror: no such file: " + exe);
        return 0;
    }

    SpawnOptions options;
    options.application = path;
    options.command_line = Process::quote(path) + L" --host " +
                           util::TextEncoding::to_wide(kHostAddress) + L" --port " +
                           std::to_wstring(port);
    // CREATE_NO_WINDOW: a console program with nobody to read it.
    options.creation_flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
    // Where the mirror says what happened. Its output would otherwise go
    // to a console it was given none of.
    std::vector<Environment::Override> overrides;
    if (!log_path.empty()) {
        overrides.emplace_back("RWIN_LOG_FILE", log_path);
    }
    try {
        options.environment = as.environment(overrides);
        as.apply(options);
    } catch (const std::exception& e) {
        set_error(error_out, std::string{"start_window_mirror: cannot start it as "} +
                                 as.describe() + ": " + e.what());
        return 0;
    }

    DWORD error = 0;
    const auto spawned = Process::spawn(options, &error);
    if (!spawned) {
        set_error(error_out, "start_window_mirror: CreateProcess as " + as.describe() +
                                 " failed (err=" + ErrorText::win32(error) + ")");
        return 0;
    }
    LSW_AGENT_INFO("start_window_mirror: pid={} port={} as {}", spawned->pid, port,
                   as.describe());
    return spawned->pid;
}

}  // namespace lsw::agent::win
