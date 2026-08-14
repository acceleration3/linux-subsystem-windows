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

#include "lsw/linkproto/commands.hpp"

#include <QtConcurrent>

#include <vector>

#include <nlohmann/json.hpp>

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
            // EVERY key goes as a key, including the ones that produce
            // characters.
            //
            // The agent injects these with SendInput and a real virtual
            // key, so Windows raises WM_KEYDOWN and the application's
            // own TranslateMessage derives the character from it — one
            // path serves both. The text path uses KEYEVENTF_UNICODE
            // instead, which delivers a character and no key at all: a
            // text box is happy with that and a game reads nothing,
            // which is exactly how Z and X reached Touhou 6's window
            // and not its input handler.
            const unsigned vk = ScancodeMap::virtual_key_for(ev.scancode);
            if (vk == 0) {
                break;  // unmapped: silence beats a plausible wrong key
            }
            if (ev.kind == Kind::KeyDown) {
                input.key_down(vk, ev.mods);
                std::lock_guard lk(held_mu_);
                held_keys_.insert(vk);
            } else {
                input.key_up(vk, ev.mods);
                std::lock_guard lk(held_mu_);
                held_keys_.erase(vk);
            }
            break;
        }
        case Kind::FocusLost: {
            // Nothing will report the release of a key that was down
            // when focus went away, so let go of everything now. In the
            // guest that key is held by SendInput at the OS level: it
            // would stay down with no window left to lift it, and the
            // application would read it forever.
            std::vector<unsigned> stuck;
            {
                std::lock_guard lk(held_mu_);
                stuck.assign(held_keys_.begin(), held_keys_.end());
                held_keys_.clear();
            }
            for (const unsigned vk : stuck) {
                input.key_up(vk, 0);
            }
            break;
        }
        case Kind::Text:
            // Only what the key path cannot express. An ASCII character
            // has already arrived as a key press, and sending it again
            // here would type everything twice; a character the host
            // layout produced some other way (a dead key, AltGr, an
            // input method) has no virtual key of ours and this is the
            // only way it gets there.
            if (ev.code_point >= 0x80) {
                input.text(ev.code_point, ev.mods);
            }
            break;
        }
    });

    // Closing the window means closing the application. The window
    // belongs to the replay, but what is drawing into it is a process
    // in the VM — so the request goes there, and the window goes away
    // when that process does. Same contract as closing a mirror.
    service_->surfaces().set_close_sink([this](std::uint64_t /*surface_id*/) {
        const std::uint64_t hwnd = input_hwnd_.load();
        if (hwnd == 0) {
            return;
        }
        LSW_INFO("vulkan capture: window closed — asking guest window {:#x} to close", hwnd);
        (void)QtConcurrent::run([this, hwnd] {
            (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kCloseWindow},
                                           {"params", {{"hwnd", hwnd}}}});
        });
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
