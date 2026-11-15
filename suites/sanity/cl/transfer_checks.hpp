#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class TransferChecks {
public:
    [[nodiscard]] static Outcome read_write_blocking_nonblocking(Context& context);

    [[nodiscard]] static Outcome rect_and_fill_copy(Context& context);

    [[nodiscard]] static Outcome map_unmap_ranges(Context& context);

    [[nodiscard]] static Outcome migrate_mem_objects(Context& context);
};
}  // namespace lsw::suites::sanity::cl
