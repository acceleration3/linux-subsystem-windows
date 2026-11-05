#include "lsw/host/manager/graphics_alerts.hpp"

#include "lsw/host/services/status_hub.hpp"

namespace lsw::host::manager {

namespace {

using Area = services::StatusHub::Area;

}

GraphicsAlerts::GraphicsAlerts(services::StatusHub& status, QObject* parent)
    : QObject(parent), status_(status) {
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        const bool running = status_.vm().running();
        const bool online = status_.vm().agent_online();
        if (online != agent_online_ || running != running_) {
            agent_online_ = online;
            running_ = running;
            Q_EMIT changed();
            return;
        }
        if (area == Area::Graphics) {
            Q_EMIT changed();
        }
    });
}

QVector<GraphicsAlerts::Warning> GraphicsAlerts::warnings() const {
    const auto& g = status_.graphics();
    const bool running = status_.vm().running();
    QVector<Warning> out;
    if (g.probed && platform::HostGraphicsProbe::missing_required(g.apis)) {
        out.push_back({Warning::Kind::NoDirect3d, true,
                       QStringLiteral("The host serves no Direct3D."),
                       QStringLiteral("DXVK and vkd3d-proton were not found, and Windows draws "
                                      "its desktop with Direct3D 11, so it will keep restarting "
                                      "until they are.")});
    }
    if (g.guest.valid && running) {
        const services::GraphicsAdapter* display =
            g.adapters.isEmpty() ? nullptr : &g.adapters.first();
        if (display == nullptr || !display->guest) {
            out.push_back({Warning::Kind::DisplayNotRunning, true,
                           QStringLiteral("Windows is not running avgpu.sys."),
                           QStringLiteral("Windows lists no accel-virt adapter at all.")});
        } else if (display->guest->no_driver()) {
            out.push_back({Warning::Kind::DisplayNoDriver, true,
                           QStringLiteral("Windows has no driver for %1.")
                               .arg(platform::HostGpus::adapter_name(display->gpu)),
                           QStringLiteral("The machine's GPU was switched after the driver package "
                                          "was installed, and the adapter carries the new GPU's "
                                          "PCI ID, so Windows has not matched it to avgpu.sys "
                                          "yet.")});
        } else if (!display->guest->running()) {
            const quint32 code = display->guest->problem_code;
            out.push_back(
                {Warning::Kind::DisplayNotRunning, true,
                 QStringLiteral("Windows is not running avgpu.sys."),
                 code != 0 ? QStringLiteral("The adapter reports problem code %1: %2.")
                                 .arg(code)
                                 .arg(services::GuestGpuStatus::explain(code, g.guest.test_signing))
                           : QStringLiteral("The adapter has not started.")});
        }
    }
    if (g.guest.valid && running) {
        for (const auto& adapter : g.adapters) {
            if (!adapter.display && (!adapter.guest || adapter.guest->no_driver())) {
                out.push_back({Warning::Kind::NoDriver, false,
                               QStringLiteral("Windows has no driver for %1.")
                                   .arg(platform::HostGpus::adapter_name(adapter.gpu)),
                               QStringLiteral("It was added as an adapter after the driver "
                                              "package was installed, so Windows has not matched "
                                              "it to avgpu.sys yet. LSW's next update of "
                                              "Windows, at the next boot, installs the driver "
                                              "for it.")});
                break;
            }
        }
    }
    for (const auto& adapter : g.adapters) {
        if (platform::HostGpus::resizable_bar_off(adapter.gpu)) {
            out.push_back({Warning::Kind::ResizableBarOff, false,
                           QStringLiteral("Resizable BAR is off for %1.")
                               .arg(platform::HostGpus::adapter_name(adapter.gpu)),
                           QStringLiteral("The CPU can reach only %1 of its VRAM, so once that "
                                          "fills, uploads through the host window go to system "
                                          "RAM. Turn on Above 4G Decoding and Resizable BAR in "
                                          "the firmware settings.")
                               .arg(platform::HostGpus::format_mib(
                                   adapter.gpu.resizable_bar->current_bytes >> 20))});
        }
    }
    return out;
}

GraphicsAlerts::Severity GraphicsAlerts::worst() const {
    Severity out = Severity::None;
    for (const auto& warning : warnings()) {
        if (warning.critical) {
            return Severity::Red;
        }
        out = Severity::Amber;
    }
    return out;
}

}
