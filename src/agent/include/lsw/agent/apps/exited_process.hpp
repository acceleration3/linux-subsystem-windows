#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::apps {
struct ExitedProcess {
    unsigned long key = 0;
    unsigned long pid = 0;
    unsigned long parent = 0;
    std::uint64_t created = 0;
    std::uint32_t exit_code = 0;
    std::wstring image;
    std::string app_id;
    std::wstring working_dir;
    std::wstring path_env;
};

}
