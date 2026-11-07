#include "sanity/sync/order_checks.hpp"

#include "sanity/sync/api_children.hpp"
#include "sanity/sync/sync_device.hpp"
#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <random>

namespace lsw::suites::sanity::sync {

namespace {

constexpr int kIterations = 10000;
constexpr int kWaitMs = 20000;
const std::vector<std::string> kApis = {"d3d11", "d3d12", "vulkan", "opencl"};

Outcome open_device(const std::string& api, std::unique_ptr<SyncDevice>& device) {
    device = SyncDevice::make(api);
    std::string error;
    if (device == nullptr || !device->open(error)) {
        return Context::skip(api + " is unavailable: " + error);
    }
    return Outcome::ok();
}

}  // namespace

Outcome OrderChecks::readback_after_signal(Context& context) {
    return ApiChildren::run(context, kApis, [](Context&, const std::string& api) {
        std::unique_ptr<SyncDevice> device;
        const Outcome opened = open_device(api, device);
        if (opened.status() != Status::Ok) {
            return opened;
        }
        std::string error;
        std::string detail;
        for (int i = 0; i < kIterations; ++i) {
            const std::uint32_t slot = static_cast<std::uint32_t>(i) % SyncDevice::kSlots;
            const std::uint32_t base = static_cast<std::uint32_t>(i) * 7u + 1u;
            if (!device->set(slot, base, error) || !device->add(slot, 3, error) || !device->signal(error)) {
                return Outcome::fail("iteration " + std::to_string(i) + ": " + error);
            }
            if (device->wait(kWaitMs, detail) != SyncState::Done) {
                return Outcome::fail("iteration " + std::to_string(i) + ": the fence did not complete: " + detail);
            }
            std::uint32_t value = 0;
            if (!device->read(slot, value, error)) {
                return Outcome::fail("iteration " + std::to_string(i) + ": " + error);
            }
            if (value != base + 3u) {
                return Outcome::fail("iteration " + std::to_string(i) + ": slot " + std::to_string(slot) + " read " + std::to_string(value) +
                                     " after the fence, expected " + std::to_string(base + 3u));
            }
        }
        return Outcome::ok();
    }, 600);
}

Outcome OrderChecks::flush_makes_visible(Context& context) {
    return ApiChildren::run(context, kApis, [](Context&, const std::string& api) {
        std::unique_ptr<SyncDevice> device;
        const Outcome opened = open_device(api, device);
        if (opened.status() != Status::Ok) {
            return opened;
        }
        std::string error;
        std::string detail;
        for (int round = 0; round < 200; ++round) {
            const std::uint32_t slot = static_cast<std::uint32_t>(round) % SyncDevice::kSlots;
            const std::uint32_t value = 1000u + static_cast<std::uint32_t>(round);
            if (!device->set(slot, value, error) || !device->add(slot, 5, error) || !device->signal(error)) {
                return Outcome::fail("round " + std::to_string(round) + ": " + error);
            }
            if (!device->finish(error)) {
                return Outcome::fail("round " + std::to_string(round) + ": finish failed: " + error);
            }
            if (device->wait(100, detail) != SyncState::Done) {
                return Outcome::fail("round " + std::to_string(round) + ": work signalled before the finish was not complete after it: " + detail);
            }
            std::uint32_t read = 0;
            if (!device->read(slot, read, error) || read != value + 5u) {
                return Outcome::fail("round " + std::to_string(round) + ": slot " + std::to_string(slot) + " read " + std::to_string(read) + ", expected " +
                                     std::to_string(value + 5u) + (error.empty() ? "" : ": " + error));
            }
        }
        return Outcome::ok();
    }, 300);
}

Outcome OrderChecks::queue_order_in_order(Context& context) {
    return ApiChildren::run(context, kApis, [](Context&, const std::string& api) {
        std::unique_ptr<SyncDevice> device;
        const Outcome opened = open_device(api, device);
        if (opened.status() != Status::Ok) {
            return opened;
        }
        std::string error;
        std::string detail;
        constexpr int kChain = 5000;
        constexpr std::uint32_t kLive = 4;
        std::uint32_t model[kLive] = {};
        for (std::uint32_t slot = 0; slot < kLive; ++slot) {
            if (!device->set(slot, 0, error)) {
                return Outcome::fail(error);
            }
        }
        for (int i = 0; i < kChain; ++i) {
            if (!device->add(0, 1, error)) {
                return Outcome::fail("chain step " + std::to_string(i) + ": " + error);
            }
        }
        std::mt19937 random(0x5eed);
        for (int i = 0; i < kChain; ++i) {
            const std::uint32_t slot = 1 + random() % (kLive - 1);
            const std::uint32_t value = random() % 1000;
            const bool assign = random() % 3 == 0;
            if (!(assign ? device->set(slot, value, error) : device->add(slot, value, error))) {
                return Outcome::fail("mixed step " + std::to_string(i) + ": " + error);
            }
            model[slot] = assign ? value : model[slot] + value;
        }
        model[0] = kChain;
        if (!device->signal(error) || device->wait(kWaitMs, detail) != SyncState::Done) {
            return Outcome::fail("the queue did not complete: " + error + detail);
        }
        for (std::uint32_t slot = 0; slot < kLive; ++slot) {
            std::uint32_t value = 0;
            if (!device->read(slot, value, error)) {
                return Outcome::fail(error);
            }
            if (value != model[slot]) {
                return Outcome::fail("slot " + std::to_string(slot) + " holds " + std::to_string(value) + ", the in-order result is " + std::to_string(model[slot]));
            }
        }
        return Outcome::ok();
    }, 300);
}

Outcome OrderChecks::out_of_order_events(Context&) {
    ClRig rig;
    std::string error;
    if (!rig.open(ClRigOptions{}, error)) {
        return Context::skip("OpenCL is unavailable: " + error);
    }
    cl_command_queue_properties supported = 0;
    if (!rig.info(CL_DEVICE_QUEUE_ON_HOST_PROPERTIES, supported)) {
        return Outcome::fail("CL_DEVICE_QUEUE_ON_HOST_PROPERTIES could not be queried");
    }
    cl_int code = CL_SUCCESS;
    ClOwned<cl_command_queue> queue(rig, rig.create_queue(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, code));
    if ((supported & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) == 0) {
        if (code != CL_INVALID_QUEUE_PROPERTIES || queue) {
            return Outcome::fail("out-of-order mode is not reported but queue creation returned " + ClTools::error_name(code));
        }
        return Context::skip("the device reports no out-of-order queue support, and creating one failed with CL_INVALID_QUEUE_PROPERTIES");
    }
    if (code != CL_SUCCESS || !queue) {
        return Outcome::fail("out-of-order mode is reported but queue creation returned " + ClTools::error_name(code));
    }
    ClProgram program;
    const char* source =
        "__kernel void step(__global uint* v, uint op, uint value, uint slot) {\n"
        "  if (op == 0u) { v[slot] = value; } else { v[slot] += value; }\n"
        "}\n";
    if (!program.build(rig, source, "", error)) {
        return Outcome::fail(error);
    }
    ClKernel kernel = program.kernel("step", error);
    const std::vector<std::uint32_t> zeros(8, 0);
    ClOwned<cl_mem> buffer(rig, rig.api.CreateBuffer(rig.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, 32, const_cast<std::uint32_t*>(zeros.data()), &code));
    if (!kernel || !buffer) {
        return Outcome::fail("creating the kernel or buffer failed: " + error + ClTools::error_name(code));
    }
    Problems problems;
    for (int round = 0; round < 50; ++round) {
        std::vector<ClOwned<cl_event>> events;
        auto submit = [&](std::uint32_t op, std::uint32_t value, std::uint32_t slot, const std::vector<int>& after, const cl_event extra) -> int {
            std::vector<cl_event> waits;
            for (int index : after) {
                waits.push_back(events[static_cast<std::size_t>(index)].get());
            }
            if (extra != nullptr) {
                waits.push_back(extra);
            }
            cl_int result = kernel.set(buffer, op, value, slot);
            cl_event event = nullptr;
            if (result == CL_SUCCESS) {
                result = kernel.run_on(queue.get(), 1, 0, static_cast<cl_uint>(waits.size()), waits.empty() ? nullptr : waits.data(), &event);
            }
            if (result != CL_SUCCESS) {
                problems.add("enqueue failed: " + ClTools::error_name(result));
                return -1;
            }
            events.emplace_back(rig, event);
            return static_cast<int>(events.size()) - 1;
        };
        cl_int user_code = CL_SUCCESS;
        ClOwned<cl_event> gate(rig, rig.api.CreateUserEvent(rig.context, &user_code));
        if (user_code != CL_SUCCESS) {
            return Outcome::fail("clCreateUserEvent returned " + ClTools::error_name(user_code));
        }
        const int a = submit(0, 10, 0, {}, gate.get());
        const int b = a < 0 ? -1 : submit(1, 5, 0, {a}, nullptr);
        const int c = b < 0 ? -1 : submit(1, 100, 0, {b}, nullptr);
        const int d = submit(0, 7, 1, {}, nullptr);
        const int e = d < 0 ? -1 : submit(1, 1, 1, {d}, nullptr);
        const int f = (c < 0 || e < 0) ? -1 : submit(0, 3, 2, {c, e}, nullptr);
        if (f < 0) {
            return problems.outcome();
        }
        rig.api.Flush(queue.get());
        if (rig.api.SetUserEventStatus(gate.get(), CL_COMPLETE) != CL_SUCCESS) {
            return Outcome::fail("clSetUserEventStatus failed");
        }
        cl_event last = events[static_cast<std::size_t>(f)].get();
        if (rig.api.WaitForEvents(1, &last) != CL_SUCCESS || rig.api.Finish(queue.get()) != CL_SUCCESS) {
            return Outcome::fail("waiting for the dependency graph failed");
        }
        std::uint32_t out[3] = {};
        if (rig.api.EnqueueReadBuffer(queue.get(), buffer.get(), CL_TRUE, 0, sizeof(out), out, 0, nullptr, nullptr) != CL_SUCCESS) {
            return Outcome::fail("clEnqueueReadBuffer failed");
        }
        if (out[0] != 115u || out[1] != 8u || out[2] != 3u) {
            return Outcome::fail("round " + std::to_string(round) + ": slots read " + std::to_string(out[0]) + "," + std::to_string(out[1]) + "," +
                                 std::to_string(out[2]) + ", the dependency order gives 115,8,3");
        }
        const std::uint32_t reset[3] = {0, 0, 0};
        if (rig.api.EnqueueWriteBuffer(queue.get(), buffer.get(), CL_TRUE, 0, sizeof(reset), reset, 0, nullptr, nullptr) != CL_SUCCESS) {
            return Outcome::fail("clEnqueueWriteBuffer failed");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::sync
