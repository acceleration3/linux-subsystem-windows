#include "sanity/err/spin_device.hpp"

#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"

namespace lsw::suites::sanity::err {

namespace {

constexpr const char* kSpin =
    "__kernel void spin(__global uint* o, uint inner, uint outer) {\n"
    "  uint id = (uint)get_global_id(0);\n"
    "  uint x = id * 2654435761u + 12345u;\n"
    "  for (uint a = 0; a < outer; ++a) {\n"
    "    for (uint k = 0; k < inner; ++k) { x = x * 1664525u + 1013904223u; }\n"
    "  }\n"
    "  o[id] = x;\n"
    "}\n";

constexpr const char* kForever =
    "__kernel void spin(volatile __global uint* o, uint inner, uint outer) {\n"
    "  uint c = 0;\n"
    "  while (atomic_or(o, 0u) == 0u) { ++c; }\n"
    "  o[1] = c + inner + outer;\n"
    "}\n";

class SpinOpenCl : public SpinDevice {
public:
    bool open(bool forever, std::string& error) override {
        forever_ = forever;
        ClRigOptions options;
        if (!rig_.open(options, error)) {
            return false;
        }
        if (!program_.build(rig_, forever ? kForever : kSpin, "", error)) {
            return false;
        }
        kernel_ = program_.kernel("spin", error);
        if (!kernel_) {
            return false;
        }
        cl_int code = CL_SUCCESS;
        const std::vector<std::uint32_t> zeros(kThreads, 0);
        buffer_ = ClOwned<cl_mem>(rig_, rig_.api.CreateBuffer(rig_.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, kThreads * 4, const_cast<std::uint32_t*>(zeros.data()), &code));
        if (code != CL_SUCCESS || !buffer_) {
            error = "clCreateBuffer failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    bool submit(std::uint32_t outer, std::string& error) override {
        const std::uint32_t inner = kInner;
        cl_int code = kernel_.set(buffer_, inner, outer);
        if (code == CL_SUCCESS) {
            if (event_ != nullptr) {
                rig_.api.ReleaseEvent(event_);
                event_ = nullptr;
            }
            code = kernel_.run(forever_ ? 1 : kThreads, 0, &event_);
        }
        if (code == CL_SUCCESS) {
            code = rig_.api.Flush(rig_.queue);
        }
        if (code != CL_SUCCESS) {
            error = "enqueueing the kernel failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    SpinState poll(int timeout_ms, std::string& detail) override {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            cl_int status = 0;
            const cl_int code = rig_.api.GetEventInfo(event_, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, nullptr);
            if (code != CL_SUCCESS) {
                detail = "clGetEventInfo returned " + ClTools::error_name(code);
                return classify(code);
            }
            if (status == CL_COMPLETE) {
                return SpinState::Done;
            }
            if (status < 0) {
                detail = "the kernel ended with " + ClTools::error_name(status);
                return classify(status);
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SpinState::Pending;
            }
            ::Sleep(5);
        }
    }

    bool read(std::vector<std::uint32_t>& out, std::string& error) override {
        out.resize(kThreads);
        const cl_int code = rig_.api.EnqueueReadBuffer(rig_.queue, buffer_.get(), CL_TRUE, 0, kThreads * 4, out.data(), 0, nullptr, nullptr);
        if (code != CL_SUCCESS) {
            error = "clEnqueueReadBuffer failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

private:
    static SpinState classify(cl_int code) {
        if (code == CL_OUT_OF_RESOURCES || code == CL_DEVICE_NOT_AVAILABLE || code == CL_OUT_OF_HOST_MEMORY) {
            return SpinState::Lost;
        }
        return SpinState::Error;
    }

    bool forever_ = false;
    ClRig rig_;
    ClProgram program_;
    ClKernel kernel_;
    ClOwned<cl_mem> buffer_;
    cl_event event_ = nullptr;
};

}  // namespace

std::unique_ptr<SpinDevice> make_spin_opencl() {
    return std::make_unique<SpinOpenCl>();
}

}  // namespace lsw::suites::sanity::err
