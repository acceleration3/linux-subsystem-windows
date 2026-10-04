#include "lsw/agent/system/volume_grower.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/base64.hpp"
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

// A resize of a large volume is quick (NTFS extends in place), but a
// wedged storage stack must not leave a PowerShell behind forever.
constexpr unsigned long kTimeoutMs = 5UL * 60UL * 1000UL;

// The script. ``__LOG__`` is replaced with where its output goes. 1MB of
// slack so a partition that is a few sectors short of the maximum (the
// alignment the cmdlet reports) is not resized over and over.
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

// -EncodedCommand takes base64 of UTF-16LE, which spares the script any
// quoting through cmd-line parsing.
std::string encode_for_powershell(const std::wstring& script) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(script.size() * 2);
    for (const wchar_t c : script) {
        bytes.push_back(static_cast<std::uint8_t>(c & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    }
    return util::Base64::encode(bytes);
}

void grow() {
    const std::wstring log_path = core::Paths::root() + L"\\disk-grow.log";
    std::string script = kScript;
    const std::string needle = "__LOG__";
    script.replace(script.find(needle), needle.size(), util::TextEncoding::to_utf8(log_path));

    const std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden "
        L"-EncodedCommand " +
        util::TextEncoding::to_wide(encode_for_powershell(util::TextEncoding::to_wide(script)));
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

}  // namespace

void VolumeGrower::start() {
    std::thread{[] { grow(); }}.detach();
}

}  // namespace lsw::agent::system
