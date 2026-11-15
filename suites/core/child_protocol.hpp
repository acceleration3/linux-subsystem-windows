#pragma once

#include "core/check_result.hpp"
#include "core/context.hpp"
#include "core/outcome.hpp"

#include <string>

namespace lsw::suites {
class ChildProtocol {
public:
    [[nodiscard]] static std::string encode(const Context& context, const Outcome& outcome);

    static void phase(const std::string& text);

    [[nodiscard]] static bool decode(const std::string& output, CheckResult& result);
};
}  // namespace lsw::suites
