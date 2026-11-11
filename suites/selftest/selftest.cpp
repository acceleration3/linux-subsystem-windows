#include "selftest/selftest.hpp"

#include "core/context.hpp"
#include "core/desktop_guard.hpp"
#include "support/unorm_expect.hpp"

namespace lsw::suites::selftest {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("selftest", Policy::Serial);
    suite.add({"selftest.runner.ok", {"internal"}, false, 10, [](Context& context) {
                   context.measure("bitness", context.bitness());
                   return Outcome::ok();
               }});
    suite.add({"selftest.unorm.tie_acceptance", {"internal"}, false, 10, [](Context&) {
                   constexpr UnormRule d3d = UnormRule::Direct3d;
                   constexpr UnormRule khr = UnormRule::Khronos;
                   const bool ties = UnormExpect::accepts8(d3d, 0.5, 127) && UnormExpect::accepts8(d3d, 0.5, 128) && !UnormExpect::accepts8(d3d, 0.5, 126) && !UnormExpect::accepts8(d3d, 0.5, 129) &&
                                     UnormExpect::accepts8(khr, 0.5, 127) && UnormExpect::accepts8(khr, 0.5, 128) && !UnormExpect::accepts8(khr, 0.5, 126) && !UnormExpect::accepts8(khr, 0.5, 129);
                   bool exact = true;
                   for (const UnormRule rule : {d3d, khr}) {
                       exact = exact && UnormExpect::accepts8(rule, 1.0 / 255.0, 1) && !UnormExpect::accepts8(rule, 1.0 / 255.0, 0) && !UnormExpect::accepts8(rule, 1.0 / 255.0, 2) &&
                               UnormExpect::accepts8(rule, 1.0, 255) && !UnormExpect::accepts8(rule, 2.0, 254) && UnormExpect::accepts8(rule, -1.0, 0);
                   }
                   const bool off_tie = UnormExpect::accepts8(d3d, 0.25, 64) && !UnormExpect::accepts8(d3d, 0.25, 63) && UnormExpect::accepts(d3d, 0.5, 2, 1) && UnormExpect::accepts(d3d, 0.5, 2, 2) &&
                                        UnormExpect::accepts8(khr, 0.25, 63) && UnormExpect::accepts8(khr, 0.25, 64) && !UnormExpect::accepts8(khr, 0.25, 62) && !UnormExpect::accepts8(khr, 0.25, 65);
                   const bool within_ulp = UnormExpect::accepts8(d3d, 111.5625 / 255.0, 111) && UnormExpect::accepts8(d3d, 111.5625 / 255.0, 112) && !UnormExpect::accepts8(d3d, 111.5625 / 255.0, 110) &&
                                           UnormExpect::accepts8(d3d, 143.4375 / 255.0, 143) && UnormExpect::accepts8(d3d, 143.4375 / 255.0, 144) && !UnormExpect::accepts8(d3d, 143.4375 / 255.0, 145);
                   return ties && exact && off_tie && within_ulp ? Outcome::ok() : Outcome::fail("the unorm tie helper accepts or refuses a wrong value");
               }});
    suite.add({"selftest.desktop.guard_scope", {"internal"}, false, 10, [](Context&) {
                   const std::string own = "C:\\run\\suites";
                   const bool own_kept = !DesktopGuard::foreign("C:\\run\\suites\\lsw-suites64.exe", own) &&
                                         !DesktopGuard::foreign("C:\\Other\\lsw-suites32.exe", own) &&
                                         !DesktopGuard::foreign("c:\\RUN\\Suites\\helper.exe", own);
                   const bool shell_kept = !DesktopGuard::foreign("C:\\Windows\\explorer.exe", own) &&
                                           !DesktopGuard::foreign("C:\\Windows\\SystemApps\\StartMenuExperienceHost.exe", own) &&
                                           !DesktopGuard::foreign("C:\\Windows\\System32\\LogonUI.exe", own);
                   const bool stray = DesktopGuard::foreign("C:\\Apps\\app.exe", own);
                   return own_kept && shell_kept && stray ? Outcome::ok() : Outcome::fail("the desktop guard classifies a process wrongly");
               }});
}

}  // namespace lsw::suites::selftest
