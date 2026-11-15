#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class TransferChecks {
public:
    [[nodiscard]] static Outcome copy_all_forms(Context& context);

    [[nodiscard]] static Outcome blit_resolve(Context& context);

    [[nodiscard]] static Outcome clear_fill_update(Context& context);
};
}  // namespace lsw::suites::sanity::vk
