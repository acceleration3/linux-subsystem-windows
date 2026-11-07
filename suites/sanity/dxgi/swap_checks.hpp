#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::dxgi {
class SwapChecks {
public:
    [[nodiscard]] static Outcome model_matrix(Context& context);

    [[nodiscard]] static Outcome buffer_contents(Context& context);

    [[nodiscard]] static Outcome present1(Context& context);

    [[nodiscard]] static Outcome allow_tearing(Context& context);

    [[nodiscard]] static Outcome do_not_wait(Context& context);

    [[nodiscard]] static Outcome waitable_latency(Context& context);

    [[nodiscard]] static Outcome frame_statistics(Context& context);

    [[nodiscard]] static Outcome resize_buffers(Context& context);

    [[nodiscard]] static Outcome fullscreen_transition(Context& context);

    [[nodiscard]] static Outcome hdr_colorspace(Context& context);

    [[nodiscard]] static Outcome getdc(Context& context);

    [[nodiscard]] static Outcome background_alpha_composition(Context& context);

    [[nodiscard]] static Outcome multiple_and_orphan(Context& context);
};
}  // namespace lsw::suites::sanity::dxgi
