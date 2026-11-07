#include "sanity/cl/event_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace lsw::suites::sanity::cl {

namespace {

struct CallbackCount {
    std::atomic<int> calls{0};
    std::atomic<int> last_status{12345};
};

void CL_CALLBACK count_callback(cl_event, cl_int status, void* user) {
    auto* counter = static_cast<CallbackCount*>(user);
    counter->last_status = status;
    ++counter->calls;
}

bool wait_for_calls(const CallbackCount& counter, int wanted) {
    for (int i = 0; i < 200 && counter.calls.load() < wanted; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    return counter.calls.load() >= wanted;
}

class UserEvents {
public:
    explicit UserEvents(ClRig& rig) : rig_(rig) {}

    UserEvents(const UserEvents&) = delete;
    UserEvents& operator=(const UserEvents&) = delete;

    ~UserEvents() {
        for (cl_event event : events_) {
            rig_.api.SetUserEventStatus(event, CL_COMPLETE);
        }
        rig_.drain();
        for (cl_event event : events_) {
            rig_.api.ReleaseEvent(event);
        }
    }

    cl_event make(cl_int& code) {
        cl_event event = rig_.api.CreateUserEvent(rig_.context, &code);
        if (event != nullptr) {
            events_.push_back(event);
        }
        return event;
    }

private:
    ClRig& rig_;
    std::vector<cl_event> events_;
};


}  // namespace

Outcome EventChecks::in_order_ordering(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        ClKernel step_a = Kit::kernel(program, "lsw_step", problems);
        ClKernel step_b = Kit::kernel(program, "lsw_step", problems);
        if (!step_a || !step_b) {
            return problems.outcome();
        }
        const std::size_t words = 4096;
        const std::size_t bytes = words * 4;
        const int rounds = 24;
        cl_int code = CL_SUCCESS;
        ClOwned<cl_mem> a = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
        ClOwned<cl_mem> b = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, nullptr, code);
        if (!a || !b) {
            return Outcome::fail("buffers: " + ClTools::error_name(code));
        }
        std::vector<std::vector<std::uint32_t>> inputs(rounds);
        std::vector<std::vector<std::uint32_t>> outputs(rounds, std::vector<std::uint32_t>(words, 0xDEADBEEFu));
        const std::uint32_t fill_pattern = 0xA5A5A5A5u;
        for (int r = 0; r < rounds && problems.empty(); ++r) {
            inputs[static_cast<std::size_t>(r)] = Kit::words(2700 + static_cast<std::uint64_t>(r), words);
            const cl_uint ur = static_cast<cl_uint>(r);
            code = rig.api.EnqueueWriteBuffer(rig.queue, a.get(), CL_FALSE, 0, bytes, inputs[static_cast<std::size_t>(r)].data(), 0, nullptr, nullptr);
            if (code == CL_SUCCESS) {
                code = step_a.set(a, cl_uint(3), ur);
            }
            if (code == CL_SUCCESS) {
                code = step_a.run(words);
            }
            if (code == CL_SUCCESS) {
                code = rig.api.EnqueueCopyBuffer(rig.queue, a.get(), b.get(), 0, 0, bytes, 0, nullptr, nullptr);
            }
            if (code == CL_SUCCESS) {
                code = step_b.set(b, cl_uint(5), cl_uint(1));
            }
            if (code == CL_SUCCESS) {
                code = step_b.run(words);
            }
            if (code == CL_SUCCESS && rig.version.at_least(1, 2)) {
                code = rig.api.EnqueueFillBuffer(rig.queue, b.get(), &fill_pattern, 4, 0, 256, 0, nullptr, nullptr);
            }
            if (code == CL_SUCCESS) {
                code = rig.api.EnqueueReadBuffer(rig.queue, b.get(), CL_FALSE, 0, bytes, outputs[static_cast<std::size_t>(r)].data(), 0, nullptr, nullptr);
            }
            if (code != CL_SUCCESS) {
                problems.add("round " + std::to_string(r) + " returned " + ClTools::error_name(code));
            }
        }
        rig.api.Finish(rig.queue);
        for (int r = 0; r < rounds && problems.empty(); ++r) {
            std::vector<std::uint32_t> expected(words);
            for (std::size_t i = 0; i < words; ++i) {
                expected[i] = (inputs[static_cast<std::size_t>(r)][i] * 3u + static_cast<std::uint32_t>(r)) * 5u + 1u;
                if (rig.version.at_least(1, 2) && i < 64) {
                    expected[i] = fill_pattern;
                }
            }
            Kit::same_words(context, problems, "round " + std::to_string(r) + " of the in-order chain", expected, outputs[static_cast<std::size_t>(r)]);
        }
        return problems.outcome();
    });
}

Outcome EventChecks::out_of_order_deps(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        cl_command_queue_properties host_properties = 0;
        rig.info(CL_DEVICE_QUEUE_PROPERTIES, host_properties);
        cl_int code = CL_SUCCESS;
        if ((host_properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) == 0) {
            cl_command_queue queue = rig.create_queue(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, code);
            if (queue != nullptr) {
                ClOwned<cl_command_queue> owned(rig, queue);
                problems.add("an out-of-order queue was created although CL_DEVICE_QUEUE_PROPERTIES (0x" + Text::hex(host_properties, 4) + ") lacks CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE");
            } else {
                Kit::expect(problems, "creating an out-of-order queue on a device that does not offer it", code, {CL_INVALID_QUEUE_PROPERTIES});
            }
            return problems.outcome();
        }
        cl_command_queue raw = rig.create_queue(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, code);
        if (raw == nullptr) {
            return Outcome::fail("creating an out-of-order queue returned " + ClTools::error_name(code));
        }
        ClOwned<cl_command_queue> queue(rig, raw);
        ClProgram program;
        if (!Kit::compile(rig, program, ClSources::kVectorAdd, "", problems, "vector add")) {
            return problems.outcome();
        }
        ClProgram steps;
        if (!Kit::compile(rig, steps, ClSources::kStep, "", problems, "step kernel")) {
            return problems.outcome();
        }
        ClKernel add = Kit::kernel(program, "lsw_add", problems);
        if (!add) {
            return problems.outcome();
        }
        const std::size_t words = 8192;
        const std::size_t bytes = words * 4;
        const std::vector<std::uint32_t> da = Kit::words(2800, words);
        const std::vector<std::uint32_t> db = Kit::words(2801, words);
        ClOwned<cl_mem> a = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "A");
        ClOwned<cl_mem> b = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "B");
        ClOwned<cl_mem> c = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "C");
        if (!a || !b || !c) {
            return problems.outcome();
        }
        cl_event ev_a = nullptr;
        cl_event ev_b = nullptr;
        cl_event ev_c = nullptr;
        cl_event ev_r = nullptr;
        std::vector<std::uint32_t> result(words, 0);
        code = rig.api.EnqueueWriteBuffer(queue.get(), a.get(), CL_FALSE, 0, bytes, da.data(), 0, nullptr, &ev_a);
        if (code == CL_SUCCESS) {
            code = rig.api.EnqueueWriteBuffer(queue.get(), b.get(), CL_FALSE, 0, bytes, db.data(), 0, nullptr, &ev_b);
        }
        ClOwned<cl_event> oa(rig, ev_a);
        ClOwned<cl_event> ob(rig, ev_b);
        if (code == CL_SUCCESS) {
            code = add.set(a, b, c);
        }
        const cl_event both[2] = {ev_a, ev_b};
        if (code == CL_SUCCESS) {
            code = add.run_on(queue.get(), words, 0, 2, both, &ev_c);
        }
        ClOwned<cl_event> oc(rig, ev_c);
        if (code == CL_SUCCESS) {
            code = rig.api.EnqueueReadBuffer(queue.get(), c.get(), CL_FALSE, 0, bytes, result.data(), 1, &ev_c, &ev_r);
        }
        ClOwned<cl_event> orr(rig, ev_r);
        if (code != CL_SUCCESS) {
            problems.add("the dependent chain returned " + ClTools::error_name(code));
            rig.api.Finish(queue.get());
            return problems.outcome();
        }
        if (Kit::wait(rig, ev_r, problems, "dependent read")) {
            std::vector<std::uint32_t> expected(words);
            for (std::size_t i = 0; i < words; ++i) {
                expected[i] = da[i] + db[i];
            }
            Kit::same_words(context, problems, "out-of-order dependency chain", expected, result);
        }
        ClKernel stepper[3];
        ClOwned<cl_mem> independent[3];
        std::vector<std::uint32_t> initial(words, 7);
        cl_event done[3] = {};
        ClOwned<cl_event> owned_done[3];
        for (int k = 0; k < 3; ++k) {
            stepper[k] = Kit::kernel(steps, "lsw_step", problems);
            independent[k] = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, initial.data(), code);
            if (!stepper[k] || !independent[k] || stepper[k].set(independent[k], cl_uint(k + 2), cl_uint(1)) != CL_SUCCESS) {
                problems.add("preparing independent command " + std::to_string(k) + " failed");
                rig.api.Finish(queue.get());
                return problems.outcome();
            }
            code = stepper[k].run_on(queue.get(), words, 0, 0, nullptr, &done[k]);
            owned_done[k].adopt(rig, code == CL_SUCCESS ? done[k] : nullptr);
            Kit::expect(problems, "independent command " + std::to_string(k), code, {CL_SUCCESS});
        }
        cl_event marker = nullptr;
        code = rig.api.EnqueueMarkerWithWaitList(queue.get(), 0, nullptr, &marker);
        ClOwned<cl_event> owned_marker(rig, code == CL_SUCCESS ? marker : nullptr);
        if (code != CL_SUCCESS) {
            problems.add("clEnqueueMarkerWithWaitList returned " + ClTools::error_name(code));
        } else if (Kit::wait(rig, marker, problems, "marker")) {
            for (int k = 0; k < 3; ++k) {
                if (Kit::event_status(rig, done[k]) != CL_COMPLETE) {
                    problems.add("the marker completed before independent command " + std::to_string(k) + " did");
                }
            }
        }
        for (int k = 0; k < 3; ++k) {
            std::vector<std::uint32_t> got(words);
            if (Kit::read(rig, independent[k].get(), got.data(), bytes) == CL_SUCCESS) {
                const std::vector<std::uint32_t> expected(words, 7u * static_cast<std::uint32_t>(k + 2) + 1u);
                Kit::same_words(context, problems, "independent command " + std::to_string(k), expected, got);
            }
        }
        const std::vector<std::uint32_t> payload = Kit::words(2802, words);
        ClOwned<cl_mem> late = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "barrier buffer");
        ClKernel late_step = Kit::kernel(steps, "lsw_step", problems);
        if (late && late_step && late_step.set(late, cl_uint(2), cl_uint(0)) == CL_SUCCESS) {
            code = rig.api.EnqueueWriteBuffer(queue.get(), late.get(), CL_FALSE, 0, bytes, payload.data(), 0, nullptr, nullptr);
            cl_event barrier = nullptr;
            if (code == CL_SUCCESS) {
                code = rig.api.EnqueueBarrierWithWaitList(queue.get(), 0, nullptr, &barrier);
            }
            ClOwned<cl_event> owned_barrier(rig, code == CL_SUCCESS ? barrier : nullptr);
            cl_event after = nullptr;
            if (code == CL_SUCCESS) {
                code = late_step.run_on(queue.get(), words, 0, 0, nullptr, &after);
            }
            ClOwned<cl_event> owned_after(rig, code == CL_SUCCESS ? after : nullptr);
            if (code != CL_SUCCESS) {
                problems.add("write, barrier, kernel returned " + ClTools::error_name(code));
            } else if (Kit::wait(rig, after, problems, "kernel after the barrier")) {
                std::vector<std::uint32_t> got(words);
                if (Kit::read(rig, late.get(), got.data(), bytes) == CL_SUCCESS) {
                    std::vector<std::uint32_t> expected(words);
                    for (std::size_t i = 0; i < words; ++i) {
                        expected[i] = payload[i] * 2u;
                    }
                    Kit::same_words(context, problems, "kernel after a barrier following a write", expected, got);
                }
            }
        }
        rig.api.Finish(queue.get());
        return problems.outcome();
    });
}

Outcome EventChecks::user_events_callbacks(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        UserEvents events(rig);
        cl_int code = CL_SUCCESS;
        cl_command_queue second_raw = rig.create_queue(0, code);
        if (second_raw == nullptr) {
            return Outcome::fail("second queue: " + ClTools::error_name(code));
        }
        ClOwned<cl_command_queue> second(rig, second_raw);
        const std::size_t bytes = 4096;
        const std::vector<std::uint8_t> before = Kit::bytes(2900, bytes);
        const std::vector<std::uint8_t> after = Kit::bytes(2901, bytes);
        ClOwned<cl_mem> buffer = Kit::buffer(rig, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, const_cast<std::uint8_t*>(before.data()), code);
        if (!buffer) {
            return Outcome::fail("buffer: " + ClTools::error_name(code));
        }
        cl_event user = events.make(code);
        if (user == nullptr) {
            return Outcome::fail("clCreateUserEvent returned " + ClTools::error_name(code));
        }
        cl_command_type type = 0;
        rig.api.GetEventInfo(user, CL_EVENT_COMMAND_TYPE, sizeof(type), &type, nullptr);
        if (type != CL_COMMAND_USER) {
            problems.add("a user event reports command type 0x" + Text::hex(type, 4));
        }
        if (Kit::event_status(rig, user) != CL_SUBMITTED) {
            problems.add("a new user event is not CL_SUBMITTED");
        }
        CallbackCount on_user;
        Kit::expect(problems, "clSetEventCallback on a user event", rig.api.SetEventCallback(user, CL_COMPLETE, count_callback, &on_user), {CL_SUCCESS});
        cl_event gated = nullptr;
        code = rig.api.EnqueueWriteBuffer(rig.queue, buffer.get(), CL_FALSE, 0, bytes, after.data(), 1, &user, &gated);
        if (code != CL_SUCCESS || gated == nullptr) {
            problems.add("a write waiting on a user event returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        ClOwned<cl_event> owned_gated(rig, gated);
        rig.api.Flush(rig.queue);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const cl_int waiting = Kit::event_status(rig, gated);
        if (waiting != CL_QUEUED && waiting != CL_SUBMITTED) {
            problems.add("a command waiting on an unset user event reports status " + std::to_string(waiting) + " after 150 ms");
        }
        std::vector<std::uint8_t> seen(bytes);
        if (rig.api.EnqueueReadBuffer(second.get(), buffer.get(), CL_TRUE, 0, bytes, seen.data(), 0, nullptr, nullptr) == CL_SUCCESS) {
            Kit::same_bytes(context, problems, "buffer contents while the write waits for its user event", before.data(), seen.data(), bytes);
        }
        if (on_user.calls.load() != 0) {
            problems.add("the user event callback ran before the status was set");
        }
        Kit::expect(problems, "clSetUserEventStatus(CL_COMPLETE)", rig.api.SetUserEventStatus(user, CL_COMPLETE), {CL_SUCCESS});
        if (Kit::wait(rig, gated, problems, "gated write")) {
            if (Kit::event_status(rig, gated) != CL_COMPLETE) {
                problems.add("the gated write is not complete after its user event was set");
            }
            if (rig.api.EnqueueReadBuffer(second.get(), buffer.get(), CL_TRUE, 0, bytes, seen.data(), 0, nullptr, nullptr) == CL_SUCCESS) {
                Kit::same_bytes(context, problems, "buffer contents after the user event was set", after.data(), seen.data(), bytes);
            }
        }
        if (!wait_for_calls(on_user, 1) || on_user.calls.load() != 1 || on_user.last_status.load() != CL_COMPLETE) {
            problems.add("the user event callback ran " + std::to_string(on_user.calls.load()) + " times with status " + std::to_string(on_user.last_status.load()) + ", expected once with CL_COMPLETE");
        }
        Kit::expect(problems, "setting a user event status twice", rig.api.SetUserEventStatus(user, CL_COMPLETE), {CL_INVALID_OPERATION});
        CallbackCount late;
        Kit::expect(problems, "clSetEventCallback after completion", rig.api.SetEventCallback(user, CL_COMPLETE, count_callback, &late), {CL_SUCCESS});
        if (!wait_for_calls(late, 1) || late.calls.load() != 1) {
            problems.add("a callback registered on a completed event ran " + std::to_string(late.calls.load()) + " times, expected once");
        }
        Kit::expect(problems, "clSetEventCallback with an invalid type", rig.api.SetEventCallback(user, 77, count_callback, &late), {CL_INVALID_VALUE});
        Kit::expect(problems, "clSetEventCallback with a NULL function", rig.api.SetEventCallback(user, CL_COMPLETE, nullptr, &late), {CL_INVALID_VALUE});
        cl_event pending = events.make(code);
        if (pending != nullptr) {
            Kit::expect(problems, "clSetUserEventStatus with CL_RUNNING", rig.api.SetUserEventStatus(pending, CL_RUNNING), {CL_INVALID_VALUE});
        }

        cl_event failing = events.make(code);
        if (failing != nullptr) {
            cl_event victim = nullptr;
            std::vector<std::uint8_t> scratch(bytes, 1);
            code = rig.api.EnqueueWriteBuffer(rig.queue, buffer.get(), CL_FALSE, 0, bytes, scratch.data(), 1, &failing, &victim);
            if (code != CL_SUCCESS || victim == nullptr) {
                problems.add("a write waiting on a second user event returned " + ClTools::error_name(code));
            } else {
                ClOwned<cl_event> owned_victim(rig, victim);
                CallbackCount on_victim;
                rig.api.SetEventCallback(victim, CL_COMPLETE, count_callback, &on_victim);
                Kit::expect(problems, "clSetUserEventStatus(-5)", rig.api.SetUserEventStatus(failing, -5), {CL_SUCCESS});
                const cl_int waited = rig.api.WaitForEvents(1, &victim);
                if (rig.version.at_least(1, 2)) {
                    Kit::expect(problems, "clWaitForEvents on a command whose dependency failed", waited, {CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST});
                } else if (waited == CL_SUCCESS) {
                    problems.add("clWaitForEvents succeeded on a command whose dependency failed");
                }
                const cl_int status = Kit::event_status(rig, victim);
                if (status >= 0) {
                    problems.add("the dependent command has status " + std::to_string(status) + " after its dependency failed, expected a negative value");
                }
                if (!wait_for_calls(on_victim, 1) || on_victim.calls.load() != 1 || on_victim.last_status.load() >= 0) {
                    problems.add("the callback of the failed command ran " + std::to_string(on_victim.calls.load()) + " times with status " + std::to_string(on_victim.last_status.load()) + ", expected once with a negative status");
                }
            }
        }

        ClProgram program;
        if (Kit::compile(rig, program, ClSources::kStep, "", problems, "step kernel")) {
            ClKernel step = Kit::kernel(program, "lsw_step", problems);
            ClOwned<cl_mem> data = Kit::buffer(rig, CL_MEM_READ_WRITE, 4096, problems, "kernel buffer");
            if (step && data && step.set(data, cl_uint(1), cl_uint(1)) == CL_SUCCESS) {
                CallbackCount submitted;
                CallbackCount running;
                CallbackCount complete;
                cl_event event = nullptr;
                code = step.run(1024, 0, &event);
                if (code != CL_SUCCESS || event == nullptr) {
                    problems.add("the callback kernel returned " + ClTools::error_name(code));
                } else {
                    ClOwned<cl_event> owned(rig, event);
                    Kit::expect(problems, "callback CL_SUBMITTED", rig.api.SetEventCallback(event, CL_SUBMITTED, count_callback, &submitted), {CL_SUCCESS});
                    Kit::expect(problems, "callback CL_RUNNING", rig.api.SetEventCallback(event, CL_RUNNING, count_callback, &running), {CL_SUCCESS});
                    Kit::expect(problems, "callback CL_COMPLETE", rig.api.SetEventCallback(event, CL_COMPLETE, count_callback, &complete), {CL_SUCCESS});
                    rig.api.Finish(rig.queue);
                    const bool all = wait_for_calls(submitted, 1) && wait_for_calls(running, 1) && wait_for_calls(complete, 1);
                    if (!all || submitted.calls.load() != 1 || running.calls.load() != 1 || complete.calls.load() != 1) {
                        problems.add("callbacks ran " + std::to_string(submitted.calls.load()) + "/" + std::to_string(running.calls.load()) + "/" + std::to_string(complete.calls.load()) + " times for SUBMITTED/RUNNING/COMPLETE, expected once each");
                    }
                    if (complete.last_status.load() != CL_COMPLETE) {
                        problems.add("the CL_COMPLETE callback received status " + std::to_string(complete.last_status.load()));
                    }
                }
            }
        }
        return problems.outcome();
    });
}

Outcome EventChecks::profiling_monotonic(Context& context) {
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
        ClKernel spin = Kit::kernel(program, "lsw_spin", problems);
        const std::size_t items = 1 << 16;
        const std::size_t bytes = items * 4;
        ClOwned<cl_mem> data = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "profiling buffer");
        ClOwned<cl_mem> copy = Kit::buffer(rig, CL_MEM_READ_WRITE, bytes, problems, "profiling copy");
        if (!spin || !data || !copy) {
            return problems.outcome();
        }
        const std::vector<std::uint32_t> host = Kit::words(3000, items);
        std::vector<std::uint32_t> back(items);
        struct Times {
            cl_ulong queued = 0, submit = 0, start = 0, end = 0;
            bool ok = false;
        };
        auto times = [&](cl_event event) {
            Times t;
            t.ok = rig.api.GetEventProfilingInfo(event, CL_PROFILING_COMMAND_QUEUED, 8, &t.queued, nullptr) == CL_SUCCESS &&
                   rig.api.GetEventProfilingInfo(event, CL_PROFILING_COMMAND_SUBMIT, 8, &t.submit, nullptr) == CL_SUCCESS &&
                   rig.api.GetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, 8, &t.start, nullptr) == CL_SUCCESS &&
                   rig.api.GetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, 8, &t.end, nullptr) == CL_SUCCESS;
            return t;
        };
        auto monotonic = [&](const std::string& what, const Times& t) {
            if (!t.ok) {
                problems.add(what + ": clGetEventProfilingInfo failed");
            } else if (!(t.queued <= t.submit && t.submit <= t.start && t.start <= t.end)) {
                problems.add(what + ": QUEUED " + std::to_string(t.queued) + ", SUBMIT " + std::to_string(t.submit) + ", START " + std::to_string(t.start) + ", END " + std::to_string(t.end) + " are not ordered");
            } else if (t.end - t.start > 20'000'000'000ull) {
                problems.add(what + ": runs for more than 20 s according to its timestamps");
            }
        };
        cl_event e_write = nullptr;
        cl_event e_kernel = nullptr;
        cl_event e_copy = nullptr;
        cl_event e_read = nullptr;
        cl_int code = rig.api.EnqueueWriteBuffer(rig.queue, data.get(), CL_FALSE, 0, bytes, host.data(), 0, nullptr, &e_write);
        ClOwned<cl_event> o1(rig, e_write);
        if (code == CL_SUCCESS) {
            code = spin.set(data, cl_uint(256));
        }
        if (code == CL_SUCCESS) {
            code = spin.run_on(rig.queue, items, 0, 1, &e_write, &e_kernel);
        }
        ClOwned<cl_event> o2(rig, e_kernel);
        if (code == CL_SUCCESS) {
            code = rig.api.EnqueueCopyBuffer(rig.queue, data.get(), copy.get(), 0, 0, bytes, 1, &e_kernel, &e_copy);
        }
        ClOwned<cl_event> o3(rig, e_copy);
        if (code == CL_SUCCESS) {
            code = rig.api.EnqueueReadBuffer(rig.queue, copy.get(), CL_FALSE, 0, bytes, back.data(), 1, &e_copy, &e_read);
        }
        ClOwned<cl_event> o4(rig, e_read);
        if (code != CL_SUCCESS) {
            problems.add("the profiled chain returned " + ClTools::error_name(code));
            return problems.outcome();
        }
        rig.api.Finish(rig.queue);
        const Times tw = times(e_write);
        const Times tk = times(e_kernel);
        const Times tc = times(e_copy);
        const Times tr = times(e_read);
        monotonic("write", tw);
        monotonic("kernel", tk);
        monotonic("copy", tc);
        monotonic("read", tr);
        if (tw.ok && tk.ok && tk.start < tw.end) {
            problems.add("the kernel started (" + std::to_string(tk.start) + ") before the write it waits for ended (" + std::to_string(tw.end) + ")");
        }
        if (tk.ok && tc.ok && tc.start < tk.end) {
            problems.add("the copy started before the kernel it waits for ended");
        }
        if (tc.ok && tr.ok && tr.start < tc.end) {
            problems.add("the read started before the copy it waits for ended");
        }
        auto median_duration = [&](cl_uint rounds) {
            std::vector<double> durations;
            for (int i = 0; i < 5; ++i) {
                cl_event event = nullptr;
                if (spin.set(data, rounds) != CL_SUCCESS || spin.run(items, 0, &event) != CL_SUCCESS || event == nullptr) {
                    return -1.0;
                }
                ClOwned<cl_event> owned(rig, event);
                rig.api.Finish(rig.queue);
                const Times t = times(event);
                durations.push_back(static_cast<double>(t.end - t.start));
            }
            std::sort(durations.begin(), durations.end());
            return durations[2];
        };
        const double small = median_duration(64);
        const double large = median_duration(6400);
        context.measure("cl.profile_small_ns", small);
        context.measure("cl.profile_large_ns", large);
        if (small < 0 || large < 0) {
            problems.add("the scaling kernels failed");
        } else if (!(large > small)) {
            problems.add("a kernel with 100x the work reports a duration of " + std::to_string(large) + " ns, not more than " + std::to_string(small) + " ns");
        }
        ClRig plain;
        std::string error;
        ClRigOptions options;
        options.device = -1;
        if (plain.open(options, error)) {
            cl_mem scratch = plain.api.CreateBuffer(plain.context, CL_MEM_READ_WRITE, 64, nullptr, &code);
            ClOwned<cl_mem> owned_scratch(plain, scratch);
            std::uint8_t byte[64] = {};
            cl_event event = nullptr;
            if (scratch != nullptr && plain.api.EnqueueWriteBuffer(plain.queue, scratch, CL_FALSE, 0, 64, byte, 0, nullptr, &event) == CL_SUCCESS) {
                ClOwned<cl_event> owned(plain, event);
                plain.api.Finish(plain.queue);
                cl_ulong value = 0;
                Kit::expect(problems, "profiling info of a command on a queue without profiling", plain.api.GetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, 8, &value, nullptr), {CL_PROFILING_INFO_NOT_AVAILABLE});
            }
            UserEvents user_events(plain);
            cl_event user = user_events.make(code);
            if (user != nullptr) {
                cl_ulong value = 0;
                Kit::expect(problems, "profiling info of a user event", plain.api.GetEventProfilingInfo(user, CL_PROFILING_COMMAND_START, 8, &value, nullptr), {CL_PROFILING_INFO_NOT_AVAILABLE, CL_INVALID_VALUE});
            }
        }
        return problems.outcome();
    }, true);
}

}  // namespace lsw::suites::sanity::cl
