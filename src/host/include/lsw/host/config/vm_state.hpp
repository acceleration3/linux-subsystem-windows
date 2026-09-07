#pragma once

#include "lsw/host/config/vm_name.hpp"
#include "lsw/host/config/vm_record.hpp"

#include <QList>
#include <QObject>

namespace lsw::host::config {

// In-memory holder for the (single) LSW VM. We deliberately do NOT
// persist this — there's exactly one VM, all its on-disk artifacts use
// stable paths derived from ``kVmName``, and the source of truth for
// "does the VM exist" is libvirt itself. The ``records()`` API + the
// ``changed`` signal are kept so the manager UI panels still work.
class VmState : public QObject {
    Q_OBJECT
public:
    explicit VmState(QObject* parent = nullptr);

    [[nodiscard]] const QList<VmRecord>& records() const noexcept { return records_; }

    // Append; returns the assigned UUID. ``vsock_cid`` is auto-allocated
    // to the next free integer >= 100.
    QUuid add(VmRecord rec);

    // Remove by uuid. No-op if not found.
    void remove(const QUuid& uuid);

    // Lookup by uuid; nullptr if not found.
    [[nodiscard]] VmRecord* find(const QUuid& uuid);

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] quint32 allocate_cid() const;

    QList<VmRecord> records_;
};

}  // namespace lsw::host::config
