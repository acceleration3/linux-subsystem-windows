#pragma once

#include <QUuid>
#include <QtGlobal>
#include <optional>

namespace lsw::host::vm {

class LibvirtConnection;

class GuestBalloon {
public:
    struct Reading {
        qint64 actual_kib = 0;
        qint64 max_kib = 0;
        std::optional<qint64> usable_kib;
    };

    explicit GuestBalloon(LibvirtConnection& conn) noexcept : conn_(conn) {}

    [[nodiscard]] std::optional<Reading> read(const QUuid& uuid) const;
    [[nodiscard]] bool set_target(const QUuid& uuid, qint64 kib) const;

private:
    LibvirtConnection& conn_;
};

}  // namespace lsw::host::vm
