#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::memory {
class LeakChecks {
public:
    [[nodiscard]] static Outcome buffers_all_apis(Context& context);

    [[nodiscard]] static Outcome textures_all_apis(Context& context);

    [[nodiscard]] static Outcome views_and_samplers(Context& context);

    [[nodiscard]] static Outcome shaders_pipelines(Context& context);

    [[nodiscard]] static Outcome contexts_devices(Context& context);

    [[nodiscard]] static Outcome sync_objects(Context& context);

    [[nodiscard]] static Outcome shared_resources(Context& context);

    [[nodiscard]] static Outcome process_exit_releases(Context& context);
};
}  // namespace lsw::suites::sanity::memory
