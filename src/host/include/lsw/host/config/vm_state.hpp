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

    // Forget every record: the wizard is replacing the machine.
    void clear();

    // Lookup by uuid; nullptr if not found.
    [[nodiscard]] VmRecord* find(const QUuid& uuid);

    // The VM, or nullptr before one has been provisioned or imported.
    // There is only ever one; this is the question every caller of
    // ``records()`` was actually asking.
    [[nodiscard]] const VmRecord* primary() const noexcept {
        return records_.isEmpty() ? nullptr : &records_.first();
    }

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] quint32 allocate_cid() const;

    QList<VmRecord> records_;
};

}  // namespace lsw::host::config
