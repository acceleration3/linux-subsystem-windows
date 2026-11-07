#include "sanity/cl/queue_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <thread>

namespace lsw::suites::sanity::cl {

namespace {

struct PipelineResult {
    std::string error;
    std::vector<std::uint32_t> expected;
    std::vector<std::uint32_t> actual;
};

class Devices {
public:
    Devices(ClApi& api, cl_platform_id platform, const std::vector<cl_device_id>& devices) : api_(api), devices_(devices) {
        cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform), 0};
        cl_int code = CL_SUCCESS;
        context_ = api.CreateContext(properties, static_cast<cl_uint>(devices.size()), devices.data(), nullptr, nullptr, &code);
        code_ = code;
        for (std::size_t i = 0; context_ != nullptr && i < devices.size() * 2; ++i) {
            const cl_device_id device = devices[i % devices.size()];
            cl_command_queue queue = nullptr;
            queue = api.CreateCommandQueueWithProperties != nullptr ? api.CreateCommandQueueWithProperties(context_, device, nullptr, &code)
                                                                    : api.CreateCommandQueue(context_, device, 0, &code);
            if (queue == nullptr) {
                code_ = code;
                break;
            }
            queues_.push_back(queue);
        }
    }

    Devices(const Devices&) = delete;
    Devices& operator=(const Devices&) = delete;

    ~Devices() {
        for (cl_command_queue queue : queues_) {
            api_.Finish(queue);
        }
        for (cl_command_queue queue : queues_) {
            api_.ReleaseCommandQueue(queue);
        }
        if (context_ != nullptr) {
            api_.ReleaseContext(context_);
        }
    }

    [[nodiscard]] cl_context context() const { return context_; }

    [[nodiscard]] cl_int code() const { return code_; }

    [[nodiscard]] const std::vector<cl_command_queue>& queues() const { return queues_; }

    [[nodiscard]] const std::vector<cl_device_id>& devices() const { return devices_; }

private:
    ClApi& api_;
    std::vector<cl_device_id> devices_;
    cl_context context_ = nullptr;
    std::vector<cl_command_queue> queues_;
    cl_int code_ = CL_SUCCESS;
};

void run_pipeline(ClApi& api, cl_context context, cl_program program, const std::vector<cl_command_queue>& queues, std::size_t thread, PipelineResult& out) {
    const std::size_t words = 16384;
    const std::size_t bytes = words * 4;
    const int stages = 10;
    cl_int code = CL_SUCCESS;
    cl_kernel kernel = api.CreateKernel(program, "lsw_step", &code);
    cl_mem buffer = api.CreateBuffer(context, CL_MEM_READ_WRITE, bytes, nullptr, &code);
    if (kernel == nullptr || buffer == nullptr) {
        out.error = "creating the pipeline objects returned " + ClTools::error_name(code);
        if (kernel != nullptr) {
            api.ReleaseKernel(kernel);
        }
        if (buffer != nullptr) {
            api.ReleaseMemObject(buffer);
        }
        return;
    }
    const std::vector<std::uint32_t> input = Kit::words(3100 + thread, words);
    out.expected = input;
    out.actual.assign(words, 0);
    std::vector<cl_event> events;
    cl_event event = nullptr;
    code = api.EnqueueWriteBuffer(queues[thread % queues.size()], buffer, CL_FALSE, 0, bytes, input.data(), 0, nullptr, &event);
    events.push_back(event);
    cl_mem arg_buffer = buffer;
    for (int s = 0; s < stages && code == CL_SUCCESS; ++s) {
        const cl_uint mul = 3;
        const cl_uint add = static_cast<cl_uint>(s);
        cl_event next = nullptr;
        const std::size_t global = words;
        cl_command_queue queue = queues[(thread + static_cast<std::size_t>(s)) % queues.size()];
        api.SetKernelArg(kernel, 0, sizeof(arg_buffer), &arg_buffer);
        api.SetKernelArg(kernel, 1, sizeof(mul), &mul);
        api.SetKernelArg(kernel, 2, sizeof(add), &add);
        code = api.EnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, nullptr, 1, &events.back(), &next);
        events.push_back(next);
        for (std::size_t i = 0; i < out.expected.size(); ++i) {
            out.expected[i] = out.expected[i] * mul + add;
        }
    }
    if (code == CL_SUCCESS) {
        code = api.EnqueueReadBuffer(queues[(thread + 1) % queues.size()], buffer, CL_FALSE, 0, bytes, out.actual.data(), 1, &events.back(), &event);
        events.push_back(event);
    }
    if (code != CL_SUCCESS) {
        out.error = "the pipeline returned " + ClTools::error_name(code);
    } else {
        code = api.WaitForEvents(1, &events.back());
        if (code != CL_SUCCESS) {
            out.error = "waiting for the pipeline returned " + ClTools::error_name(code);
        }
    }
    for (cl_command_queue queue : queues) {
        api.Finish(queue);
    }
    for (cl_event e : events) {
        if (e != nullptr) {
            api.ReleaseEvent(e);
        }
    }
    api.ReleaseKernel(kernel);
    api.ReleaseMemObject(buffer);
}

void run_threads(ClApi& api, cl_context context, cl_program program, const std::vector<cl_command_queue>& queues, std::size_t threads,
                 Context& test, Problems& problems, const std::string& tag) {
    std::vector<PipelineResult> results(threads);
    std::vector<std::thread> workers;
    for (std::size_t t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() { run_pipeline(api, context, program, queues, t, results[t]); });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (std::size_t t = 0; t < threads; ++t) {
        if (!results[t].error.empty()) {
            problems.add(tag + " thread " + std::to_string(t) + ": " + results[t].error);
        } else {
            Kit::same_words(test, problems, tag + " thread " + std::to_string(t), results[t].expected, results[t].actual);
        }
    }
}

}  // namespace

Outcome QueueChecks::multiple_queues_concurrent(Context& context) {
    ClApi api;
    ClDeviceList list;
    std::string error;
    if (!ClRig::list_devices(api, list, error)) {
        return Kit::open_failed(error);
    }
    Problems problems;
    {
        ClRig rig;
        ClRigOptions options;
        if (!rig.open(options, error)) {
            return Kit::open_failed(error);
        }
        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        std::vector<cl_command_queue> queues;
        std::vector<ClOwned<cl_command_queue>> owned;
        for (int i = 0; i < 4; ++i) {
            cl_int code = CL_SUCCESS;
            cl_command_queue queue = rig.create_queue(0, code);
            if (queue == nullptr) {
                problems.add("creating queue " + std::to_string(i) + " returned " + ClTools::error_name(code));
                return problems.outcome();
            }
            queues.push_back(queue);
            owned.emplace_back(rig, queue);
        }
        run_threads(rig.api, rig.context, program.get(), queues, 4, context, problems, "one device, four queues");
        rig.drain();
    }
    std::vector<ClDeviceRef> same_platform;
    for (const ClDeviceRef& ref : list.refs) {
        if (same_platform.empty() || ref.platform == same_platform.front().platform) {
            same_platform.push_back(ref);
        }
    }
    if (same_platform.size() < 2) {
        context.measure("cl.two_device_context", "needs two devices on one platform; " + std::to_string(list.refs.size()) + " matched");
        return problems.outcome();
    }
    std::vector<cl_device_id> ids;
    for (const ClDeviceRef& ref : same_platform) {
        ids.push_back(ref.device);
    }
    Devices devices(api, same_platform.front().platform, ids);
    if (devices.context() == nullptr || devices.queues().size() != ids.size() * 2) {
        problems.add("a context over " + std::to_string(ids.size()) + " devices could not be set up: " + ClTools::error_name(devices.code()));
        return problems.outcome();
    }
    cl_int code = CL_SUCCESS;
    const char* text = ClSources::kStep;
    cl_program program = api.CreateProgramWithSource(devices.context(), 1, &text, nullptr, &code);
    if (program == nullptr) {
        problems.add("clCreateProgramWithSource on the two-device context returned " + ClTools::error_name(code));
        return problems.outcome();
    }
    code = api.BuildProgram(program, static_cast<cl_uint>(ids.size()), ids.data(), "", nullptr, nullptr);
    if (code != CL_SUCCESS) {
        problems.add("building for both devices returned " + ClTools::error_name(code));
    } else {
        run_threads(api, devices.context(), program, devices.queues(), 4, context, problems, "two devices, " + std::to_string(devices.queues().size()) + " queues");
    }
    for (cl_command_queue queue : devices.queues()) {
        api.Finish(queue);
    }
    api.ReleaseProgram(program);
    context.measure("cl.two_device_context", "ran");
    return problems.outcome();
}

Outcome QueueChecks::finish_flush_release(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        const char* source =
            "__kernel void lsw_spin(__global uint* d, uint rounds) {\n"
            "    size_t i = get_global_id(0); uint x = d[i];\n"
            "    for (uint r = 0; r < rounds; ++r) { x = x * 1664525u + 1013904223u; }\n"
            "    d[i] = x;\n"
            "}\n";
        ClProgram program;
        if (!Kit::compile(rig, program, source, "", problems, "spin kernel")) {
            return problems.outcome();
        }
        const std::size_t items = 1 << 16;
        const std::size_t bytes = items * 4;
        const std::vector<std::uint32_t> seed = Kit::words(3200, items);
        auto lcg = [&](std::uint32_t value, std::uint32_t rounds) {
            for (std::uint32_t r = 0; r < rounds; ++r) {
                value = value * 1664525u + 1013904223u;
            }
            return value;
        };
        cl_int code = CL_SUCCESS;
        {
            ClKernel spin = Kit::kernel(program, "lsw_spin", problems);
            ClOwned<cl_mem> data = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint32_t*>(seed.data()), code);
            if (!spin || !data || spin.set(data, cl_uint(200)) != CL_SUCCESS) {
                return Outcome::fail("setup: " + ClTools::error_name(code));
            }
            std::vector<cl_event> events;
            for (int i = 0; i < 8; ++i) {
                cl_event event = nullptr;
                code = spin.run(items, 0, &event);
                events.push_back(event);
                if (code != CL_SUCCESS) {
                    problems.add("kernel " + std::to_string(i) + " returned " + ClTools::error_name(code));
                    break;
                }
            }
            Kit::expect(problems, "clFlush", rig.api.Flush(rig.queue), {CL_SUCCESS});
            Kit::expect(problems, "clFinish", rig.api.Finish(rig.queue), {CL_SUCCESS});
            for (std::size_t i = 0; i < events.size(); ++i) {
                if (events[i] != nullptr && Kit::event_status(rig, events[i]) != CL_COMPLETE) {
                    problems.add("event " + std::to_string(i) + " is not CL_COMPLETE after clFinish returned");
                }
            }
            std::vector<std::uint32_t> got;
            if (Kit::read_words(rig, data.get(), items, got, problems, "after clFinish")) {
                std::vector<std::uint32_t> expected(items);
                for (std::size_t i = 0; i < items; ++i) {
                    expected[i] = lcg(seed[i], 1600);
                }
                Kit::same_words(context, problems, "results after eight kernels and clFinish", expected, got);
            }
            for (cl_event event : events) {
                if (event != nullptr) {
                    rig.api.ReleaseEvent(event);
                }
            }
        }
        {
            cl_command_queue short_lived_raw = rig.create_queue(0, code);
            ClOwned<cl_command_queue> keeper(rig, short_lived_raw);
            ClKernel spin = Kit::kernel(program, "lsw_spin", problems);
            ClOwned<cl_mem> data = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint32_t*>(seed.data()), code);
            ClOwned<cl_mem> other = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint32_t*>(seed.data()), code);
            if (!keeper || !spin || !data || !other || spin.set(data, cl_uint(200)) != CL_SUCCESS) {
                problems.add("release setup failed: " + ClTools::error_name(code));
                return problems.outcome();
            }
            cl_event tail = nullptr;
            for (int i = 0; i < 4; ++i) {
                cl_event event = nullptr;
                code = spin.run_on(keeper.get(), items, 0, 0, nullptr, &event);
                if (code != CL_SUCCESS) {
                    problems.add("queued kernel " + std::to_string(i) + " returned " + ClTools::error_name(code));
                    break;
                }
                if (i == 3) {
                    tail = event;
                } else {
                    rig.api.ReleaseEvent(event);
                }
            }
            if (tail == nullptr) {
                return problems.outcome();
            }
            rig.api.Flush(keeper.get());
            cl_mem retained = data.get();
            rig.api.RetainMemObject(retained);
            data.reset();
            cl_command_queue raw = keeper.detach();
            Kit::expect(problems, "clReleaseCommandQueue with commands in flight", rig.api.ReleaseCommandQueue(raw), {CL_SUCCESS});
            rig.forget_queue(raw);
            if (rig.api.WaitForEvents(1, &tail) != CL_SUCCESS) {
                problems.add("the last command of a released queue never completed");
            }
            rig.api.ReleaseEvent(tail);
            std::vector<std::uint32_t> got(items);
            if (Kit::read(rig, retained, got.data(), bytes) == CL_SUCCESS) {
                std::vector<std::uint32_t> expected(items);
                for (std::size_t i = 0; i < items; ++i) {
                    expected[i] = lcg(seed[i], 800);
                }
                Kit::same_words(context, problems, "memory used by commands of a released queue", expected, got);
            } else {
                problems.add("reading the memory used by a released queue failed");
            }
            rig.drain();
            rig.api.ReleaseMemObject(retained);
            std::vector<std::uint32_t> neighbour(items);
            if (Kit::read(rig, other.get(), neighbour.data(), bytes) == CL_SUCCESS) {
                Kit::same_words(context, problems, "unrelated memory next to released objects", seed, neighbour);
            }
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
