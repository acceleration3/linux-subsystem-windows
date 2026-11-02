#include "lsw/agent/system/log_packer.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/powershell_command.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace lsw::agent::system {

namespace {

constexpr unsigned long kTimeoutMs = 2UL * 60UL * 1000UL;

constexpr const char* kScript = R"PS(
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipPath = '__ZIP__'
$skipped = New-Object System.Collections.Generic.List[string]

function Add-Source($Archive, $Source, $Name, $TailBytes) {
    try {
        $reader = [System.IO.File]::Open($Source, 'Open', 'Read', 'ReadWrite, Delete')
        try {
            if ($TailBytes -gt 0 -and $reader.Length -gt $TailBytes) {
                $null = $reader.Seek(-$TailBytes, 'End')
            }
            $member = $Archive.CreateEntry($Name, 'Optimal')
            $writer = $member.Open()
            try { $reader.CopyTo($writer) } finally { $writer.Dispose() }
        } finally { $reader.Dispose() }
    } catch {
        $skipped.Add("$Source : $($_.Exception.Message)")
    }
}

try {
    Get-ChildItem -LiteralPath (Split-Path $zipPath) -Filter 'logs-*.zip' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    $zipFile = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        $logsRoot = '__LOGS__'
        Get-ChildItem -LiteralPath $logsRoot -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
            $relative = $_.FullName.Substring($logsRoot.Length).TrimStart('\').Replace('\', '/')
            Add-Source $zipFile $_.FullName ('logs/' + $relative) 0
        }
        foreach ($leaf in 'release.json', 'update-result.json') {
            $file = Join-Path '__ROOT__' $leaf
            if (Test-Path -LiteralPath $file) { Add-Source $zipFile $file $leaf 0 }
        }
        Get-ChildItem -LiteralPath '__AV__' -Filter '*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 50 | ForEach-Object {
                Add-Source $zipFile $_.FullName ('av/' + $_.Name) 0
            }
        foreach ($leaf in 'setupact.log', 'setuperr.log') {
            $file = Join-Path 'C:\Windows\Panther' $leaf
            if (Test-Path -LiteralPath $file) { Add-Source $zipFile $file ('panther/' + $leaf) 0 }
        }
        $setupapi = 'C:\Windows\INF\setupapi.dev.log'
        if (Test-Path -LiteralPath $setupapi) { Add-Source $zipFile $setupapi 'setupapi.dev.log' (2MB) }
        $werRoot = 'C:\ProgramData\Microsoft\Windows\WER\ReportArchive'
        Get-ChildItem -LiteralPath $werRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'lsw-agent|remotewin-guest' } | ForEach-Object {
                $report = Join-Path $_.FullName 'Report.wer'
                if (Test-Path -LiteralPath $report) { Add-Source $zipFile $report ('wer/' + $_.Name + '/Report.wer') 0 }
            }
        if ($skipped.Count -gt 0) {
            $member = $zipFile.CreateEntry('pack-notes.txt')
            $writer = New-Object System.IO.StreamWriter($member.Open())
            try { $writer.Write("Not included:`n" + ($skipped -join "`n") + "`n") } finally { $writer.Dispose() }
        }
    } finally { $zipFile.Dispose() }
} catch {
    Set-Content -LiteralPath ($zipPath + '.err') -Value $_.Exception.Message
    Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    exit 1
}
)PS";

std::string stamp() {
    SYSTEMTIME t{};
    ::GetLocalTime(&t);
    char text[32];
    std::snprintf(text, sizeof(text), "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay,
                  t.wHour, t.wMinute, t.wSecond);
    return text;
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

}

nlohmann::json LogPacker::pack() {
    const std::filesystem::path work{core::Paths::work_dir()};
    std::error_code ignored;
    std::filesystem::create_directories(work, ignored);
    const std::filesystem::path zip = work / ("logs-" + stamp() + ".zip");
    const auto utf8 = [](const std::wstring& w) { return util::TextEncoding::to_utf8(w); };

    std::string script = kScript;
    replace_all(script, "__ZIP__", zip.string());
    replace_all(script, "__LOGS__", utf8(core::Paths::logs_dir()));
    replace_all(script, "__ROOT__", utf8(core::Paths::root()));
    replace_all(script, "__AV__", utf8(core::Paths::driver_logs_dir()));

    const auto outcome =
        install::ProcessRunner::run(util::PowerShellCommand::encoded(script), kTimeoutMs);
    if (!outcome.completed || outcome.exit_code != 0) {
        std::string reason = outcome.completed ? "exit code " + std::to_string(outcome.exit_code)
                                               : outcome.error;
        std::ifstream err{std::filesystem::path{zip}.concat(".err")};
        std::stringstream said;
        said << err.rdbuf();
        if (!said.str().empty()) {
            reason = said.str();
        }
        std::filesystem::remove(std::filesystem::path{zip}.concat(".err"), ignored);
        throw std::runtime_error("pack_logs: " + reason);
    }
    const auto size = std::filesystem::file_size(zip, ignored);
    if (ignored) {
        throw std::runtime_error("pack_logs: the archive " + zip.string() + " was not written");
    }
    LSW_AGENT_INFO("pack_logs: {} ({} bytes)", zip.string(), size);
    return {{"path", zip.string()}, {"size", size}};
}

}
