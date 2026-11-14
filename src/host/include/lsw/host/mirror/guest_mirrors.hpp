#pragma once

#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/mirror/icon_badge_file.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <utility>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {
class GuestMirrors : public QObject {
    Q_OBJECT
public:
    explicit GuestMirrors(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void set_output_source(std::function<QString()> source) { output_source_ = std::move(source); }

    void open_desktop(const QString& title);

    [[nodiscard]] bool desktop_open() const { return !desktop_.isNull(); }

    void show(const QString& identity, const WindowMirror::Application& application,
              bool keep_previous);
    void forget(const QString& identity);
    void raise(const QString& identity, const QString& activation_token) const;

    [[nodiscard]] bool has(const QString& identity) const;
    [[nodiscard]] WindowMirror* mirror(const QString& identity) const;
    [[nodiscard]] quint32 pid(const QString& identity) const;
    [[nodiscard]] QString identity_of(quint32 pid, quint64 pid_created) const;

    void expect_launch();
    void launch_settled();

Q_SIGNALS:
    void host_started(qint64 pid);
    void host_ended(qint64 pid);
    void desktop_opened();
    void desktop_resized(const QSize& pixels);
    void desktop_closed();
    void desktop_ready();
    void desktop_failed(const QString& reason);
    void shown(const QString& identity);
    void failed(const QString& identity, const QString& reason);
    void closed(const QString& identity);
    void merged(const QString& identity, const QString& into);

private:
    struct Entry {
        WindowMirror::Application application;
        QPointer<WindowMirror> mirror;
        bool unowned = false;
        bool resumed = false;
    };

    struct GuestWindow {
        quint32 pid = 0;
        quint32 root_pid = 0;
        QString image;
        bool mirrorable = false;

        explicit GuestWindow(const nlohmann::json& row);

        [[nodiscard]] quint32 owner_pid() const { return root_pid != 0 ? root_pid : pid; }
    };

    [[nodiscard]] QString window_key(quint32 pid) const;
    [[nodiscard]] QString output() const;
    void follow_host(WindowMirror* mirror);
    void start(const QString& identity);
    void remove(const QString& identity);
    void identified(const QString& identity, const QString& claim);
    void suspend();
    void resume();
    void remirror(const nlohmann::json& rows);
    void resume(const QString& identity);
    [[nodiscard]] QString application_of(quint32 pid) const;
    void appeared(const GuestWindow& window);
    void adopt(const GuestWindow& window);

    link::AgentRpcClient& rpc_;
    std::function<QString()> output_source_;
    IconBadgeFile icon_badge_;
    QHash<QString, Entry> entries_;
    QHash<QString, QString> shown_by_;
    QPointer<WindowMirror> desktop_;
    QList<GuestWindow> parked_;
    int expected_launches_ = 0;
    bool suspended_ = false;
    link::EventSubscription appeared_sub_;
};

}  // namespace lsw::host::mirror
