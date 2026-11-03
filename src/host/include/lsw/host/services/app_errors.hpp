#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::services {

struct AppErrorReport {
    enum class Kind { Startup, Crash };

    struct Problem {
        enum class Kind { Missing, WrongBitness, MissingExport, Invalid };

        Kind kind = Kind::Missing;
        QString module;
        QString importer;
        QString function;
        int module_bits = 0;
        int image_bits = 0;

        [[nodiscard]] bool operator==(const Problem&) const = default;
    };

    Kind kind = Kind::Startup;
    QString image;
    quint32 pid = 0;
    quint32 parent_pid = 0;
    QString app_id;
    quint32 exit_code = 0;
    QString exit_code_hex;
    QString code_name;
    QList<Problem> problems;
    QString note;
    QStringList event_text;
    QString module;
    QString offset;
    QString module_version;
    QString app_version;

    [[nodiscard]] QString image_name() const;
    [[nodiscard]] QString identity() const;
    [[nodiscard]] QString log_line() const;
};

class AppErrors : public QObject {
    Q_OBJECT
public:
    explicit AppErrors(link::AgentRpcClient& rpc, QObject* parent = nullptr);
    ~AppErrors() override;

Q_SIGNALS:
    void reported(const lsw::host::services::AppErrorReport& report);

private:
    link::AgentRpcClient& rpc_;
    link::EventSubscription sub_;
};

}
