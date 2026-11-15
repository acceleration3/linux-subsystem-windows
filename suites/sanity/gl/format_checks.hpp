#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class FormatChecks {
public:
    [[nodiscard]] static Outcome enumerate_choosable(Context& context);

    [[nodiscard]] static Outcome every_format_renders(Context& context);

    [[nodiscard]] static Outcome choose_matches_request(Context& context);

    [[nodiscard]] static Outcome arb_attribs_agree_with_describe(Context& context);

    [[nodiscard]] static Outcome set_twice_fails(Context& context);

    [[nodiscard]] static Outcome srgb_and_float(Context& context);
};
}  // namespace lsw::suites::sanity::gl
