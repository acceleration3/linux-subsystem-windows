#pragma once

#include "core/check_result.hpp"
#include "core/context.hpp"
#include "core/outcome.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
class RobustChild {
public:
    using Role = std::function<Outcome(Context&, const std::string&)>;

    [[nodiscard]] static std::string role();

    [[nodiscard]] static Outcome child(Context& context, const Role& specific);

    static void ready();

    static void forward(Context& context, const std::string& prefix, const CheckResult& result);

    [[nodiscard]] static CheckResult run(const std::string& id, const std::string& role,
                                         const std::vector<std::pair<std::string, std::string>>& env,
                                         int timeout_seconds, int kill_after_ready_ms);
};
}  // namespace lsw::suites
