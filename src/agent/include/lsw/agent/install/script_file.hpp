#pragma once

#include <string>

namespace lsw::agent::install {

// A temporary .cmd file, written on construction and deleted on
// destruction.
//
// The CRLF conversion is not cosmetic: cmd.exe mis-parses multi-line
// ``if`` and ``for`` blocks that arrive with bare LFs, so a script
// authored on the host would fail in ways that look like logic errors.
class ScriptFile {
public:
    // ``body`` is normalised to CRLF and written to a work-dir path
    // derived from ``job_id`` + ``suffix``.
    ScriptFile(const std::string& job_id, const std::wstring& suffix,
               const std::string& body);
    ~ScriptFile();

    ScriptFile(const ScriptFile&) = delete;
    ScriptFile& operator=(const ScriptFile&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    // Why the write failed. Empty when ``ok()``.
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

    // ``cmd.exe /c "<path>"`` — what ProcessRunner should be handed.
    [[nodiscard]] std::wstring command_line() const;

private:
    std::wstring path_;
    bool ok_ = false;
    std::string error_;
};

}  // namespace lsw::agent::install
