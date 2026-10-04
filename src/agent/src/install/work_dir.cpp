#include "lsw/agent/install/work_dir.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

namespace lsw::agent::install {

std::wstring WorkDir::path() {
    ::CreateDirectoryW(core::Paths::root().c_str(), nullptr);
    const std::wstring work = core::Paths::work_dir();
    ::CreateDirectoryW(work.c_str(), nullptr);
    return work + L"\\";
}

std::wstring WorkDir::staged_path(const std::string& job_id, const std::wstring& suffix) {
    return path() + L"lsw-" + util::TextEncoding::to_wide(job_id) + suffix;
}

}  // namespace lsw::agent::install
