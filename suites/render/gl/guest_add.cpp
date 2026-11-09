#include "render/gl/gl.hpp"
#include "render/host/ref_check.hpp"
#include "render/host/registration.hpp"
#include "render/platform/gl_platform.hpp"

namespace lsw::suites::render::gl {

void add(Suite& suite) {
    using Kit = rr::gl::Kit;
    for (const std::string& problem : rr::CaseTable<Kit>::instance().problems()) {
        suite.add_problem("gl: " + problem);
    }
    for (const auto* entry : rr::CaseTable<Kit>::instance().sorted()) {
        CaseRegistration::add(suite, "gl", "gl", entry->spec, [entry](Context& context) {
            std::string error;
            std::unique_ptr<rr::platform::GlPlatform> host = rr::platform::make_gl_platform({}, error);
            if (!host) {
                return Outcome::fail("gl platform: " + error);
            }
            Kit kit;
            const rr::OpenResult opened = kit.open(*host, rr::gl::KitOptions::from(entry->spec), error);
            if (opened == rr::OpenResult::Failed) {
                return Outcome::fail("gl context: " + error);
            }
            context.measure("gl.renderer", kit.renderer);
            context.measure("gl.uuid", kit.uuid);
            GuestIdentity guest{"gl", std::string(), kit.uuid, kit.renderer};
            return RefCheck::run<Kit>(context, *entry, kit, opened == rr::OpenResult::Unsupported ? error : std::string(), guest);
        });
    }
}

}  // namespace lsw::suites::render::gl
