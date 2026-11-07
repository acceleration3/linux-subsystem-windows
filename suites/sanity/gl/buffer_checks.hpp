#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class BufferChecks {
public:
    [[nodiscard]] static Outcome storage_flag_matrix(Context& context);

    [[nodiscard]] static Outcome persistent_coherent_gpu_write(Context& context);

    [[nodiscard]] static Outcome persistent_noncoherent_flush(Context& context);

    [[nodiscard]] static Outcome dynamic_storage_subdata(Context& context);

    [[nodiscard]] static Outcome map_range_flags(Context& context);

    [[nodiscard]] static Outcome map_errors(Context& context);

    [[nodiscard]] static Outcome copy_and_clear(Context& context);

    [[nodiscard]] static Outcome pbo_pack_unpack(Context& context);
};
}  // namespace lsw::suites::sanity::gl
