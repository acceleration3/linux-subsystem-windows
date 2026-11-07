#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class MemChecks {
public:
    [[nodiscard]] static Outcome use_host_ptr_coherence(Context& context);

    [[nodiscard]] static Outcome use_host_ptr_blocking_ops(Context& context);

    [[nodiscard]] static Outcome alloc_host_ptr(Context& context);

    [[nodiscard]] static Outcome copy_host_ptr(Context& context);

    [[nodiscard]] static Outcome access_flags_honoured(Context& context);

    [[nodiscard]] static Outcome sub_buffer_alignment(Context& context);
};
}  // namespace lsw::suites::sanity::cl
