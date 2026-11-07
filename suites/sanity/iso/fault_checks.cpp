#include "sanity/iso/fault_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/iso/fleet.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/iso/scene.hpp"
#include "support/backend_harness.hpp"
#include "support/fresh_process.hpp"
#include "support/pattern.hpp"
#include "support/robust_child.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace lsw::suites::sanity::iso {

namespace {

constexpr int kTimeout = 240;

Outcome child_or(Context& context, bool& is_child) {
    is_child = ChildMode::active();
    return is_child ? RobustChild::child(context, Loads::role) : Outcome::ok();
}

}  // namespace

Outcome FaultChecks::crash_leaves_others(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    const unsigned seed = static_cast<unsigned>(::GetTickCount64());
    context.measure("seed", seed);
    std::mt19937 random(seed);
    Fleet victims;
    for (int i = 0; i < 8; ++i) {
        const std::string& api = apis[static_cast<std::size_t>(i) % apis.size()];
        victims.start(context.id(), "victim" + std::to_string(i) + "-" + api, Loads::name("verified", api), Scene::seconds(20), kTimeout);
    }
    Scene::sleep(1500);
    int killed = 0;
    for (int i = 0; i < 6; ++i) {
        const std::string& api = apis[static_cast<std::size_t>(i) % apis.size()];
        const CheckResult result = RobustChild::run(context.id(), Loads::name("hammer", api), {}, 90, static_cast<int>(random() % 600));
        if (result.status == Status::Timeout && result.reason.rfind("terminated", 0) == 0) {
            ++killed;
        }
        Scene::sleep(500);
    }
    context.measure("killed", killed);
    victims.join();
    Problems problems;
    Scene::judge(problems, victims, "bystander");
    Scene::forward(context, victims, "");
    FreshProcess::check(context, problems, context.id(), "crash_leaves_others");
    return problems.outcome();
}

Outcome FaultChecks::device_loss_leaves_others(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Fleet victims;
    for (const std::string& api : apis) {
        victims.start(context.id(), "victim-" + api, Loads::name("verified", api), Scene::seconds(20 + 10 * static_cast<double>(apis.size())), kTimeout);
    }
    Scene::sleep(3000);
    Fleet losses;
    losses.start(context.id(), "remove-d3d12", Loads::name("remove", "d3d12"), Scene::seconds(5), 120);
    losses.join();
    Scene::sleep(1500);
    for (const std::string& api : apis) {
        Fleet one;
        one.start(context.id(), "tdr-" + api, Loads::name("spinforever", api), Scene::seconds(6), 120);
        one.join();
        Scene::forward(context, one, "");
    }
    victims.join();
    Problems problems;
    Scene::judge(problems, victims, "bystander");
    Scene::forward(context, victims, "");
    FreshProcess::check(context, problems, context.id(), "device_loss_leaves_others");
    return problems.outcome();
}

Outcome FaultChecks::oom_leaves_others(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        Fleet holder;
        holder.start(context.id(), "holder-" + api, Loads::name("holder", api), Scene::seconds(14), kTimeout);
        Scene::sleep(2500);
        Fleet hog;
        hog.start(context.id(), "exhaust-" + api, Loads::name("exhaust", api), Scene::seconds(5), 300);
        hog.join();
        holder.join();
        Scene::judge(problems, holder, "holder");
        Scene::forward(context, holder, "");
        Scene::forward(context, hog, "");
    }
    FreshProcess::check(context, problems, context.id(), "oom_leaves_others");
    return problems.outcome();
}

Outcome FaultChecks::hang_leaves_others(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    constexpr double kBoundMs = 6000.0;
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        Fleet victims;
        for (const std::string& other : apis) {
            victims.start(context.id(), "latency-" + other, Loads::name("latency", other), Scene::seconds(12), kTimeout);
        }
        Scene::sleep(2500);
        Fleet spinner;
        spinner.start(context.id(), "spin-" + api, Loads::name("spinforever", api), Scene::seconds(6), 120);
        spinner.join();
        victims.join();
        Scene::judge(problems, victims, "while " + api + " hung, victim");
        for (const FleetMember& member : victims.members()) {
            const double worst = static_cast<double>(member.result.measures.find("max_ms") != nullptr ? member.result.measures.find("max_ms")->as_real() : 0.0);
            context.measure("hang_" + api + "." + member.label + ".max_ms", worst);
            if (worst > kBoundMs) {
                problems.add("while " + api + " hung, " + member.label + " waited " + std::to_string(worst) + " ms for a 64 KiB roundtrip, bound " + std::to_string(kBoundMs));
            }
        }
    }
    FreshProcess::check(context, problems, context.id(), "hang_leaves_others");
    return problems.outcome();
}

Outcome FaultChecks::invalid_commands_contained(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        Fleet victims;
        for (const std::string& other : apis) {
            victims.start(context.id(), "victim-" + other, Loads::name("verified", other), Scene::seconds(10), kTimeout);
        }
        Scene::sleep(1500);
        Fleet offender;
        offender.start(context.id(), "invalid-" + api, Loads::name("oob", api), {}, 120);
        offender.join();
        victims.join();
        Scene::judge(problems, victims, "while " + api + " submitted invalid commands, victim");
        Scene::forward(context, offender, "");
        for (const FleetMember& member : offender.members()) {
            if (member.result.status == Status::Timeout) {
                problems.add(api + " offender never ended: " + member.result.reason);
            }
        }
    }
    FreshProcess::check(context, problems, context.id(), "invalid_commands_contained");
    return problems.outcome();
}

Outcome FaultChecks::out_of_bounds_contained(Context& context) {
    bool is_child = false;
    const Outcome child = child_or(context, is_child);
    if (is_child) {
        return child;
    }
    constexpr std::uint64_t kBuffer = 32ull << 20;
    const std::vector<std::string> apis = Scene::apis();
    if (apis.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    for (const std::string& api : apis) {
        AvAdapter adapter;
        std::string error;
        std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
        if (backend == nullptr) {
            problems.add(error);
            continue;
        }
        std::vector<std::unique_ptr<GpuBuffer>> sentinels;
        std::vector<std::uint8_t> expected(kBuffer);
        for (int i = 0; i < 4; ++i) {
            AllocStatus status = AllocStatus::Ok;
            std::unique_ptr<GpuBuffer> buffer = backend->create(kBuffer, BufferKind::Device, status, error);
            Pattern::fill(7000 + static_cast<std::uint64_t>(i), 0, expected.data(), expected.size());
            if (buffer == nullptr || !backend->upload(*buffer, 0, expected.data(), expected.size(), error)) {
                problems.add(api + ": preparing sentinel " + std::to_string(i) + ": " + error);
                break;
            }
            sentinels.push_back(std::move(buffer));
        }
        static_cast<void>(backend->finish(error));
        Fleet offender;
        offender.start(context.id(), "oob-" + api, Loads::name("oob", api), {}, 120);
        std::vector<std::uint8_t> actual(kBuffer);
        auto verify = [&](const std::string& when) {
            for (std::size_t i = 0; i < sentinels.size(); ++i) {
                Pattern::fill(7000 + static_cast<std::uint64_t>(i), 0, expected.data(), expected.size());
                if (!backend->download(*sentinels[i], 0, actual.data(), actual.size(), error)) {
                    problems.add(api + " sentinel " + std::to_string(i) + " " + when + ": " + error);
                    return false;
                }
                if (std::memcmp(expected.data(), actual.data(), expected.size()) != 0) {
                    problems.add(api + " sentinel " + std::to_string(i) + " " + when + " changed while another process wrote out of bounds");
                    return false;
                }
            }
            return true;
        };
        for (int i = 0; i < 6 && verify("during"); ++i) {
            Scene::sleep(300);
        }
        offender.join();
        static_cast<void>(verify("after"));
    }
    FreshProcess::check(context, problems, context.id(), "out_of_bounds_contained");
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::iso
