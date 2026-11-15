#include "sanity/iso/scene.hpp"

#include "sanity/iso/loads.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/pattern.hpp"
#include "support/robust_child.hpp"

#include <windows.h>

#include <chrono>

namespace lsw::suites::sanity::iso {

std::vector<std::string> Scene::apis() {
    return ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl"});
}

std::vector<std::pair<std::string, std::string>> Scene::seconds(double value) {
    return {{Loads::kSeconds, std::to_string(value)}};
}

void Scene::judge(Problems& problems, const Fleet& fleet, const std::string& what, bool must_run) {
    for (const FleetMember& member : fleet.members()) {
        if (member.result.status == Status::Ok || member.result.status == Status::Skip) {
            continue;
        }
        if (!must_run && member.result.status != Status::Fail) {
            continue;
        }
        problems.add(what + " " + member.label + ": " + Outcome::name(member.result.status) + " " + member.result.reason);
    }
}

void Scene::forward(Context& context, const Fleet& fleet, const std::string& prefix) {
    for (const FleetMember& member : fleet.members()) {
        RobustChild::forward(context, prefix + member.label + ".", member.result);
    }
}

bool Scene::roundtrip(Context& context, const std::string& api, std::uint64_t size, std::uint64_t seed, double& ms, std::string& error) {
    static thread_local std::string cached_api;
    static thread_local std::unique_ptr<GpuBackend> backend;
    if (backend == nullptr || cached_api != api) {
        AvAdapter adapter;
        backend = BackendHarness::open(api, adapter, error);
        cached_api = api;
        if (backend == nullptr) {
            return false;
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    AllocStatus status = AllocStatus::Ok;
    const bool ok = BackendHarness::roundtrip(*backend, size, seed, status, context, error);
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return ok && status == AllocStatus::Ok;
}

void Scene::sleep(int milliseconds) {
    ::Sleep(static_cast<DWORD>(milliseconds));
}

}  // namespace lsw::suites::sanity::iso
