#include "sanity/sync/sync_device.hpp"

namespace lsw::suites::sanity::sync {

std::unique_ptr<SyncDevice> SyncDevice::make(const std::string& api) {
    if (api == "d3d11") {
        return make_sync_d3d11();
    }
    if (api == "d3d12") {
        return make_sync_d3d12();
    }
    if (api == "vulkan") {
        return make_sync_vulkan();
    }
    if (api == "opencl") {
        return make_sync_opencl();
    }
    return nullptr;
}

}  // namespace lsw::suites::sanity::sync
