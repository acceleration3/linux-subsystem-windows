#pragma once

#include <string>

namespace lsw::agent::core {

class StartupHeader {
public:
    StartupHeader() = delete;

    static void write(const std::string& role);
};

}
