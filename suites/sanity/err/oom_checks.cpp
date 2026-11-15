#include "sanity/err/oom_checks.hpp"

#include "core/child_mode.hpp"
#include "support/api_loop.hpp"
#include "support/fresh_process.hpp"
#include "support/gate_window.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace lsw::suites::sanity::err {

namespace {

constexpr std::uint64_t kMiB = 1ull << 20;
constexpr std::uint64_t kCapBytes = 48ull << 30;

}  // namespace

Outcome OomChecks::every_api(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, [](Context&, const std::string& name) { return Outcome::fail("unknown role " + name); });
    }
    Problems problems;
    GateWindow window(context.id());
    window.open(problems);
    ApiLoop::run(problems, {"d3d11", "d3d12", "vulkan", "opencl", "gl", "d3d9"}, [&](GpuBackend& backend, const std::string& api, const AvAdapter&) {
        const std::uint64_t maximum = backend.max_allocation();
        const std::uint64_t cap = std::min(std::max<std::uint64_t>(backend.available_memory() * 2, 4096 * kMiB), kCapBytes);
        std::vector<std::unique_ptr<GpuBuffer>> held;
        std::uint64_t total = 0;
        std::string first_failure = "none within " + std::to_string(cap / kMiB) + " MiB";
        for (const std::uint64_t chunk_mib : {256ull, 64ull, 16ull, 4ull, 1ull}) {
            std::uint64_t chunk = chunk_mib * kMiB;
            if (maximum != 0) {
                chunk = std::min(chunk, maximum);
            }
            while (total < cap) {
                AllocStatus status = AllocStatus::Ok;
                std::string detail;
                std::unique_ptr<GpuBuffer> buffer = backend.create(chunk, BufferKind::Device, status, detail);
                if (buffer == nullptr) {
                    if (first_failure.rfind("none", 0) == 0) {
                        first_failure = GpuBackends::describe(status) + " at " + std::to_string(chunk_mib) + " MiB after " + std::to_string(total / kMiB) + " MiB: " + detail;
                    }
                    if (status != AllocStatus::OutOfMemory) {
                        problems.add(api + ": a " + std::to_string(chunk / kMiB) + " MiB request after " + std::to_string(total / kMiB) + " MiB failed with '" + detail +
                                     "' (" + GpuBackends::describe(status) + ") instead of the API's out-of-memory error");
                    }
                    break;
                }
                total += chunk;
                held.push_back(std::move(buffer));
            }
        }
        context.measure(api + ".held_mib", total / kMiB);
        context.measure(api + ".first_failure", first_failure);
        held.clear();
        std::string error;
        static_cast<void>(backend.finish(error));
        for (const std::uint64_t size : {4096ull, kMiB, 16 * kMiB}) {
            AllocStatus status = AllocStatus::Ok;
            if (!BackendHarness::roundtrip(backend, size, 90 + size, status, context, error) || status != AllocStatus::Ok) {
                problems.add(api + ": a fresh " + std::to_string(size) + " byte roundtrip after freeing everything failed: " + (error.empty() ? GpuBackends::describe(status) : error));
            }
        }
    });
    window.close(context, problems, "oom_every_api", 5000000);
    FreshProcess::check(context, problems, context.id(), "oom_every_api");
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::err
