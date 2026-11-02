#pragma once

#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::vm { class Lifecycle; }

namespace lsw::host::provision {
class VncPortResolver : public QObject {
    Q_OBJECT
public:
    static constexpr int kIntervalMs = 250;
    static constexpr int kMaxAttempts = 80;

    VncPortResolver(vm::Lifecycle& lifecycle, QObject* parent = nullptr);

    void start(const QUuid& uuid);
    void stop();

Q_SIGNALS:
    void resolved(int port);

private Q_SLOTS:
    void on_tick();

private:
    vm::Lifecycle& lifecycle_;
    QUuid uuid_;
    int attempts_ = 0;
    QTimer* timer_;
};

}
