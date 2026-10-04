#include "lsw/agent/install/script_file.hpp"

#include "lsw/agent/install/work_dir.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <windows.h>

namespace lsw::agent::install {

namespace {

// cmd.exe mis-parses multi-line if/for blocks that arrive with bare LFs.
[[nodiscard]] std::string to_crlf(const std::string& body) {
    std::string out;
    out.reserve(body.size() + 8);
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\n' && (i == 0 || body[i - 1] != '\r')) {
            out.push_back('\r');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

ScriptFile::ScriptFile(const std::string& job_id, const std::wstring& suffix,
                       const std::string& body)
    : path_(WorkDir::staged_path(job_id, suffix)) {
    const std::string crlf = to_crlf(body);
    const win::UniqueHandle file{::CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr,
                                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        const std::string err = win::ErrorText::last_error();
        error_ = "CreateFileW(" + util::TextEncoding::to_utf8(path_) + ") failed err=" + err;
        return;
    }
    DWORD wrote = 0;
    ::WriteFile(file.get(), crlf.data(), static_cast<DWORD>(crlf.size()), &wrote, nullptr);
    ok_ = true;
}

ScriptFile::~ScriptFile() {
    if (!path_.empty()) {
        ::DeleteFileW(path_.c_str());
    }
}

std::wstring ScriptFile::command_line() const {
    return L"cmd.exe /c \"" + path_ + L"\"";
}

}  // namespace lsw::agent::install
