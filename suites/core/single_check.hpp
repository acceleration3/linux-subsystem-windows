#pragma once

#include "core/registry.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites {
class SingleCheck {
public:
    SingleCheck(const Registry& registry, std::filesystem::path artifacts);

    [[nodiscard]] int run(const std::string& id) const;

private:
    const Registry& registry_;
    std::filesystem::path artifacts_;
};
}  // namespace lsw::suites
