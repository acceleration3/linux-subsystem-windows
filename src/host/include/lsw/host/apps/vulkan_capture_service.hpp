#pragma once

#include <QObject>
#include <QUuid>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace lsw::protocol { class Listener; }
namespace lsw::host::link { class AgentRpcClient; }
namespace lsw::render { class RenderService; }
namespace lsw::host::vm { class Lifecycle; }

namespace lsw::host::apps {

// Replays a Vulkan-capture application's command stream, in this
// process.
//
// An application configured for Vulkan capture sends no pixels: it
// sends the Vulkan calls that would have produced them, into the
// region's command-stream channel. Something has to be at the other end
// replaying them against the real GPU, or the application blocks on its
// first call and looks like a hang.
//
// IN THIS PROCESS, and that is not an optimisation. A replay can only
// present into a window its own process owns — presenting into another
// process's window succeeds at every Vulkan call and paints nothing.
// The window we want it to present into is a mirror's, and mirrors
// belong to the manager; so the replay has to live here too. Running it
// as ``lsw-render-server`` instead means a second window, showing the
// picture, next to the mirror that has the input.
//
// Started lazily, on the first Vulkan-capture launch: a VM that never
// runs one should not pay for a Vulkan instance and a thread. Left
// running afterwards, because it is idle when nothing is connected and
// the next launch then does not race its startup.
class VulkanCaptureService : public QObject {
    Q_OBJECT
public:
    VulkanCaptureService(vm::Lifecycle& lifecycle, link::AgentRpcClient& rpc,
                         QObject* parent = nullptr);
    ~VulkanCaptureService() override;

    // Make sure the replay is serving ``vm_uuid``'s region. Returns
    // false when there is no region — which is what a manager restarted
    // underneath a running VM has, since the region belongs to the
    // process that created it. Logged either way; the caller should
    // still go ahead with the launch, because an application that
    // starts and reports no driver is easier to diagnose than one that
    // never starts.
    [[nodiscard]] bool ensure_running(const QUuid& vm_uuid);

    // Send input that happens in the replay's window to ``hwnd``.
    //
    // The replay's window is the one with the application's picture in
    // it, so it is the one the user clicks and types into — but only
    // the manager can reach the guest, over the agent's input RPC. So
    // the window hands its events here and they go on to the guest
    // window that produced the picture.
    //
    // This is why the replay is hosted in the manager rather than run
    // as ``lsw-render-server``: not to share a window with Qt, but to
    // share the agent connection. It works the same under X11 and
    // Wayland, because SDL owns the window either way.
    void forward_input_to(std::uint64_t hwnd);

    // Stop forwarding — the guest window is gone.
    void stop_forwarding();

    [[nodiscard]] bool running() const noexcept { return running_.load(); }

private:
    void stop();

    vm::Lifecycle& lifecycle_;
    link::AgentRpcClient& rpc_;
    // The guest window input is going to, or 0. Read from the replay's
    // window thread, written from the GUI thread.
    std::atomic<std::uint64_t> input_hwnd_{0};
    // Virtual keys currently down, so they can be released if the
    // window stops hearing about them — losing focus produces no
    // key-up, and a key injected with SendInput and never lifted stays
    // down in the guest.
    std::mutex held_mu_;
    std::set<unsigned> held_keys_;
    std::unique_ptr<::lsw::render::RenderService> service_;
    std::unique_ptr<::lsw::protocol::Listener> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    QUuid attached_uuid_;
};

}  // namespace lsw::host::apps
