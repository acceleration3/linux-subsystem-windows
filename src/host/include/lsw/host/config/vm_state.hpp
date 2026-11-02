#pragma once

#include "lsw/host/config/vm_name.hpp"
#include "lsw/host/config/vm_record.hpp"

#include <QList>
#include <QObject>

namespace lsw::host::config {
class VmState : public QObject {
    Q_OBJECT
public:
    explicit VmState(QObject* parent = nullptr);

    [[nodiscard]] const QList<VmRecord>& records() const noexcept { return records_; }

    QUuid add(VmRecord rec);

    void clear();

    [[nodiscard]] VmRecord* find(const QUuid& uuid);

    [[nodiscard]] const VmRecord* primary() const noexcept {
        return records_.isEmpty() ? nullptr : &records_.first();
    }

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] quint32 allocate_cid() const;

    QList<VmRecord> records_;
};

}
