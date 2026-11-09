#include "render/host/ref_check.hpp"
#include "render/host/registration.hpp"
#include "render/platform/vk_platform.hpp"
#include "render/vk/vk.hpp"

namespace lsw::suites::render::vk {

void add(Suite& suite) {
    using Kit = rr::vk::Kit;
    for (const std::string& problem : rr::CaseTable<Kit>::instance().problems()) {
        suite.add_problem("vk: " + problem);
    }
    for (const auto* entry : rr::CaseTable<Kit>::instance().sorted()) {
        CaseRegistration::add(suite, "vk", "vulkan", entry->spec, [entry](Context& context) {
            std::string error;
            std::unique_ptr<rr::platform::VkPlatform> host = rr::platform::make_vk_platform({}, error);
            if (!host) {
                return Outcome::fail("vulkan platform: " + error);
            }
            Kit kit;
            const rr::OpenResult opened = kit.open(*host, rr::vk::KitOptions::from(entry->spec), error);
            if (opened == rr::OpenResult::Failed) {
                return Outcome::fail("vulkan device: " + error);
            }
            context.measure("vk.device", kit.description);
            GuestIdentity guest{"vk", RefCheck::hex(kit.identity.uuid, 16), std::string(), kit.description};
            return RefCheck::run<Kit>(context, *entry, kit, opened == rr::OpenResult::Unsupported ? error : std::string(), guest);
        });
    }
}

}  // namespace lsw::suites::render::vk
