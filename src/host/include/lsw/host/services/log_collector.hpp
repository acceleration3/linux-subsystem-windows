#pragma once

#include "lsw/host/services/system_report.hpp"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace lsw::host::config {
class ManagerSettings;
class Settings;
}
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class LogCollector : public QObject {
    Q_OBJECT
public:
    using InputsFn = std::function<SystemReport::Inputs()>;

    enum Step { WindowsLogs, HostLogs, SystemDetails, Archive, StepCount };

    LogCollector(config::Settings& settings, config::ManagerSettings& manager_settings,
                 link::AgentRpcClient& rpc, InputsFn inputs, QObject* parent = nullptr);
    ~LogCollector() override;

    [[nodiscard]] static QString step_text(int step);

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] const QString& archive() const noexcept { return archive_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    [[nodiscard]] const QString& windows_note() const noexcept { return windows_note_; }

    void start();

Q_SIGNALS:
    void step_started(int step);
    void step_skipped(int step, const QString& why);
    void finished(const QString& archive);
    void failed(const QString& error);

private:
    struct Job;
    struct Outcome;

    static Outcome work(LogCollector* self, const Job& job);
    void conclude(const Outcome& outcome);

    config::Settings& settings_;
    config::ManagerSettings& manager_settings_;
    link::AgentRpcClient& rpc_;
    InputsFn inputs_;
    bool running_ = false;
    QString archive_;
    QString error_;
    QString windows_note_;
    QFuture<void> future_;
};

}
