#include "sanity/iso/stall_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/iso/fleet.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/iso/scene.hpp"
#include "support/robust_child.hpp"

#include <algorithm>

namespace lsw::suites::sanity::iso {

namespace {

constexpr int kTimeout = 240;

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0.0 : values[values.size() / 2];
}

}  // namespace

Outcome StallChecks::heavy_load_other_finishes(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, Loads::role);
    }
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        std::vector<double> idle;
        std::string error;
        double ms = 0;
        for (int i = 0; i < 20; ++i) {
            if (!Scene::roundtrip(context, api, 1u << 20, 100 + static_cast<std::uint64_t>(i), ms, error)) {
                problems.add(api + ": idle roundtrip: " + error);
                break;
            }
            idle.push_back(ms);
        }
        if (idle.size() < 20) {
            continue;
        }
        Fleet load;
        load.start(context.id(), "spin-" + api, Loads::name("spinloop", api), Scene::seconds(9), kTimeout);
        Scene::sleep(2500);
        double worst = 0;
        for (int i = 0; i < 20; ++i) {
            if (!Scene::roundtrip(context, api, 1u << 20, 500 + static_cast<std::uint64_t>(i), ms, error)) {
                problems.add(api + ": roundtrip under a saturating compute loop: " + error);
                break;
            }
            worst = std::max(worst, ms);
        }
        load.join();
        Scene::judge(problems, load, "saturating process");
        const double idle_median = median(idle);
        const double bound = idle_median * 40.0 + 1000.0;
        context.measure(api + ".idle_median_ms", idle_median);
        context.measure(api + ".loaded_worst_ms", worst);
        if (worst > bound) {
            problems.add(api + ": a 1 MiB roundtrip took " + std::to_string(worst) + " ms next to a saturating compute loop, idle median " + std::to_string(idle_median) + " ms, bound " + std::to_string(bound) + " ms");
        }
    }
    return problems.outcome();
}

Outcome StallChecks::huge_upload_other_progress(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, Loads::role);
    }
    constexpr double kBoundMs = 3000.0;
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        Fleet hog;
        hog.start(context.id(), "upload-" + api, Loads::name("uploadhog", api), Scene::seconds(10), kTimeout);
        Scene::sleep(2000);
        double worst = 0;
        std::string error;
        double ms = 0;
        for (int i = 0; i < 40; ++i) {
            if (!Scene::roundtrip(context, api, 64u << 10, 900 + static_cast<std::uint64_t>(i), ms, error)) {
                problems.add(api + ": roundtrip " + std::to_string(i) + " during the upload storm: " + error);
                break;
            }
            worst = std::max(worst, ms);
            Scene::sleep(100);
        }
        hog.join();
        Scene::judge(problems, hog, "uploading process");
        context.measure(api + ".worst_roundtrip_ms", worst);
        if (worst > kBoundMs) {
            problems.add(api + ": a 64 KiB roundtrip took " + std::to_string(worst) + " ms while another process uploaded 512 MiB repeatedly, bound " + std::to_string(kBoundMs) + " ms");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::iso
