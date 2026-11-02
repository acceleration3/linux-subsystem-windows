#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::platform {
struct HostApi {
    enum class Kind { Vulkan, OpenGl, OpenCl, Direct3d11, Direct3d12 };

    Kind kind = Kind::Vulkan;
    QString name;
    QString runs_on;
    bool served = false;
    bool required = false;
};

class HostGraphicsProbe {
public:
    HostGraphicsProbe() = delete;

    [[nodiscard]] static QVector<HostApi> probe(const QVector<HostGpu>& gpus,
                                                const QString& d3d_dir);

    [[nodiscard]] static bool missing_required(const QVector<HostApi>& apis);

    [[nodiscard]] static QString driver_label(const HostGpu& gpu);

    [[nodiscard]] static QString driver_name(const HostGpu& gpu);

private:
    [[nodiscard]] static HostApi vulkan(const QVector<HostGpu>& gpus);
    [[nodiscard]] static HostApi opengl();
    [[nodiscard]] static HostApi opencl();
    [[nodiscard]] static HostApi direct3d(HostApi::Kind kind, const QString& d3d_dir);
};

}
