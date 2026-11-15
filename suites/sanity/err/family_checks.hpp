#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class FamilyChecks {
public:
    [[nodiscard]] static Outcome vk_wrong_queue_family(Context& context);
};
}  // namespace lsw::suites::sanity::err
