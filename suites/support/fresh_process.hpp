#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "support/problems.hpp"

#include <string>

namespace lsw::suites {
class FreshProcess {
public:
    static void check(Context& context, Problems& problems, const std::string& id, const std::string& label);

    [[nodiscard]] static Outcome child(Context& context);
};
}  // namespace lsw::suites
