#pragma once

#include <QObject>

namespace lsw::host::config { class LogVerbosity; class ManagerSettings; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class StatusHub;

class AgentLogLevels : public QObject {
    Q_OBJECT
public:
    AgentLogLevels(StatusHub& status, link::AgentRpcClient& rpc,
                   config::ManagerSettings& settings, config::LogVerbosity& verbosity,
                   QObject* parent = nullptr);

private:
    void on_status();
    void push();

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    config::ManagerSettings& settings_;
    bool online_ = false;
};

}
