#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::vm {

class Lifecycle;

class GracefulShutdown : public QObject {
    Q_OBJECT
public:
    static constexpr int kPollMs = 2000;
    static constexpr qint64 kDefaultDeadlineMs = 3 * 60 * 1000;
    static constexpr int kRepressTicks = 15;

    GracefulShutdown(Lifecycle& lifecycle, QUuid uuid, qint64 deadline_ms = kDefaultDeadlineMs,
                     QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    void finished(bool forced);

private:
    void poll();

    Lifecycle& lifecycle_;
    QUuid uuid_;
    qint64 deadline_ms_;
    QTimer timer_;
    QElapsedTimer clock_;
    int ticks_ = 0;
    bool forced_ = false;
};

}
