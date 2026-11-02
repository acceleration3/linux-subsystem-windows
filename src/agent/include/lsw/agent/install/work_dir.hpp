#pragma once

#include <string>

namespace lsw::agent::install {
class WorkDir {
public:
    WorkDir() = delete;

    [[nodiscard]] static std::wstring path();

    [[nodiscard]] static std::wstring staged_path(const std::string& job_id,
                                                  const std::wstring& suffix);
};

}
