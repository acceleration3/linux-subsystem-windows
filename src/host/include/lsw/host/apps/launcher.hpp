#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {
class Launcher : public QObject {
    Q_OBJECT
public:
    explicit Launcher(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void launch(const QString& app_id, const QString& args = {}, const QString& env = {},
                const QStringList& documents = {});
    void activate(const QString& aumid, const QString& args = {},
                  const QStringList& documents = {});

Q_SIGNALS:
    void launched(const QString& app_id, quint32 pid, const QString& job, quint64 pid_created,
                  const QString& image);
    void failed(const QString& app_id, const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
};

}
