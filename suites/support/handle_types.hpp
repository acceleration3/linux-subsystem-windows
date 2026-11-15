#pragma once

#include <map>
#include <string>

namespace lsw::suites {
class HandleTypes {
public:
    [[nodiscard]] static std::map<std::string, long long> count();

    [[nodiscard]] static std::string grown(const std::map<std::string, long long>& before,
                                            const std::map<std::string, long long>& after);
};
}  // namespace lsw::suites
