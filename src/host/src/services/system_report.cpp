#include "lsw/host/services/system_report.hpp"

#include "lsw/host/services/startup_header.hpp"

#include <QProcessEnvironment>
#include <QSysInfo>
#include <QtGlobal>

namespace lsw::host::services {

namespace {

QString or_dash(const QString& text) {
    return text.trimmed().isEmpty() ? QStringLiteral("-") : text.trimmed();
}

QString environment(const char* name) {
    return or_dash(QProcessEnvironment::systemEnvironment().value(QLatin1String(name)));
}

QString gpu_line(const platform::HostGpu& gpu) {
    return QStringLiteral("  %1, uuid %2, vendor %3, device %4, %5 MiB, driver %6 %7%8")
        .arg(or_dash(gpu.name), or_dash(gpu.uuid),
             QStringLiteral("%1").arg(gpu.vendor_id, 4, 16, QLatin1Char('0')),
             QStringLiteral("%1").arg(gpu.device_id, 4, 16, QLatin1Char('0')))
        .arg(gpu.vram_bytes >> 20)
        .arg(or_dash(gpu.driver), or_dash(gpu.driver_info),
             gpu.cpu ? QStringLiteral(", software") : QString{});
}

void section(QString& out, const QString& title, const QString& body) {
    out += QStringLiteral("\n== %1 ==\n%2\n").arg(title, body.endsWith(QLatin1Char('\n')) ? body.chopped(1) : body);
}

}

QString SystemReport::render(const Inputs& inputs) {
    QString out;

    QString lsw;
    lsw += QStringLiteral("LSW: %1\n").arg(StartupHeader::version());
    lsw += QStringLiteral("Release shipped: %1\n").arg(or_dash(inputs.shipped_release));
    lsw += QStringLiteral("Release in Windows: %1").arg(or_dash(inputs.guest_release));
    section(out, QStringLiteral("LSW"), lsw);

    QString host;
    host += QStringLiteral("Distribution: %1\n").arg(or_dash(QSysInfo::prettyProductName()));
    host += QStringLiteral("Kernel: %1 %2, %3\n")
                .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                     QSysInfo::currentCpuArchitecture());
    host += QStringLiteral("Desktop: %1\n").arg(environment("XDG_CURRENT_DESKTOP"));
    host += QStringLiteral("Session type: %1\n").arg(environment("XDG_SESSION_TYPE"));
    host += QStringLiteral("Wayland display: %1\n").arg(environment("WAYLAND_DISPLAY"));
    host += QStringLiteral("Qt: %1 (built with %2)\n")
                .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR));
    host += QStringLiteral("libvirt: %1").arg(or_dash(StartupHeader::libvirt_version()));
    section(out, QStringLiteral("Host"), host);

    QString gpus;
    if (!inputs.gpus_known) {
        gpus = QStringLiteral("-");
    } else if (inputs.gpus.isEmpty()) {
        gpus = QStringLiteral("none seen through Vulkan");
    } else {
        QStringList lines;
        for (const auto& gpu : inputs.gpus) {
            lines << gpu_line(gpu);
        }
        gpus = lines.join(QLatin1Char('\n'));
    }
    section(out, QStringLiteral("Host GPUs"), gpus);

    if (inputs.agent_online && inputs.agent_note.isEmpty()) {
        section(out, QStringLiteral("Windows system_info"), or_dash(inputs.agent_system_info));
        section(out, QStringLiteral("Windows gpu_status"), or_dash(inputs.agent_gpu_status));
    } else {
        section(out, QStringLiteral("Windows"),
                QStringLiteral("Not collected: %1")
                    .arg(inputs.agent_note.isEmpty() ? QStringLiteral("Windows did not answer")
                                                     : inputs.agent_note));
    }

    section(out, QStringLiteral("VM definition"), or_dash(inputs.domain_xml));
    return out.mid(1);
}

}
