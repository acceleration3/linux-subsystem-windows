#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lsw::suites::sanity::sync {
class ApiChildren {
public:
    using Body = std::function<Outcome(Context&, const std::string&)>;

    [[nodiscard]] static Outcome run(Context& context, const std::vector<std::string>& apis, const Body& body,
                                     int timeout_seconds);
};
}  // namespace lsw::suites::sanity::sync
