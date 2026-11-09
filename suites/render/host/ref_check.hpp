#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "render/host/guest_types.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites::render {
class RefCheck {
public:
    [[nodiscard]] static std::filesystem::path root();

    [[nodiscard]] static std::string hex(const unsigned char* bytes, std::size_t count);

    [[nodiscard]] static bool open_store(rr::RefStore& store, std::string& error);

    [[nodiscard]] static bool device_matches(const rr::RefStore& store, const std::string& guest_vulkan_uuid,
                                             const std::string& guest_gl_uuid, const std::string& guest_name,
                                             std::string& error);

    template <class Kit>
    [[nodiscard]] static Outcome run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry, const std::string& api,
                                     Kit& kit, const std::string& guest_vulkan_uuid, const std::string& guest_gl_uuid,
                                     const std::string& guest_name);

private:
    [[nodiscard]] static Outcome judge(Context& context, const std::string& id, const rr::CaseSpec& spec,
                                       const rr::FrameCaptures& frames, const rr::RefStore& store);
};

template <class Kit>
Outcome RefCheck::run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry, const std::string& api, Kit& kit,
                      const std::string& guest_vulkan_uuid, const std::string& guest_gl_uuid, const std::string& guest_name) {
    const std::string id = entry.spec.id(api);
    rr::RefStore store(root());
    std::string error;
    if (!open_store(store, error)) {
        return Outcome::fail(error);
    }
    if (!device_matches(store, guest_vulkan_uuid, guest_gl_uuid, guest_name, error)) {
        return Outcome::fail(error);
    }
    std::unique_ptr<rr::Scene<Kit>> scene = entry.make();
    const std::string unsupported = scene->unsupported(kit);
    std::string marker;
    if (store.read_skip(id, marker)) {
        if (!unsupported.empty()) {
            return Outcome::skip(marker + " (guest: " + unsupported + ")");
        }
        return Outcome::fail("capabilities disagree: the host reference was skipped (" + marker +
                             ") but the guest reports the feature present");
    }
    rr::RunResult result = rr::run_scene(kit, entry.spec, *scene);
    if (result.status == rr::RunStatus::Skip) {
        return Outcome::fail("the guest hides a feature the host has: " + result.reason);
    }
    if (result.status == rr::RunStatus::Fail) {
        return Outcome::fail(result.reason);
    }
    return judge(context, id, entry.spec, result.frames, store);
}
}  // namespace lsw::suites::render
