#pragma once

#include <string>
#include <vector>

namespace lsw::suites {
class ApiPolicy {
public:
    static constexpr const char* kVariable = "LSW_SUITES_SKIP_APIS";

    [[nodiscard]] static std::vector<std::string> skipped();

    [[nodiscard]] static bool is_skipped(const std::string& api);

    [[nodiscard]] static std::vector<std::string> filter(const std::vector<std::string>& apis);

    [[nodiscard]] static std::vector<std::string> known();
};
}  // namespace lsw::suites
