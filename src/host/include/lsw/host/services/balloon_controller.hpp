#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <optional>

namespace lsw::host::vm {
class GuestBalloon;
}

namespace lsw::host::services {

class StatusHub;

class BalloonController : public QObject {
    Q_OBJECT
public:
    BalloonController(StatusHub& status, vm::GuestBalloon& balloon, QObject* parent = nullptr);

private:
    static constexpr int kPeriodMs = 5000;
    static constexpr qint64 kMostSettleMs = 30000;

    void tick();
    [[nodiscard]] bool moving(qint64 actual_kib) const;

    StatusHub& status_;
    vm::GuestBalloon& balloon_;
    QTimer timer_;
    std::optional<qint64> asked_kib_;
    QElapsedTimer asked_at_;
};

}
