#pragma once

#include <string>

namespace lsw::suites {
class Glob {
public:
    [[nodiscard]] static bool match(const std::string& pattern, const std::string& text);
};
}  // namespace lsw::suites
