#pragma once

#include <QFile>
#include <QObject>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::apps {

class GuestFileTransfer : public QObject {
    Q_OBJECT
public:
    explicit GuestFileTransfer(link::AgentRpcClient& rpc, QObject* parent = nullptr);
    ~GuestFileTransfer() override;

    // ``guest_name`` is what the file is called in Windows, when it is not
    // what it is called here. A ``document`` is a copy of a file opened from
    // the host: it may be empty, any name Windows can create is allowed, and
    // the agent leaves it read-only.
    void start(const QString& local_path, const QString& guest_name = {}, bool document = false);
    void cancel();

    [[nodiscard]] bool busy() const noexcept { return busy_; }

Q_SIGNALS:
    void progress(qint64 sent, qint64 total);
    void transferred(const QString& guest_path);
    void failed(const QString& error);
    void cancelled();

private:
    void next_chunk();
    bool stop_if_cancelled();
    void abort();
    void fail(const QString& error);

    link::AgentRpcClient& rpc_;
    QFile file_;
    QString upload_id_;
    qint64 size_ = 0;
    qint64 offset_ = 0;
    quint32 port_ = 0;
    bool busy_ = false;
    bool cancel_requested_ = false;
};

}  // namespace lsw::host::apps
