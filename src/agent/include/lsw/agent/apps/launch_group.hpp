#pragma once

#include "lsw/agent/apps/launch_identity.hpp"

#include <string>

namespace lsw::agent::apps {
class LaunchGroup {
public:
    LaunchGroup() = delete;

    static bool adopt(void* process, unsigned long pid, LaunchIdentity identity);

    [[nodiscard]] static std::string job_name(unsigned long pid);

    [[nodiscard]] static unsigned long root_of(unsigned long pid);
};

}
