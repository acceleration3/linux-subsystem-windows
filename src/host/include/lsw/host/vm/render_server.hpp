#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QUuid>

namespace lsw::host::vm {

// The ``rvk-render-server`` process serving one VM's guest drivers.
//
// An application in the guest that renders through the OpenCL, OpenGL
// or Vulkan driver puts its calls on a ring in shared memory; this is
// what is at the other end, replaying them against the host's real
// driver on the host's real GPU. Without it every such call blocks and
// the application looks like it has hung.
//
// A separate process, not a thread in the manager. It draws with the
// host's GPU drivers — three of them — and a fault in any of those
// takes down whatever address space it is in; the manager is the wrong
// address space to lose. Its own process also means it can be killed
// and restarted without the manager, which is what debugging a driver
// consists of.
//
// STARTED BEFORE THE DOMAIN, ALWAYS. The server creates the region as a
// file in ``/dev/shm``, and QEMU opens that file by path when the guest
// boots and holds the mapping against the inode for the life of the
// domain. A server started afterwards makes a different file, and the
// guest then maps pages nobody writes to: every call fails with
// VK_ERROR_INCOMPATIBLE_DRIVER or CL_PLATFORM_NOT_FOUND_KHR and nothing
// says why. Restarting it under a running guest has the same shape,
// which is why ``--vm`` also asks the server to reuse its backing file.
class RenderServer : public QObject {
    Q_OBJECT
public:
    explicit RenderServer(QObject* parent = nullptr);
    ~RenderServer() override;

    // The name of the shared-memory object the guest's ivshmem device
    // is backed by, for ``uuid``. This is what goes in the domain XML's
    // ``<shmem name='...'/>``, and it is ``region_prefix`` plus the
    // session index the server appends.
    [[nodiscard]] static QString region_name(const QUuid& uuid);

    // How large that region is, in MiB. Matches what the command line
    // below asks for once ``--vm`` has rounded it up to a power of two,
    // which it must be: the region is a PCI BAR, and a BAR's size is
    // decoded by address bits, so QEMU does not round — it asserts, and
    // the VM dies before the guest exists.
    static constexpr int kRegionMib = 1024;

    // Start the server for ``uuid`` if it is not already running, and
    // wait for the region file to appear. Returns false when the server
    // could not be started or never created its region; the caller
    // should start the domain anyway, because a guest whose
    // applications report no driver is easier to diagnose than a guest
    // that never boots.
    [[nodiscard]] bool ensure_running(const QUuid& uuid);

    // Stop the server. The region file stays: it belongs to the domain
    // that mapped it, not to this process.
    void stop();

    [[nodiscard]] bool running() const;

private:
    // Absolute path to the executable, or empty when there is none.
    // ``$LSW_RENDER_SERVER`` wins so a developer can point at a
    // specific build; otherwise the build tree beside the manager.
    [[nodiscard]] static QString executable_path();

    QProcess process_;
    QUuid serving_;
};

}  // namespace lsw::host::vm
