#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>

namespace lsw::host::vm {
class Lifecycle;
}

namespace lsw::host::services {

class StatusHub;

class HostShutdownWatch : public QObject {
    Q_OBJECT
public:
    HostShutdownWatch(vm::Lifecycle& lifecycle, StatusHub& status, QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    void shutting_down();

private Q_SLOTS:
    void prepare_for_shutdown(bool starting);

private:
    void take_lock();

    vm::Lifecycle& lifecycle_;
    StatusHub& status_;
    QDBusUnixFileDescriptor lock_;
    bool started_ = false;
};

}
