#pragma once

#include "support/cl_owned.hpp"

namespace lsw::suites {
struct ClLocal {
    std::size_t bytes = 0;
};

struct ClSvm {
    void* pointer = nullptr;
};

class ClKernel {
public:
    ClKernel() = default;
    ClKernel(ClRig& rig, cl_kernel kernel) : rig_(&rig), kernel_(rig, kernel) {}

    template <typename... Args>
    [[nodiscard]] cl_int set(const Args&... args) {
        cl_uint index = 0;
        cl_int code = CL_SUCCESS;
        ((code == CL_SUCCESS ? (code = one(index, args)) : code, ++index), ...);
        return code;
    }

    [[nodiscard]] cl_int enqueue(cl_command_queue queue, cl_uint dims, const std::size_t* offset, const std::size_t* global,
                                 const std::size_t* local, cl_uint waits = 0, const cl_event* wait_list = nullptr,
                                 cl_event* event = nullptr) {
        return rig_->api.EnqueueNDRangeKernel(queue, kernel_.get(), dims, offset, global, local, waits, wait_list, event);
    }

    [[nodiscard]] cl_int run(std::size_t global, std::size_t local = 0, cl_event* event = nullptr) {
        return enqueue(rig_->queue, 1, nullptr, &global, local != 0 ? &local : nullptr, 0, nullptr, event);
    }

    [[nodiscard]] cl_int run_on(cl_command_queue queue, std::size_t global, std::size_t local = 0, cl_uint waits = 0,
                                const cl_event* wait_list = nullptr, cl_event* event = nullptr) {
        return enqueue(queue, 1, nullptr, &global, local != 0 ? &local : nullptr, waits, wait_list, event);
    }

    [[nodiscard]] cl_int work_group_info(std::size_t& value) const {
        value = 0;
        return rig_->api.GetKernelWorkGroupInfo(kernel_.get(), rig_->device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(value), &value, nullptr);
    }

    [[nodiscard]] std::size_t work_group_size() const {
        std::size_t value = 0;
        static_cast<void>(work_group_info(value));
        return value;
    }

    [[nodiscard]] cl_kernel get() const { return kernel_.get(); }

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(kernel_); }

private:
    [[nodiscard]] cl_int one(cl_uint index, const ClOwned<cl_mem>& mem) {
        const cl_mem handle = mem.get();
        return rig_->api.SetKernelArg(kernel_.get(), index, sizeof(handle), &handle);
    }

    [[nodiscard]] cl_int one(cl_uint index, const ClLocal& local) {
        return rig_->api.SetKernelArg(kernel_.get(), index, local.bytes, nullptr);
    }

    [[nodiscard]] cl_int one(cl_uint index, const ClSvm& svm) {
        return rig_->api.SetKernelArgSVMPointer(kernel_.get(), index, svm.pointer);
    }

    template <typename T>
    [[nodiscard]] cl_int one(cl_uint index, const T& value) {
        return rig_->api.SetKernelArg(kernel_.get(), index, sizeof(T), &value);
    }

    ClRig* rig_ = nullptr;
    ClOwned<cl_kernel> kernel_;
};
}  // namespace lsw::suites
