#pragma once

#include "lsw/host/mirror/mirror_log.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {
class WindowMirror : public QObject {
    Q_OBJECT
public:
    struct Application {
        quint32 pid = 0;
        QString image;
        QString aumid;
        QString desktop_id;
        QString log_name;
        QString activation_token;
        QString output;
        quint64 pid_created = 0;
    };

    ~WindowMirror() override;
    [[nodiscard]] static WindowMirror* of_application(link::AgentRpcClient& rpc, const Application& application,
                                                      QObject* parent = nullptr);

    [[nodiscard]] static WindowMirror* of_desktop(link::AgentRpcClient& rpc, const QString& title,
                                                  const QString& output, QObject* parent = nullptr);

    bool start();

    void raise(const QString& activation_token) const;

    [[nodiscard]] const QStringList& app_ids() const noexcept { return app_ids_; }

Q_SIGNALS:
    void app_identified(const QString& aumid);
    void finished();
    void ready();
    void failed(const QString& reason);

private:
    WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                 quint32 guest_pid, bool desktop, QString log_name, QString activation_token,
                 QObject* parent);

    void on_host_end_finished(int code, QProcess::ExitStatus status);
    void read_status();
    void read_log();
    void start_guest(quint16 port);
    void fail(const QString& reason);
    void announce_ready();

    link::AgentRpcClient& rpc_;
    QString what_;
    QStringList host_arguments_;
    quint32 guest_pid_;
    bool desktop_;
    QString log_name_;
    QString activation_token_;
    QStringList app_ids_;
    QProcess* host_end_;
    MirrorLog log_;
    QElapsedTimer running_;
    QTimer deadline_;
    QByteArray status_buffer_;
    bool started_ = false;
    bool guest_started_ = false;
    bool guest_confirmed_ = false;
    bool first_frame_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

}
