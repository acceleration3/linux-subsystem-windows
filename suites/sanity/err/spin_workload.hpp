#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <string>

namespace lsw::suites::sanity::err {
class SpinWorkload {
public:
    [[nodiscard]] static Outcome run(Context& context, const std::string& api, bool forever);
};
}  // namespace lsw::suites::sanity::err
