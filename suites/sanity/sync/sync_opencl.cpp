#include "sanity/sync/sync_device.hpp"

#include "support/cl_kernel.hpp"
#include "support/cl_program.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"

#include <windows.h>

namespace lsw::suites::sanity::sync {

namespace {

constexpr const char* kStep =
    "__kernel void step(__global uint* v, uint op, uint value, uint slot) {\n"
    "  if (op == 0u) { v[slot] = value; } else { v[slot] += value; }\n"
    "}\n";

class SyncOpenCl : public SyncDevice {
public:
    ~SyncOpenCl() override { drop_event(); }

    bool open(std::string& error) override {
        ClRigOptions options;
        if (!rig_.open(options, error) || !program_.build(rig_, kStep, "", error)) {
            return false;
        }
        kernel_ = program_.kernel("step", error);
        if (!kernel_) {
            return false;
        }
        cl_int code = CL_SUCCESS;
        const std::vector<std::uint32_t> zeros(kSlots, 0);
        buffer_ = ClOwned<cl_mem>(rig_, rig_.api.CreateBuffer(rig_.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, kSlots * 4, const_cast<std::uint32_t*>(zeros.data()), &code));
        if (code != CL_SUCCESS || !buffer_) {
            error = "clCreateBuffer failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    bool set(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(0, slot, value, error); }

    bool add(std::uint32_t slot, std::uint32_t value, std::string& error) override { return step(1, slot, value, error); }

    bool signal(std::string& error) override {
        drop_event();
        cl_int code = rig_.api.EnqueueMarkerWithWaitList(rig_.queue, 0, nullptr, &event_);
        if (code == CL_SUCCESS) {
            code = rig_.api.Flush(rig_.queue);
        }
        if (code != CL_SUCCESS) {
            error = "enqueueing the marker failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    SyncState wait(int timeout_ms, std::string& detail) override {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
        for (;;) {
            cl_int status = 0;
            const cl_int code = rig_.api.GetEventInfo(event_, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, nullptr);
            if (code != CL_SUCCESS) {
                detail = "clGetEventInfo returned " + ClTools::error_name(code);
                return classify(code);
            }
            if (status == CL_COMPLETE) {
                return SyncState::Done;
            }
            if (status < 0) {
                detail = "the queue ended with " + ClTools::error_name(status);
                return classify(status);
            }
            if (static_cast<LONGLONG>(GetTickCount64() - end) >= 0) {
                return SyncState::Timeout;
            }
            ::Sleep(1);
        }
    }

    bool finish(std::string& error) override {
        const cl_int code = rig_.api.Finish(rig_.queue);
        if (code != CL_SUCCESS) {
            error = "clFinish returned " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    bool read(std::uint32_t slot, std::uint32_t& out, std::string& error) override {
        const cl_int code = rig_.api.EnqueueReadBuffer(rig_.queue, buffer_.get(), CL_TRUE, slot * 4, 4, &out, 0, nullptr, nullptr);
        if (code != CL_SUCCESS) {
            error = "clEnqueueReadBuffer failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

private:
    static SyncState classify(cl_int code) {
        return code == CL_OUT_OF_RESOURCES || code == CL_DEVICE_NOT_AVAILABLE || code == CL_OUT_OF_HOST_MEMORY ? SyncState::Lost : SyncState::Error;
    }

    void drop_event() {
        if (event_ != nullptr) {
            rig_.api.ReleaseEvent(event_);
            event_ = nullptr;
        }
    }

    bool step(std::uint32_t op, std::uint32_t slot, std::uint32_t value, std::string& error) {
        cl_int code = kernel_.set(buffer_, op, value, slot);
        if (code == CL_SUCCESS) {
            code = kernel_.run(1);
        }
        if (code != CL_SUCCESS) {
            error = "enqueueing the kernel failed: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }

    ClRig rig_;
    ClProgram program_;
    ClKernel kernel_;
    ClOwned<cl_mem> buffer_;
    cl_event event_ = nullptr;
};

}  // namespace

std::unique_ptr<SyncDevice> make_sync_opencl() {
    return std::make_unique<SyncOpenCl>();
}

}  // namespace lsw::suites::sanity::sync
