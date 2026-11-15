#pragma once

#include <string>
#include <vector>

namespace lsw::suites {
class CheckFilter {
public:
    CheckFilter(const std::string& globs, const std::string& tags);

    [[nodiscard]] bool accepts(const std::string& id, const std::vector<std::string>& tags) const;

    [[nodiscard]] static std::vector<std::string> split(const std::string& list);

private:
    std::vector<std::string> include_;
    std::vector<std::string> exclude_;
    std::vector<std::string> tags_;
};
}  // namespace lsw::suites
