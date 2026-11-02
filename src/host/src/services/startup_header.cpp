#include "lsw/host/services/startup_header.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/host_gpus.hpp"

#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSysInfo>
#include <QtConcurrent>
#include <QtGlobal>

#include <libvirt/libvirt.h>

#ifndef LSW_VERSION
#define LSW_VERSION "unknown"
#endif
#ifndef LSW_BUILD_ID
#define LSW_BUILD_ID ""
#endif

namespace lsw::host::services {

QString StartupHeader::version() {
    const QString build = QStringLiteral(LSW_BUILD_ID);
    return build.isEmpty() ? QStringLiteral(LSW_VERSION)
                           : QStringLiteral(LSW_VERSION " (build %1)").arg(build);
}

QString StartupHeader::libvirt_version() {
    unsigned long lib = 0;
    if (::virGetVersion(&lib, nullptr, nullptr) != 0 || lib == 0) {
        return {};
    }
    return QStringLiteral("%1.%2.%3").arg(lib / 1000000).arg(lib / 1000 % 1000).arg(lib % 1000);
}

void StartupHeader::write() {
    const auto env = QProcessEnvironment::systemEnvironment();
    const auto show = [&env](const char* name) {
        const QString value = env.value(QLatin1String(name));
        return value.isEmpty() ? QStringLiteral("-") : value;
    };
    LSW_INFO("LSW manager {}, pid {}", version().toStdString(), QCoreApplication::applicationPid());
    LSW_INFO("host: {}, kernel {} {}, {}", QSysInfo::prettyProductName().toStdString(),
             QSysInfo::kernelType().toStdString(), QSysInfo::kernelVersion().toStdString(),
             QSysInfo::currentCpuArchitecture().toStdString());
    LSW_INFO("desktop: XDG_CURRENT_DESKTOP={} XDG_SESSION_TYPE={} WAYLAND_DISPLAY={}",
             show("XDG_CURRENT_DESKTOP").toStdString(), show("XDG_SESSION_TYPE").toStdString(),
             show("WAYLAND_DISPLAY").toStdString());
    LSW_INFO("Qt {} (built with {}), libvirt {}", qVersion(), QT_VERSION_STR,
             libvirt_version().isEmpty() ? std::string{"unknown"} : libvirt_version().toStdString());

    (void)QtConcurrent::run([] {
        const auto gpus = platform::HostGpus::enumerate();
        if (gpus.isEmpty()) {
            LSW_INFO("GPUs: none seen through Vulkan");
            return;
        }
        for (const auto& gpu : gpus) {
            LSW_INFO("GPU: {} uuid={} vendor={:04x} device={:04x} {} MiB driver={} {}{}",
                     gpu.name.toStdString(), gpu.uuid.toStdString(), gpu.vendor_id, gpu.device_id,
                     gpu.vram_bytes >> 20, gpu.driver.toStdString(), gpu.driver_info.toStdString(),
                     gpu.cpu ? " (software)" : "");
        }
    });
}

}
