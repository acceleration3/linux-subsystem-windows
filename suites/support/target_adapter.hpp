#pragma once

#include "support/av_adapters.hpp"

#include <string>

namespace lsw::suites {
class TargetAdapter {
public:
    [[nodiscard]] static bool pick(AvAdapter& out, std::string& error);
};
}  // namespace lsw::suites
