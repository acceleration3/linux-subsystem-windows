#include "selftest/selftest.hpp"

#include "core/context.hpp"
#include "support/unorm_expect.hpp"

namespace lsw::suites::selftest {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("selftest", Policy::Serial);
    suite.add({"selftest.runner.ok", {"internal"}, false, 10, [](Context& context) {
                   context.measure("bitness", context.bitness());
                   return Outcome::ok();
               }});
    suite.add({"selftest.unorm.tie_acceptance", {"internal"}, false, 10, [](Context&) {
                   const bool ties = UnormExpect::accepts8(0.5, 127) && UnormExpect::accepts8(0.5, 128) && !UnormExpect::accepts8(0.5, 126) && !UnormExpect::accepts8(0.5, 129);
                   const bool exact = UnormExpect::accepts8(1.0 / 255.0, 1) && !UnormExpect::accepts8(1.0 / 255.0, 0) && !UnormExpect::accepts8(1.0 / 255.0, 2) &&
                                      UnormExpect::accepts8(1.0, 255) && !UnormExpect::accepts8(2.0, 254) && UnormExpect::accepts8(-1.0, 0);
                   const bool off_tie = UnormExpect::accepts8(0.25, 64) && !UnormExpect::accepts8(0.25, 63) && UnormExpect::accepts(0.5, 2, 1) && UnormExpect::accepts(0.5, 2, 2);
                   return ties && exact && off_tie ? Outcome::ok() : Outcome::fail("the unorm tie helper accepts or refuses a wrong value");
               }});
}

}  // namespace lsw::suites::selftest
