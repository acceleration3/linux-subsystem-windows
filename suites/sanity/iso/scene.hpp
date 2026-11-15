#pragma once

#include "core/context.hpp"
#include "sanity/iso/fleet.hpp"
#include "support/problems.hpp"

#include <string>
#include <utility>
#include <vector>

namespace lsw::suites::sanity::iso {
class Scene {
public:
    [[nodiscard]] static std::vector<std::string> apis();

    [[nodiscard]] static std::vector<std::pair<std::string, std::string>> seconds(double value);

    static void judge(Problems& problems, const Fleet& fleet, const std::string& what, bool must_run = true);

    static void forward(Context& context, const Fleet& fleet, const std::string& prefix);

    [[nodiscard]] static bool roundtrip(Context& context, const std::string& api, std::uint64_t size, std::uint64_t seed, double& ms, std::string& error);

    static void sleep(int milliseconds);
};
}  // namespace lsw::suites::sanity::iso
