#include "lsw/host/platform/host_monitors.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace lsw::host::platform {

namespace {
struct Fd {
    explicit Fd(int fd) : value(fd) {}
    ~Fd() {
        if (value >= 0) {
            ::close(value);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int value;
};

QVector<qint64> rates_of(const drmModeConnector& connector) {
    QVector<qint64> rates;
    for (int i = 0; i < connector.count_modes; ++i) {
        const drmModeModeInfo& mode = connector.modes[i];
        if ((mode.flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)) != 0) {
            continue;
        }
        const qint64 rate = HostMonitors::mode_millihz(mode.clock, mode.htotal, mode.vtotal);
        if (rate > 0 && !rates.contains(rate)) {
            rates.push_back(rate);
        }
    }
    std::sort(rates.begin(), rates.end(), std::greater<>());
    return rates;
}

QSize active_mode_of(int fd, const drmModeConnector& connector) {
    if (connector.encoder_id == 0) {
        return {};
    }
    QSize size;
    if (drmModeEncoder* encoder = drmModeGetEncoder(fd, connector.encoder_id)) {
        if (encoder->crtc_id != 0) {
            if (drmModeCrtc* crtc = drmModeGetCrtc(fd, encoder->crtc_id)) {
                if (crtc->mode_valid != 0) {
                    size = QSize(crtc->mode.hdisplay, crtc->mode.vdisplay);
                }
                drmModeFreeCrtc(crtc);
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return size;
}

}

qint64 HostMonitors::mode_millihz(quint32 clock_khz, quint32 htotal, quint32 vtotal) {
    const qint64 frame = qint64{htotal} * vtotal;
    if (frame <= 0) {
        return 0;
    }
    return (qint64{clock_khz} * 1'000'000 + frame / 2) / frame;
}

QVector<HostMonitor> HostMonitors::read() {
    QVector<HostMonitor> monitors;
    const QStringList nodes =
        QDir(QStringLiteral("/dev/dri")).entryList({QStringLiteral("card*")}, QDir::System | QDir::Files,
                                                   QDir::Name);
    for (const QString& node : nodes) {
        const QByteArray path = QStringLiteral("/dev/dri/%1").arg(node).toLocal8Bit();
        Fd fd(::open(path.constData(), O_RDWR | O_CLOEXEC));
        if (fd.value < 0) {
            LSW_DEBUG("monitor rates: {} cannot be opened", path.toStdString());
            continue;
        }
        drmModeRes* resources = drmModeGetResources(fd.value);
        if (resources == nullptr) {
            continue;
        }
        for (int i = 0; i < resources->count_connectors; ++i) {
            drmModeConnector* connector =
                drmModeGetConnectorCurrent(fd.value, resources->connectors[i]);
            if (connector == nullptr) {
                continue;
            }
            if (connector->connection == DRM_MODE_CONNECTED) {
                HostMonitor found;
                found.name = QStringLiteral("%1-%2")
                                 .arg(QString::fromUtf8(drmModeGetConnectorTypeName(connector->connector_type)))
                                 .arg(connector->connector_type_id);
                found.millihz = rates_of(*connector);
                found.mode = active_mode_of(fd.value, *connector);
                if (!found.millihz.isEmpty() || found.mode.isValid()) {
                    monitors.push_back(std::move(found));
                }
            }
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);
    }
    return monitors;
}

QVector<RateChoice> HostMonitors::merge(const QVector<HostMonitor>& monitors,
                                            const QVector<HostMonitor>& screens) {
    struct Entry {
        qint64 millihz;
        QString monitor;
    };
    QVector<Entry> all;
    for (const auto* source : {&monitors, &screens}) {
        for (const HostMonitor& monitor : *source) {
            for (const qint64 rate : monitor.millihz) {
                all.push_back({rate, monitor.name});
            }
        }
    }
    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        return a.millihz != b.millihz ? a.millihz > b.millihz : a.monitor < b.monitor;
    });
    QVector<RateChoice> choices;
    qint64 anchor = 0;
    for (const Entry& entry : all) {
        if (choices.isEmpty() || anchor - entry.millihz >= kSameChoiceMillihz) {
            choices.push_back({entry.millihz, {}});
            anchor = entry.millihz;
        }
        const auto off_whole = [](qint64 millihz) { return std::min(millihz % 1000, 1000 - millihz % 1000); };
        if (off_whole(entry.millihz) < off_whole(choices.last().millihz)) {
            choices.last().millihz = entry.millihz;
        }
        if (!entry.monitor.isEmpty() && !choices.last().monitors.contains(entry.monitor)) {
            choices.last().monitors.push_back(entry.monitor);
        }
    }
    return choices;
}

}
