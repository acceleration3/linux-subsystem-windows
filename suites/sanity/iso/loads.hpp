#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <string>

namespace lsw::suites::sanity::iso {
class Loads {
public:
    static constexpr const char* kSeconds = "LSW_SUITES_ISO_SECONDS";

    [[nodiscard]] static Outcome role(Context& context, const std::string& name);

    [[nodiscard]] static std::string name(const std::string& kind, const std::string& api) { return kind + "-" + api; }

    [[nodiscard]] static double seconds();
};
}  // namespace lsw::suites::sanity::iso
