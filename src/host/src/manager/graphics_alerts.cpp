#include "lsw/host/manager/graphics_alerts.hpp"

#include "lsw/host/services/status_hub.hpp"

namespace lsw::host::manager {

namespace {

using Area = services::StatusHub::Area;

}

GraphicsAlerts::GraphicsAlerts(services::StatusHub& status, QObject* parent)
    : QObject(parent), status_(status) {
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Graphics) {
            Q_EMIT changed();
        }
    });
}

QVector<GraphicsAlerts::Warning> GraphicsAlerts::warnings() const {
    const auto& g = status_.graphics();
    QVector<Warning> out;
    if (g.probed && platform::HostGraphicsProbe::missing_required(g.apis)) {
        out.push_back({Warning::Kind::NoDirect3d, true,
                       QStringLiteral("The host serves no Direct3D."),
                       QStringLiteral("DXVK and vkd3d-proton were not found, and Windows draws "
                                      "its desktop with Direct3D 11, so it will keep restarting "
                                      "until they are.")});
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
