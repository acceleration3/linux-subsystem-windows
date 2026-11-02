#pragma once

namespace lsw::agent::system {

class DriverLogPruner {
public:
    DriverLogPruner() = delete;

    static constexpr int kKeep = 50;

    static void run();
};

}
