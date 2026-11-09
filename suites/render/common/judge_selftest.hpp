#pragma once

#include <string>
#include <vector>

namespace lsw::render {
class JudgeSelfTest {
public:
    [[nodiscard]] static std::vector<std::string> run(std::vector<std::string>& passed);
};
}  // namespace lsw::render
