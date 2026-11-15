#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GlClChecks {
public:
    [[nodiscard]] static Outcome context_device_match(Context& context);

    [[nodiscard]] static Outcome buffer_sharing(Context& context);

    [[nodiscard]] static Outcome texture_targets_mips_layers(Context& context);

    [[nodiscard]] static Outcome renderbuffer_sharing(Context& context);

    [[nodiscard]] static Outcome gl_event_sync(Context& context);

    [[nodiscard]] static Outcome acquire_release_errors(Context& context);

    [[nodiscard]] static Outcome caps_unsupported(Context& context);
};
}  // namespace lsw::suites::sanity::interop
