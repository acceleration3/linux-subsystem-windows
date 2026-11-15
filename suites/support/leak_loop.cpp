#include "support/leak_loop.hpp"

#include "support/dxgi_budget.hpp"
#include "support/process_sampler.hpp"

namespace lsw::suites {

namespace {

std::uint64_t gpu_usage(const LUID& luid) {
    VideoMemory memory;
    std::string error;
    return DxgiBudget::query(luid, true, memory, error) ? memory.usage : 0;
}

}  // namespace

void LeakLoop::run(Context& context, Problems& problems, const std::string& label, const LUID& luid, int count,
                   const std::function<bool(int, std::string&)>& churn) {
    std::string error;
    for (int i = 0; i < 100; ++i) {
        if (!churn(i, error)) {
            problems.add(label + " warm-up " + std::to_string(i) + ": " + error);
            return;
        }
    }
    ::Sleep(300);
    const ProcessSample before = ProcessSampler::take();
    const std::uint64_t gpu_before = gpu_usage(luid);
    for (int i = 0; i < count; ++i) {
        if (!churn(i, error)) {
            problems.add(label + " iteration " + std::to_string(i) + ": " + error);
            return;
        }
    }
    ::Sleep(500);
    const ProcessSample after = ProcessSampler::take();
    const std::uint64_t gpu_after = gpu_usage(luid);
    const std::int64_t private_delta = static_cast<std::int64_t>(after.private_bytes) - static_cast<std::int64_t>(before.private_bytes);
    const std::int64_t handle_delta = static_cast<std::int64_t>(after.handles) - static_cast<std::int64_t>(before.handles);
    const std::int64_t gpu_delta = static_cast<std::int64_t>(gpu_after) - static_cast<std::int64_t>(gpu_before);
    context.measure(label + ".private_delta", private_delta);
    context.measure(label + ".handle_delta", handle_delta);
    context.measure(label + ".gpu_delta", gpu_delta);
    if (private_delta > static_cast<std::int64_t>(kPrivateTolerance)) {
        problems.add(label + ": private bytes grew by " + std::to_string(private_delta >> 10) + " KiB over " + std::to_string(count) + " objects");
    }
    if (handle_delta > static_cast<std::int64_t>(kHandleTolerance)) {
        problems.add(label + ": " + std::to_string(handle_delta) + " handles were not released");
    }
    if (gpu_delta > static_cast<std::int64_t>(kGpuTolerance)) {
        problems.add(label + ": GPU usage grew by " + std::to_string(gpu_delta >> 10) + " KiB");
    }
}

}  // namespace lsw::suites
