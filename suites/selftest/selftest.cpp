#include "selftest/selftest.hpp"

#include "core/context.hpp"

namespace lsw::suites::selftest {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("selftest", Policy::Serial);
    suite.add({"selftest.runner.ok", {"internal"}, false, 10, [](Context& context) {
                   context.measure("bitness", context.bitness());
                   return Outcome::ok();
               }});
}

}  // namespace lsw::suites::selftest
