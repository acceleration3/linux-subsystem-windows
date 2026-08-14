#include "lsw/host/apps/vulkan_capture_service.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/vm/shmem_region.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/protocol/listener.hpp"
#include "lsw/render/render_service.hpp"

#include "lsw/host/apps/scancode_map.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/guest_input_sender.hpp"
#include "lsw/render/window_input.hpp"

namespace lsw::host::apps {

VulkanCaptureService::VulkanCaptureService(vm::Lifecycle& lifecycle, link::AgentRpcClient& rpc,
                                           QObject* parent)
    : QObject(parent), lifecycle_(lifecycle), rpc_(rpc) {}

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
    // Everything that happens in the replay's window goes to the guest
    // window that produced the picture in it. Runs on the replay's
    // window thread and must not block, so every send is the RPC
    // client's fire-and-forget path.
    service_->surfaces().set_input_sink([this](const ::lsw::render::WindowInputEvent& ev) {
        const std::uint64_t hwnd = input_hwnd_.load();
        if (hwnd == 0) {
            return;  // no guest window bound yet; nothing to send to
        }
        manager::GuestInputSender input{rpc_, hwnd};
        const QPoint at{ev.x, ev.y};
        using Kind = ::lsw::render::WindowInputEvent::Kind;
        switch (ev.kind) {
        case Kind::MouseMove:
            input.move(at, ev.mods);
            break;
        case Kind::MouseDown:
            input.button_down(at, ev.button, ev.mods);
            break;
        case Kind::MouseUp:
            input.button_up(at, ev.button, ev.mods);
            break;
        case Kind::Wheel:
            input.wheel(at, ev.wheel_delta, ev.horizontal, ev.mods);
            break;
        case Kind::KeyDown:
        case Kind::KeyUp: {
            // A key that produces a character is sent as text only: the
            // guest's own TranslateMessage would otherwise synthesise a
            // second character from the key and every letter would
            // arrive twice.
            if (ScancodeMap::produces_text(ev.scancode)) {
                break;
            }
            const unsigned vk = ScancodeMap::virtual_key_for(ev.scancode);
            if (vk == 0) {
                break;  // unmapped: silence beats a plausible wrong key
            }
            if (ev.kind == Kind::KeyDown) {
                input.key_down(vk, ev.mods);
            } else {
                input.key_up(vk, ev.mods);
            }
            break;
        }
        case Kind::Text:
            input.text(ev.code_point, ev.mods);
            break;
        }
    });

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

void VulkanCaptureService::forward_input_to(std::uint64_t hwnd) {
    input_hwnd_.store(hwnd);
    LSW_INFO("vulkan capture: input goes to guest window {:#x}", hwnd);
}

void VulkanCaptureService::stop_forwarding() {
    input_hwnd_.store(0);
}

}  // namespace lsw::host::apps
