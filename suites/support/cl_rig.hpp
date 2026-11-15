#pragma once

#include "support/av_adapters.hpp"
#include "support/cl_api.hpp"
#include "support/cl_tools.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace lsw::suites {
struct ClDeviceRef {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    std::string name;
    AvAdapter adapter;
    bool luid_matched = false;

    [[nodiscard]] std::string label() const {
        return name + (adapter.has_outputs() ? " (display adapter)" : " (render-only adapter)");
    }
};

struct ClDeviceList {
    std::vector<ClDeviceRef> refs;
    std::size_t adapters = 0;
};

struct ClRigOptions {
    int device = -1;
    bool profiling = false;
    bool context = true;
};

class ClRig {
public:
    ClRig() = default;
    ClRig(const ClRig&) = delete;
    ClRig& operator=(const ClRig&) = delete;
    ~ClRig();

    [[nodiscard]] static bool list_devices(ClApi& api, ClDeviceList& out, std::string& error);

    [[nodiscard]] bool open(const ClRigOptions& options, std::string& error);

    [[nodiscard]] bool open_ref(const ClDeviceRef& ref, const ClRigOptions& options, std::string& error);

    [[nodiscard]] cl_command_queue create_queue(cl_command_queue_properties properties, cl_int& code,
                                                 cl_device_id for_device = nullptr);

    void forget_queue(cl_command_queue queue);

    void drain();

    template <typename T>
    bool info(cl_device_info param, T& out, cl_int* code = nullptr) const {
        const cl_int result = api.GetDeviceInfo(device, param, sizeof(T), &out, nullptr);
        if (code != nullptr) {
            *code = result;
        }
        return result == CL_SUCCESS;
    }

    [[nodiscard]] cl_uint uint_info(cl_device_info param) const;

    [[nodiscard]] cl_ulong ulong_info(cl_device_info param) const;

    [[nodiscard]] std::size_t size_info(cl_device_info param) const;

    [[nodiscard]] std::string text_info(cl_device_info param) const;

    [[nodiscard]] bool has_extension(const char* name) const { return ClTools::has_word(extensions, name); }

    [[nodiscard]] std::uint64_t max_allocation() const { return ulong_info(CL_DEVICE_MAX_MEM_ALLOC_SIZE); }

    [[nodiscard]] std::uint64_t global_memory() const { return ulong_info(CL_DEVICE_GLOBAL_MEM_SIZE); }

    [[nodiscard]] std::size_t base_alignment() const { return uint_info(CL_DEVICE_MEM_BASE_ADDR_ALIGN) / 8; }

    [[nodiscard]] bool images() const { return uint_info(CL_DEVICE_IMAGE_SUPPORT) != 0; }

    [[nodiscard]] bool profile_full() const { return profile == "FULL_PROFILE"; }

    ClApi api;
    ClDeviceRef ref;
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    ClVersion version;
    std::string extensions;
    std::string profile;

private:
    std::mutex mutex_;
    std::vector<cl_command_queue> queues_;
};
}  // namespace lsw::suites
