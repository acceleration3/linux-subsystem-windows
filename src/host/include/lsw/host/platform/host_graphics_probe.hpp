#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::platform {

// One graphics API as the host can serve it to the guest's adapters.
struct HostApi {
    enum class Kind { Vulkan, OpenGl, OpenCl, Direct3d11, Direct3d12 };

    Kind kind = Kind::Vulkan;
    QString name;          // "Direct3D 11"
    // What the probe found it runs on: "RADV, the host Vulkan driver",
    // "EGL, radeonsi", "DXVK 2.7.1, native" (a version only where the
    // file name carries one); why not, when not served. What Windows
    // gets on top of it is the guest's to say, not the host's.
    QString runs_on;
    bool served = false;
    // Windows cannot run without it: DWM composes with Direct3D 11, and
    // without DXVK and vkd3d-proton on the host it crash-loops with
    // 0x889800B2.
    bool required = false;
};

// What the host can serve the guest, checked the way accel-virt's
// executor decides: a Vulkan driver, EGL with desktop OpenGL, an OpenCL
// ICD, and DXVK's and vkd3d-proton's native libraries in AV_D3D_PATH.
//
// Cheap enough to repeat on demand — the EGL check loads the GL driver
// and does nothing with it — but it does load a driver, so callers run
// it off the GUI thread.
class HostGraphicsProbe {
public:
    HostGraphicsProbe() = delete;

    // The five APIs, in the order the Graphics page lists them. ``gpus``
    // are the ones whose Vulkan drivers to name; ``d3d_dir`` is where the
    // Direct3D libraries are looked for (empty: nowhere, so missing).
    [[nodiscard]] static QVector<HostApi> probe(const QVector<HostGpu>& gpus,
                                                const QString& d3d_dir);

    // True when an API that Windows needs is not served.
    [[nodiscard]] static bool missing_required(const QVector<HostApi>& apis);

    // A GPU's driver as people name it: "RADV, Mesa 25.2.3",
    // "NVIDIA 580.82", "llvmpipe".
    [[nodiscard]] static QString driver_label(const HostGpu& gpu);

    // The short form of ``driver_label``: "RADV".
    [[nodiscard]] static QString driver_name(const HostGpu& gpu);

private:
    [[nodiscard]] static HostApi vulkan(const QVector<HostGpu>& gpus);
    [[nodiscard]] static HostApi opengl();
    [[nodiscard]] static HostApi opencl();
    [[nodiscard]] static HostApi direct3d(HostApi::Kind kind, const QString& d3d_dir);
};

}  // namespace lsw::host::platform
