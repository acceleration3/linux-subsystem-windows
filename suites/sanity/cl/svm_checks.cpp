#include "sanity/cl/svm_checks.hpp"

#include "sanity/cl/aligned_block.hpp"
#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace lsw::suites::sanity::cl {

namespace {

class SvmBlock {
public:
    SvmBlock(ClRig& rig, cl_svm_mem_flags flags, std::size_t bytes, cl_uint alignment) : rig_(&rig) {
        pointer_ = rig.api.SVMAlloc(rig.context, flags, bytes, alignment);
    }

    SvmBlock(const SvmBlock&) = delete;
    SvmBlock& operator=(const SvmBlock&) = delete;

    ~SvmBlock() {
        if (pointer_ != nullptr) {
            rig_->drain();
            rig_->api.SVMFree(rig_->context, pointer_);
        }
    }

    [[nodiscard]] void* get() const { return pointer_; }

    [[nodiscard]] std::uint32_t* words() const { return static_cast<std::uint32_t*>(pointer_); }

    [[nodiscard]] explicit operator bool() const { return pointer_ != nullptr; }

private:
    ClRig* rig_;
    void* pointer_ = nullptr;
};

cl_device_svm_capabilities capabilities(const ClRig& rig) {
    cl_device_svm_capabilities caps = 0;
    if (rig.version.at_least(2, 0)) {
        rig.api.GetDeviceInfo(rig.device, CL_DEVICE_SVM_CAPABILITIES, sizeof(caps), &caps, nullptr);
    }
    return caps;
}

std::string caps_text(cl_device_svm_capabilities caps) {
    std::string text;
    auto add = [&](cl_device_svm_capabilities bit, const char* name) {
        if ((caps & bit) != 0) {
            text += std::string(text.empty() ? "" : "|") + name;
        }
    };
    add(CL_DEVICE_SVM_COARSE_GRAIN_BUFFER, "COARSE_GRAIN_BUFFER");
    add(CL_DEVICE_SVM_FINE_GRAIN_BUFFER, "FINE_GRAIN_BUFFER");
    add(CL_DEVICE_SVM_FINE_GRAIN_SYSTEM, "FINE_GRAIN_SYSTEM");
    add(CL_DEVICE_SVM_ATOMICS, "ATOMICS");
    return text.empty() ? "none" : text;
}

const char* kSvmKernels =
    "__kernel void lsw_step(__global uint* d, uint mul, uint add) {\n"
    "    size_t i = get_global_id(0);\n"
    "    d[i] = d[i] * mul + add;\n"
    "}\n";

}  // namespace

Outcome SvmChecks::coarse_grain(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.version.at_least(2, 0) || rig.api.SVMAlloc == nullptr) {
            return Context::skip("shared virtual memory needs OpenCL 2.0; the device reports " + rig.version.text());
        }
        const cl_device_svm_capabilities caps = capabilities(rig);
        context.measure("cl.svm_capabilities", caps_text(caps));
        if ((caps & CL_DEVICE_SVM_COARSE_GRAIN_BUFFER) == 0) {
            void* pointer = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE, 4096, 0);
            if (pointer != nullptr) {
                rig.api.SVMFree(rig.context, pointer);
                problems.add("CL_DEVICE_SVM_CAPABILITIES (" + caps_text(caps) + ") lacks COARSE_GRAIN_BUFFER yet clSVMAlloc returned memory");
            }
            return problems.outcome();
        }
        const std::size_t words = 16384;
        const std::size_t bytes = words * 4;
        SvmBlock block(rig, CL_MEM_READ_WRITE, bytes, 0);
        if (!block) {
            return Outcome::fail("clSVMAlloc(" + ClTools::bytes_text(bytes) + ") returned NULL although COARSE_GRAIN_BUFFER is reported");
        }
        if (rig.profile_full() && reinterpret_cast<std::uintptr_t>(block.get()) % 128 != 0) {
            problems.add("clSVMAlloc with alignment 0 returned a pointer that is not aligned to the largest built-in type (128 bytes)");
        }
        SvmBlock aligned(rig, CL_MEM_READ_WRITE, 5000, 4096);
        if (!aligned) {
            problems.add("clSVMAlloc with alignment 4096 returned NULL");
        } else if (reinterpret_cast<std::uintptr_t>(aligned.get()) % 4096 != 0) {
            problems.add("clSVMAlloc with alignment 4096 returned a misaligned pointer");
        }
        ClProgram program;
        if (!Kit::compile(rig, program, kSvmKernels, "", problems, "SVM kernel")) {
            return problems.outcome();
        }
        ClKernel step = Kit::kernel(program, "lsw_step", problems);
        if (!step) {
            return problems.outcome();
        }
        std::vector<std::uint32_t> data = Kit::words(1300, words);
        cl_int code = rig.api.EnqueueSVMMap(rig.queue, CL_TRUE, CL_MAP_WRITE, block.get(), bytes, 0, nullptr, nullptr);
        Kit::expect(problems, "clEnqueueSVMMap(WRITE)", code, {CL_SUCCESS});
        if (code == CL_SUCCESS) {
            std::memcpy(block.get(), data.data(), bytes);
            Kit::expect(problems, "clEnqueueSVMUnmap", rig.api.EnqueueSVMUnmap(rig.queue, block.get(), 0, nullptr, nullptr), {CL_SUCCESS});
        }
        const cl_uint mul = 7;
        const cl_uint add = 3;
        code = step.set(ClSvm{block.get()}, mul, add);
        if (code == CL_SUCCESS) {
            code = step.run(words);
        }
        Kit::expect(problems, "kernel with an SVM pointer argument", code, {CL_SUCCESS});
        code = rig.api.EnqueueSVMMap(rig.queue, CL_TRUE, CL_MAP_READ, block.get(), bytes, 0, nullptr, nullptr);
        if (code == CL_SUCCESS) {
            std::vector<std::uint32_t> expected(words);
            for (std::size_t i = 0; i < words; ++i) {
                expected[i] = data[i] * mul + add;
            }
            Kit::same_bytes(context, problems, "SVM contents after a kernel and a map", expected.data(), block.get(), bytes);
            Kit::expect(problems, "clEnqueueSVMUnmap", rig.api.EnqueueSVMUnmap(rig.queue, block.get(), 0, nullptr, nullptr), {CL_SUCCESS});
        } else {
            problems.add("clEnqueueSVMMap(READ) returned " + ClTools::error_name(code));
        }
        rig.api.Finish(rig.queue);

        std::vector<std::uint8_t> model(bytes, 0);
        Kit::expect(problems, "SVM zero fill", rig.api.EnqueueSVMMemFill(rig.queue, block.get(), std::vector<std::uint8_t>(4, 0).data(), 4, bytes, 0, nullptr, nullptr), {CL_SUCCESS});
        for (const std::size_t pattern_size : {std::size_t(1), std::size_t(2), std::size_t(4), std::size_t(8), std::size_t(16), std::size_t(32), std::size_t(64), std::size_t(128)}) {
            const std::vector<std::uint8_t> pattern = Kit::bytes(1310 + pattern_size, pattern_size);
            const std::size_t offset = pattern_size * 4;
            const std::size_t size = pattern_size * 9;
            code = rig.api.EnqueueSVMMemFill(rig.queue, static_cast<std::uint8_t*>(block.get()) + offset, pattern.data(), pattern_size, size, 0, nullptr, nullptr);
            if (code != CL_SUCCESS) {
                problems.add("clEnqueueSVMMemFill with pattern size " + std::to_string(pattern_size) + " returned " + ClTools::error_name(code));
                continue;
            }
            for (std::size_t i = 0; i < size; ++i) {
                model[offset + i] = pattern[i % pattern_size];
            }
        }
        code = rig.api.EnqueueSVMMap(rig.queue, CL_TRUE, CL_MAP_READ, block.get(), bytes, 0, nullptr, nullptr);
        if (code == CL_SUCCESS) {
            Kit::same_bytes(context, problems, "SVM contents after fills of every pattern size", model.data(), block.get(), bytes);
            rig.api.EnqueueSVMUnmap(rig.queue, block.get(), 0, nullptr, nullptr);
        }
        rig.api.Finish(rig.queue);
        std::uint8_t one_pattern[4] = {1, 2, 3, 4};
        Kit::expect(problems, "clEnqueueSVMMemFill with pattern size 3", rig.api.EnqueueSVMMemFill(rig.queue, block.get(), one_pattern, 3, 12, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, "clEnqueueSVMMemFill with a size that is not a multiple of the pattern", rig.api.EnqueueSVMMemFill(rig.queue, block.get(), one_pattern, 4, 6, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, "clEnqueueSVMMemFill with a pointer not aligned to the pattern", rig.api.EnqueueSVMMemFill(rig.queue, static_cast<std::uint8_t*>(block.get()) + 1, one_pattern, 4, 8, 0, nullptr, nullptr), {CL_INVALID_VALUE});

        const std::vector<std::uint8_t> source = Kit::bytes(1320, bytes);
        code = rig.api.EnqueueSVMMemcpy(rig.queue, CL_TRUE, block.get(), source.data(), bytes, 0, nullptr, nullptr);
        Kit::expect(problems, "blocking clEnqueueSVMMemcpy host to SVM", code, {CL_SUCCESS});
        SvmBlock other(rig, CL_MEM_READ_WRITE, bytes, 0);
        if (other) {
            cl_event event = nullptr;
            code = rig.api.EnqueueSVMMemcpy(rig.queue, CL_FALSE, other.get(), block.get(), bytes, 0, nullptr, &event);
            Kit::expect(problems, "non-blocking clEnqueueSVMMemcpy SVM to SVM", code, {CL_SUCCESS});
            ClOwned<cl_event> owned(rig, code == CL_SUCCESS ? event : nullptr);
            std::vector<std::uint8_t> back(bytes, 0);
            code = rig.api.EnqueueSVMMemcpy(rig.queue, CL_TRUE, back.data(), other.get(), bytes, event != nullptr ? 1 : 0, event != nullptr ? &event : nullptr, nullptr);
            Kit::expect(problems, "blocking clEnqueueSVMMemcpy SVM to host", code, {CL_SUCCESS});
            Kit::same_bytes(context, problems, "SVM memcpy chain", source.data(), back.data(), bytes);
            Kit::expect(problems, "clEnqueueSVMMemcpy of overlapping SVM regions", rig.api.EnqueueSVMMemcpy(rig.queue, CL_TRUE, static_cast<std::uint8_t*>(block.get()) + 64, block.get(), 256, 0, nullptr, nullptr), {CL_MEM_COPY_OVERLAP});
        }
        if ((caps & CL_DEVICE_SVM_FINE_GRAIN_SYSTEM) == 0) {
            std::uint32_t stack_word[4] = {};
            Kit::expect(problems, "clSetKernelArgSVMPointer with a pointer that is not SVM", step.set(ClSvm{stack_word}, mul, add), {CL_INVALID_ARG_VALUE});
        }
        void* empty = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE, 0, 0);
        if (empty != nullptr) {
            rig.api.SVMFree(rig.context, empty);
            problems.add("clSVMAlloc of zero bytes returned memory");
        }
        void* odd = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE, 4096, 3);
        if (odd != nullptr) {
            rig.api.SVMFree(rig.context, odd);
            problems.add("clSVMAlloc with alignment 3 returned memory");
        }
        void* both = rig.api.SVMAlloc(rig.context, CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY, 4096, 0);
        if (both != nullptr) {
            rig.api.SVMFree(rig.context, both);
            problems.add("clSVMAlloc with READ_ONLY | WRITE_ONLY returned memory");
        }
        void* beyond = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE, static_cast<std::size_t>(rig.max_allocation()) + 4096, 0);
        if (beyond != nullptr) {
            rig.api.SVMFree(rig.context, beyond);
            problems.add("clSVMAlloc above CL_DEVICE_MAX_MEM_ALLOC_SIZE returned memory");
        }
        rig.api.SVMFree(rig.context, nullptr);
        rig.api.Finish(rig.queue);
        return problems.outcome();
    });
}

Outcome SvmChecks::fine_grain_caps(Context& context) {
    return Kit::on_default(context, [&](ClRig& rig) {
        Problems problems;
        if (!rig.version.at_least(2, 0) || rig.api.SVMAlloc == nullptr) {
            return Context::skip("shared virtual memory needs OpenCL 2.0; the device reports " + rig.version.text());
        }
        const cl_device_svm_capabilities caps = capabilities(rig);
        context.measure("cl.svm_capabilities", caps_text(caps));
        const bool fine_buffer = (caps & CL_DEVICE_SVM_FINE_GRAIN_BUFFER) != 0;
        const bool fine_system = (caps & CL_DEVICE_SVM_FINE_GRAIN_SYSTEM) != 0;
        const bool atomics = (caps & CL_DEVICE_SVM_ATOMICS) != 0;
        if (atomics && !fine_buffer && !fine_system) {
            problems.add("CL_DEVICE_SVM_ATOMICS is set without FINE_GRAIN_BUFFER or FINE_GRAIN_SYSTEM (" + caps_text(caps) + ")");
        }
        if (!fine_buffer) {
            void* pointer = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER, 4096, 0);
            if (pointer != nullptr) {
                rig.api.SVMFree(rig.context, pointer);
                problems.add("clSVMAlloc(FINE_GRAIN_BUFFER) returned memory although the capabilities are " + caps_text(caps));
            }
        }
        if (!fine_buffer || !atomics) {
            void* pointer = rig.api.SVMAlloc(rig.context, CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS, 4096, 0);
            if (pointer != nullptr) {
                rig.api.SVMFree(rig.context, pointer);
                problems.add("clSVMAlloc(FINE_GRAIN_BUFFER | ATOMICS) returned memory although the capabilities are " + caps_text(caps));
            }
        }
        if (!fine_buffer && !fine_system) {
            return problems.empty() ? Outcome::ok() : problems.outcome();
        }
        ClProgram program;
        if (!Kit::compile(rig, program, kSvmKernels, "", problems, "SVM kernel")) {
            return problems.outcome();
        }
        ClKernel step = Kit::kernel(program, "lsw_step", problems);
        if (!step) {
            return problems.outcome();
        }
        const std::size_t words = 8192;
        const std::size_t bytes = words * 4;
        auto run_direct = [&](void* memory, const std::string& tag) {
            auto* w = static_cast<std::uint32_t*>(memory);
            const std::vector<std::uint32_t> data = Kit::words(1400, words);
            std::memcpy(w, data.data(), bytes);
            cl_int code = step.set(ClSvm{memory}, std::uint32_t(5), std::uint32_t(9));
            if (code == CL_SUCCESS) {
                code = step.run(words);
            }
            if (code == CL_SUCCESS) {
                code = rig.api.Finish(rig.queue);
            }
            if (code != CL_SUCCESS) {
                problems.add(tag + ": the kernel returned " + ClTools::error_name(code));
                return;
            }
            std::vector<std::uint32_t> expected(words);
            for (std::size_t i = 0; i < words; ++i) {
                expected[i] = data[i] * 5 + 9;
            }
            Kit::same_bytes(context, problems, tag + " read directly by the host after the kernel", expected.data(), memory, bytes);
        };
        if (fine_buffer) {
            SvmBlock block(rig, CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER, bytes, 0);
            if (!block) {
                problems.add("clSVMAlloc(FINE_GRAIN_BUFFER) returned NULL although the capability is reported");
            } else {
                run_direct(block.get(), "fine-grain buffer");
            }
        }
        if (fine_system) {
            AlignedBlock block(bytes, 4096);
            run_direct(block.get(), "fine-grain system memory");
        }
        if (fine_buffer && atomics) {
            std::string source =
                "__kernel void lsw_count(volatile __global atomic_int* counter, uint rounds) {\n"
                "    for (uint i = 0; i < rounds; ++i) {\n"
                "        atomic_fetch_add_explicit(counter, 1, memory_order_relaxed, memory_scope_all_svm_devices);\n"
                "    }\n"
                "}\n";
            ClProgram counting;
            std::string error;
            const char* standard = rig.version.at_least(3, 0) ? "-cl-std=CL3.0" : "-cl-std=CL2.0";
            if (!counting.build(rig, source, standard, error)) {
                if (rig.version.at_least(3, 0)) {
                    context.measure("cl.svm_atomics_kernel", "not built: " + error);
                } else {
                    problems.add("SVM atomics are reported but the C11 atomics kernel does not build: " + error);
                }
            } else {
                SvmBlock counter(rig, CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS, 64, 0);
                ClKernel count = Kit::kernel(counting, "lsw_count", problems);
                if (!counter || !count) {
                    problems.add("allocating the SVM atomic counter failed");
                } else {
                    auto* value = static_cast<volatile LONG*>(counter.get());
                    *value = 0;
                    const std::size_t items = 4096;
                    const cl_uint rounds = 64;
                    cl_event event = nullptr;
                    cl_int code = count.set(ClSvm{counter.get()}, rounds);
                    if (code == CL_SUCCESS) {
                        code = count.run(items, 0, &event);
                    }
                    if (code != CL_SUCCESS || event == nullptr) {
                        problems.add("the SVM atomics kernel returned " + ClTools::error_name(code));
                    } else {
                        ClOwned<cl_event> owned(rig, event);
                        rig.api.Flush(rig.queue);
                        long host_adds = 0;
                        while (Kit::event_status(rig, event) > CL_COMPLETE && host_adds < 50'000'000) {
                            for (int i = 0; i < 1000; ++i) {
                                InterlockedIncrement(value);
                            }
                            host_adds += 1000;
                        }
                        (void)Kit::wait(rig, event, problems, "SVM atomics event");
                        const long expected = static_cast<long>(items) * static_cast<long>(rounds) + host_adds;
                        if (*value != expected) {
                            problems.add("device and host atomic increments sum to " + std::to_string(*value) + ", expected " + std::to_string(expected) + " (" + std::to_string(host_adds) + " host increments)");
                        }
                        context.measure("cl.svm_atomics_host_increments", host_adds);
                    }
                }
            }
        }
        return problems.outcome();
    });
}

}  // namespace lsw::suites::sanity::cl
