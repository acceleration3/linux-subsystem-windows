#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <functional>
#include <vector>

namespace lsw::suites::sanity::err {
class Delegate {
public:
    using Part = std::function<Outcome(Context&)>;

    [[nodiscard]] static Outcome all(Context& context, const std::vector<Part>& parts);
};
}  // namespace lsw::suites::sanity::err
