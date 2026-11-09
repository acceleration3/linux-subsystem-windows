#include "render/gl/gl.hpp"
#include "render/host/ref_check.hpp"
#include "render/host/registration.hpp"
#include "render/platform/gl_platform.hpp"

namespace lsw::suites::render::gl {

void add(Suite& suite) {
    using Kit = rr::gl::Kit;
    for (const auto* entry : rr::CaseTable<Kit>::instance().sorted()) {
        CaseRegistration::add(suite, "gl", "gl", entry->spec, [entry](Context& context) {
            std::string error;
            std::unique_ptr<rr::platform::GlPlatform> host = rr::platform::make_gl_platform({}, error);
            if (!host) {
                return Outcome::fail("gl platform: " + error);
            }
            Kit kit;
            rr::gl::KitOptions options;
            options.width = entry->spec.width();
            options.height = entry->spec.height();
            options.window = entry->spec.uses_swapchain();
            if (!kit.open(*host, options, error)) {
                return Outcome::fail("gl context: " + error);
            }
            context.measure("gl.renderer", kit.renderer);
            return RefCheck::run<Kit>(context, *entry, "gl", kit, std::string(), kit.uuid, kit.renderer);
        });
    }
}

}  // namespace lsw::suites::render::gl
