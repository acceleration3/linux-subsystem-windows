#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::data {
class SequenceChecks {
public:
    [[nodiscard]] static Outcome back_to_back_10000(Context& context);

    [[nodiscard]] static Outcome interleaved_small_large(Context& context);

    [[nodiscard]] static Outcome overwrite_ordering(Context& context);

    [[nodiscard]] static Outcome read_after_write_same_context(Context& context);

    [[nodiscard]] static Outcome many_contexts(Context& context);

    [[nodiscard]] static Outcome many_devices_one_process(Context& context);

    [[nodiscard]] static Outcome mixed_api_chain(Context& context);
};
}  // namespace lsw::suites::sanity::data
