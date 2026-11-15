#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::memory {
class BudgetChecks {
public:
    [[nodiscard]] static Outcome dxgi_moves_with_alloc(Context& context);

    [[nodiscard]] static Outcome vulkan_budget_moves(Context& context);

    [[nodiscard]] static Outcome reservation_honoured(Context& context);

    [[nodiscard]] static Outcome opencl_free_memory(Context& context);
};
}  // namespace lsw::suites::sanity::memory
