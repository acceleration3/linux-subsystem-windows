#pragma once

#include <string>

namespace lsw::agent::apps {
struct LaunchIdentity {
    std::string app_id;
    std::wstring image;
    std::wstring working_dir;
    std::wstring path_env;
};

}
