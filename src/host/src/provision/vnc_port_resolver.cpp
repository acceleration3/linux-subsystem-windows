#include "lsw/host/provision/vnc_port_resolver.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QTimer>

namespace lsw::host::provision {

VncPortResolver::VncPortResolver(vm::Lifecycle& lifecycle, QObject* parent)
    : QObject(parent), lifecycle_(lifecycle), timer_(new QTimer(this)) {
    timer_->setInterval(kIntervalMs);
    connect(timer_, &QTimer::timeout, this, &VncPortResolver::on_tick);
}

void VncPortResolver::start(const QUuid& uuid) {
    uuid_ = uuid;
    attempts_ = 0;
    timer_->start();
}

void VncPortResolver::stop() { timer_->stop(); }

void VncPortResolver::on_tick() {
    if (uuid_.isNull()) {
        timer_->stop();
        return;
    }
    if (++attempts_ > kMaxAttempts) {
        timer_->stop();
        LSW_WARN("provision: vnc port never appeared on domain {}",
                 uuid_.toString().toStdString());
        return;
    }
    const int port = lifecycle_.vnc_port(uuid_);
    if (port <= 0) {
        return;
    }
    timer_->stop();
    LSW_INFO("provision: vnc port resolved: 127.0.0.1:{}", port);
    Q_EMIT resolved(port);
}

}  // namespace lsw::host::provision
