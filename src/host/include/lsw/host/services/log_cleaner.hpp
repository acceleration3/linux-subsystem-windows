#pragma once

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace lsw::host::apps { class LogStore; }
namespace lsw::host::config { class Settings; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class LogCleaner : public QObject {
    Q_OBJECT
public:
    using Probe = std::function<bool()>;

    struct Result {
        int removed = 0;
        int emptied = 0;
        QStringList failed;
        bool windows_cleared = false;
        QString windows_note;
        int windows_removed = 0;
        int windows_emptied = 0;
        QStringList windows_skipped;

        [[nodiscard]] bool complete() const noexcept;
        [[nodiscard]] QString headline() const;
        [[nodiscard]] QString detail() const;
    };

    LogCleaner(config::Settings& settings, link::AgentRpcClient& rpc, apps::LogStore& store,
               Probe agent_online, Probe vm_running, QObject* parent = nullptr);
    ~LogCleaner() override;

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] const Result& result() const noexcept { return result_; }

    void start();

Q_SIGNALS:
    void started();
    void finished();

private:
    struct Windows;

    void clear_host();
    void conclude(const Windows& windows);

    config::Settings& settings_;
    link::AgentRpcClient& rpc_;
    apps::LogStore& store_;
    Probe agent_online_;
    Probe vm_running_;
    bool running_ = false;
    Result result_;
    QFuture<void> future_;
};

}
