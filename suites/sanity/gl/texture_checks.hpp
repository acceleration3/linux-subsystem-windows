#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class TextureChecks {
public:
    [[nodiscard]] static Outcome all_targets_roundtrip(Context& context);

    [[nodiscard]] static Outcome compressed_formats(Context& context);

    [[nodiscard]] static Outcome generate_mipmap(Context& context);

    [[nodiscard]] static Outcome views(Context& context);

    [[nodiscard]] static Outcome immutable_storage(Context& context);

    [[nodiscard]] static Outcome max_size_limits(Context& context);
};
}  // namespace lsw::suites::sanity::gl
