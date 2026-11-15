#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class PathChecks {
public:
    [[nodiscard]] static Outcome d3d9_blt(Context& context);

    [[nodiscard]] static Outcome d3d9ex_flip(Context& context);

    [[nodiscard]] static Outcome dxgi_blt_sequential(Context& context);

    [[nodiscard]] static Outcome dxgi_blt_discard(Context& context);

    [[nodiscard]] static Outcome dxgi_flip_sequential(Context& context);

    [[nodiscard]] static Outcome dxgi_flip_discard(Context& context);

    [[nodiscard]] static Outcome d3d12_flip(Context& context);

    [[nodiscard]] static Outcome gl_swapbuffers(Context& context);

    [[nodiscard]] static Outcome vk_fifo(Context& context);

    [[nodiscard]] static Outcome vk_immediate_mailbox(Context& context);
};
}  // namespace lsw::suites::sanity::present
