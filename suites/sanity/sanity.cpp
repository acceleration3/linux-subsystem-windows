#include "sanity/sanity.hpp"

#include "sanity/env/adapters_present.hpp"

namespace lsw::suites::sanity {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("sanity", Policy::Parallel);
    suite.add({"sanity.env.adapters.present", {"env", "dxgi"}, false, 30, env::AdaptersPresent::run});
}

}  // namespace lsw::suites::sanity
