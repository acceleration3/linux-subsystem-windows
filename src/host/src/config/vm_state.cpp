#include "lsw/host/config/vm_state.hpp"

#include <QSet>

namespace lsw::host::config {

namespace {
constexpr int kFirstAutoCid = 100;
}  // namespace

VmState::VmState(QObject* parent) : QObject(parent) {}

QUuid VmState::add(VmRecord rec) {
    if (rec.uuid.isNull()) {
        rec.uuid = QUuid::createUuid();
    }
    if (rec.vsock_cid == 0) {
        rec.vsock_cid = allocate_cid();
    }
    records_.append(std::move(rec));
    Q_EMIT changed();
    return records_.last().uuid;
}

VmRecord* VmState::find(const QUuid& uuid) {
    for (auto& r : records_) {
        if (r.uuid == uuid) {
            return &r;
        }
    }
    return nullptr;
}

quint32 VmState::allocate_cid() const {
    QSet<quint32> taken;
    for (const auto& r : records_) {
        taken.insert(r.vsock_cid);
    }
    quint32 c = kFirstAutoCid;
    while (taken.contains(c)) {
        ++c;
    }
    return c;
}

}  // namespace lsw::host::config
