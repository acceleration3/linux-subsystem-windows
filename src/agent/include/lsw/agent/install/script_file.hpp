#pragma once

#include <string>

namespace lsw::agent::install {
class ScriptFile {
public:
    ScriptFile(const std::string& job_id, const std::wstring& suffix,
               const std::string& body);
    ~ScriptFile();

    ScriptFile(const ScriptFile&) = delete;
    ScriptFile& operator=(const ScriptFile&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

    [[nodiscard]] std::wstring command_line() const;

private:
    std::wstring path_;
    bool ok_ = false;
    std::string error_;
};

}
