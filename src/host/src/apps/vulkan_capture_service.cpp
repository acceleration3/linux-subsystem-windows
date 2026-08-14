#include "lsw/host/apps/vulkan_capture_service.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/vm/shmem_region.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/protocol/listener.hpp"
#include "lsw/render/render_service.hpp"

namespace lsw::host::apps {

VulkanCaptureService::VulkanCaptureService(vm::Lifecycle& lifecycle, QObject* parent)
    : QObject(parent), lifecycle_(lifecycle) {}

VulkanCaptureService::~VulkanCaptureService() {
    stop();
}

void VulkanCaptureService::stop() {
    if (service_ != nullptr) {
        service_->request_stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
    service_.reset();
    listener_.reset();
}

bool VulkanCaptureService::ensure_running(const QUuid& vm_uuid) {
    if (running_.load() && attached_uuid_ == vm_uuid) {
        return true;
    }
    vm::ShmemRegion* region = lifecycle_.shmem_region(vm_uuid);
    if (region == nullptr || region->shm_name().isEmpty()) {
        // The region belongs to the manager process that created it and
        // is unlinked when that process exits, so a manager restarted
        // under a running VM has none — and cannot make one, because
        // the fd went to QEMU at domain start. Restarting the VM is the
        // way back.
        LSW_WARN("vulkan capture: this manager has no shared region for the VM. If it was "
                 "already running before the manager started, restart the VM to reattach.");
        return false;
    }
    if (running_.load()) {
        stop();  // pointed at a different VM
    }

    // ``shm-attach://`` rather than ``shm://``: the region is ours
    // already — created here and handed to QEMU — so the replay maps
    // what exists instead of making a second one.
    const std::string endpoint =
        "shm-attach:///" + region->shm_name().toStdString();
    std::string err;
    listener_ = ::lsw::protocol::open_listener(endpoint, &err);
    if (listener_ == nullptr) {
        LSW_WARN("vulkan capture: cannot attach to {}: {}", endpoint, err);
        return false;
    }

    service_ = std::make_unique<::lsw::render::RenderService>();
    // No SDL in this process: the manager has its own event loop and
    // its own display connection, and every surface the replay makes
    // here is created on a window the manager already owns.
    service_->surfaces().set_adopt_only(true);

    running_.store(true);
    attached_uuid_ = vm_uuid;
    thread_ = std::thread([this] {
        LSW_INFO("vulkan capture: replaying in-process");
        service_->serve(*listener_);
        running_.store(false);
        LSW_INFO("vulkan capture: replay stopped");
    });
    return true;
}

void VulkanCaptureService::set_adoption_target(std::uint64_t native_window, std::uint32_t width,
                                               std::uint32_t height) {
    if (service_ == nullptr) {
        return;
    }
    service_->surfaces().set_adoption_target(native_window, width, height);
}

}  // namespace lsw::host::apps
