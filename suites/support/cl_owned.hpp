#pragma once

#include "support/cl_rig.hpp"

#include <utility>

namespace lsw::suites {
template <typename Handle>
struct ClTraits;

template <>
struct ClTraits<cl_mem> {
    static void release(ClRig& rig, cl_mem handle) {
        rig.drain();
        rig.api.ReleaseMemObject(handle);
    }
};

template <>
struct ClTraits<cl_program> {
    static void release(ClRig& rig, cl_program handle) {
        rig.drain();
        rig.api.ReleaseProgram(handle);
    }
};

template <>
struct ClTraits<cl_kernel> {
    static void release(ClRig& rig, cl_kernel handle) {
        rig.drain();
        rig.api.ReleaseKernel(handle);
    }
};

template <>
struct ClTraits<cl_sampler> {
    static void release(ClRig& rig, cl_sampler handle) {
        rig.drain();
        rig.api.ReleaseSampler(handle);
    }
};

template <>
struct ClTraits<cl_event> {
    static void release(ClRig& rig, cl_event handle) { rig.api.ReleaseEvent(handle); }
};

template <>
struct ClTraits<cl_command_queue> {
    static void release(ClRig& rig, cl_command_queue handle) {
        rig.drain();
        rig.forget_queue(handle);
        rig.api.ReleaseCommandQueue(handle);
    }
};

template <>
struct ClTraits<cl_context> {
    static void release(ClRig& rig, cl_context handle) {
        rig.drain();
        rig.api.ReleaseContext(handle);
    }
};

template <>
struct ClTraits<cl_device_id> {
    static void release(ClRig& rig, cl_device_id handle) { rig.api.ReleaseDevice(handle); }
};

template <typename Handle>
class ClOwned {
public:
    ClOwned() = default;
    ClOwned(ClRig& rig, Handle handle) : rig_(&rig), handle_(handle) {}
    ClOwned(const ClOwned&) = delete;
    ClOwned& operator=(const ClOwned&) = delete;
    ClOwned(ClOwned&& other) noexcept : rig_(other.rig_), handle_(std::exchange(other.handle_, nullptr)) {}

    ClOwned& operator=(ClOwned&& other) noexcept {
        if (this != &other) {
            reset();
            rig_ = other.rig_;
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~ClOwned() { reset(); }

    void reset() {
        if (handle_ != nullptr && rig_ != nullptr) {
            ClTraits<Handle>::release(*rig_, handle_);
        }
        handle_ = nullptr;
    }

    void adopt(ClRig& rig, Handle handle) {
        reset();
        rig_ = &rig;
        handle_ = handle;
    }

    [[nodiscard]] Handle get() const { return handle_; }

    [[nodiscard]] Handle detach() { return std::exchange(handle_, nullptr); }

    [[nodiscard]] explicit operator bool() const { return handle_ != nullptr; }

private:
    ClRig* rig_ = nullptr;
    Handle handle_ = nullptr;
};
}  // namespace lsw::suites
