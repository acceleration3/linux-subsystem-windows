#include "lsw/agent/system/volume_grower.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/powershell_command.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace lsw::agent::system {

namespace {
constexpr unsigned long kTimeoutMs = 5UL * 60UL * 1000UL;

constexpr const char* kScript = R"PS(
$ErrorActionPreference = 'Stop'
& {
    try {
        Update-HostStorageCache -ErrorAction SilentlyContinue
        $p = Get-Partition -DriveLetter C
        $max = (Get-PartitionSupportedSize -DriveLetter C).SizeMax
        if ($max -gt $p.Size + 1MB) {
            Resize-Partition -DriveLetter C -Size $max
            "grew C: from $($p.Size) to $max bytes"
        } else {
            "nothing to do: C: is $($p.Size) bytes, the most it can be is $max"
        }
    } catch {
        "FAILED: $($_.Exception.Message)"
    }
} *>&1 | Out-File -Encoding ascii -FilePath '__LOG__'
)PS";

void grow() {
    std::error_code ignored;
    std::filesystem::create_directories(core::Paths::logs_dir(), ignored);
    const std::wstring log_path = core::Paths::logs_dir() + L"\\disk-grow.log";
    std::string script = kScript;
    const std::string needle = "__LOG__";
    script.replace(script.find(needle), needle.size(), util::TextEncoding::to_utf8(log_path));

    const std::wstring command = util::PowerShellCommand::encoded(script);
    const auto outcome = install::ProcessRunner::run(command, kTimeoutMs);
    if (!outcome.completed) {
        LSW_AGENT_WARN("disk: growing C: did not finish: {}", outcome.error);
        return;
    }

    std::ifstream file(std::filesystem::path{log_path});
    std::string line;
    bool said_anything = false;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        said_anything = true;
        if (line.rfind("FAILED", 0) == 0) {
            LSW_AGENT_WARN("disk: {}", line);
        } else {
            LSW_AGENT_INFO("disk: {}", line);
        }
    }
    if (!said_anything) {
        LSW_AGENT_WARN("disk: the resize script said nothing (exit code {})", outcome.exit_code);
    }
}

}

void VolumeGrower::start() {
    std::thread{[] { grow(); }}.detach();
}

}
