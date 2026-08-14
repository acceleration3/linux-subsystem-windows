#pragma once

#include <QObject>
#include <QUuid>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace lsw::protocol { class Listener; }
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
    explicit VulkanCaptureService(vm::Lifecycle& lifecycle, QObject* parent = nullptr);
    ~VulkanCaptureService() override;

    // Make sure the replay is serving ``vm_uuid``'s region. Returns
    // false when there is no region — which is what a manager restarted
    // underneath a running VM has, since the region belongs to the
    // process that created it. Logged either way; the caller should
    // still go ahead with the launch, because an application that
    // starts and reports no driver is easier to diagnose than one that
    // never starts.
    [[nodiscard]] bool ensure_running(const QUuid& vm_uuid);

    // The window the next surface the guest asks for should be created
    // on, instead of one of the replay's own. ``native_window`` is an
    // X11 window id — a mirror's ``winId()``.
    //
    // Set before the application is launched: the guest asks for its
    // surface within a moment of starting, and a target that arrives
    // afterwards is too late for that surface.
    void set_adoption_target(std::uint64_t native_window, std::uint32_t width,
                             std::uint32_t height);

    [[nodiscard]] bool running() const noexcept { return running_.load(); }

private:
    void stop();

    vm::Lifecycle& lifecycle_;
    std::unique_ptr<::lsw::render::RenderService> service_;
    std::unique_ptr<::lsw::protocol::Listener> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    QUuid attached_uuid_;
};

}  // namespace lsw::host::apps
