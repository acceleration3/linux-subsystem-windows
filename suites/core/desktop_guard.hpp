#pragma once

#include <string>
#include <vector>

namespace lsw::suites {
class DesktopGuard {
public:
    DesktopGuard();

    [[nodiscard]] std::vector<std::string> sweep() const;

    [[nodiscard]] static bool foreign(const std::string& image_path, const std::string& own_directory);

private:
    std::string own_directory_;
};
}  // namespace lsw::suites
