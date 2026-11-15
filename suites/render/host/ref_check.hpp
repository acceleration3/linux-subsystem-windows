#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "render/host/guest_types.hpp"

#include <filesystem>
#include <string>

namespace lsw::suites::render {
struct GuestIdentity {
    std::string api;
    std::string vulkan_uuid;
    std::string gl_uuid;
    std::string name;
};

class RefCheck {
public:
    [[nodiscard]] static std::filesystem::path root();

    [[nodiscard]] static std::filesystem::path store_root(const GuestIdentity& guest);

    [[nodiscard]] static std::string hex(const unsigned char* bytes, std::size_t count);

    [[nodiscard]] static bool open_store(rr::RefStore& store, std::string& error);

    [[nodiscard]] static bool device_matches(const rr::RefStore& store, const GuestIdentity& guest, std::string& error);

    template <class Kit>
    [[nodiscard]] static Outcome run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry, Kit& kit,
                                     const std::string& open_unsupported, const GuestIdentity& guest);

private:
    [[nodiscard]] static Outcome judge(Context& context, const std::string& id, const rr::CaseSpec& spec,
                                       const rr::FrameCaptures& frames, const rr::RefStore& store);
};

template <class Kit>
Outcome RefCheck::run(Context& context, const typename rr::CaseTable<Kit>::Entry& entry, Kit& kit, const std::string& open_unsupported,
                      const GuestIdentity& guest) {
    const std::string id = entry.spec.id(guest.api);
    rr::RefStore store(store_root(guest));
    std::string error;
    if (!open_store(store, error) || !device_matches(store, guest, error)) {
        return Outcome::fail(error);
    }
    std::string recipe;
    if (!store.read_recipe(id, recipe)) {
        return Outcome::fail("no reference recipe for " + id + " in " + store.root().string() + ": the case is newer than the references");
    }
    if (recipe != entry.spec.recipe()) {
        return Outcome::fail("stale reference for " + id + ": it was rendered by recipe " + recipe + " but the case is " + entry.spec.recipe() +
                             "; run lsw-render-ref again");
    }
    std::unique_ptr<rr::Scene<Kit>> scene = entry.make();
    std::string unsupported = open_unsupported;
    if (unsupported.empty()) {
        unsupported = scene->unsupported(kit);
    }
    std::string marker;
    if (store.read_skip(id, marker)) {
        if (!unsupported.empty()) {
            if (unsupported != marker) {
                return Outcome::fail("caps disagree: the host skipped with \"" + marker + "\" but the guest reports \"" + unsupported + "\"");
            }
            return Outcome::skip(marker);
        }
        return Outcome::fail("capabilities disagree: the host reference was skipped (" + marker + ") but the guest reports the feature present");
    }
    if (!unsupported.empty()) {
        return Outcome::fail("the guest hides a feature the host has: " + unsupported);
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
